#include "VideoDecoder.h"
#include "Log/Log.h"
#include "MediaEncoder/VideoEncoder.h"

#define MAX_CPU_COUNT (12)

VideoDecoder::VideoDecoder()
    :m_DecoderLock()
{
    m_pVideoInfo = nullptr;
    m_pAVCodec = nullptr;
    m_pAVContext = nullptr;
    m_pAVParserContext = nullptr;
    m_pFrame = nullptr;
    m_pPacket = nullptr;
    m_bEnableLowDlay = false;
    m_pVideoFrameCallbaclk = nullptr;
    m_pResampleFrame = nullptr;
    m_pResampleContext = nullptr;
    m_nResampleWidth = 0;
    m_nResampleHight = 0;
    m_eResampFormat = AV_PIX_FMT_NONE;
}

VideoDecoder::~VideoDecoder()
{
    DestroyDecoder();
}

//调用它的大部分函数需要上锁，为了方便此函数不加锁，上锁的逻辑由外层调用者控制
int32_t VideoDecoder::DestroyDecoder()
{
    if (m_pVideoInfo != nullptr)
    {
        delete m_pVideoInfo;
        m_pVideoInfo = nullptr;
    }
    if (m_pAVContext != nullptr)
    {
        avcodec_free_context(&m_pAVContext);
    }
    if (m_pAVParserContext != nullptr)
    {
        av_parser_close(m_pAVParserContext);
        m_pAVParserContext = nullptr;
    }
    if (m_pFrame != nullptr)
    {
        av_frame_free(&m_pFrame);
    }
    if (m_pPacket != nullptr)
    {
        av_packet_free(&m_pPacket);
    }

    if (m_pResampleFrame != nullptr)
    {
        av_frame_free(&m_pResampleFrame);
    }
    if (m_pResampleContext != nullptr)
    {
        sws_freeContext(m_pResampleContext);
    }
    m_nResampleWidth = 0;
    m_nResampleHight = 0;
    m_pVideoFrameCallbaclk = nullptr;
    m_eResampFormat = AV_PIX_FMT_NONE;

    return 0;
}

int32_t VideoDecoder::AddVideoStream(const VideoInfo& info)
{
    {
        std::lock_guard<std::mutex> lock(m_DecoderLock);

        if (m_pAVContext != nullptr)
        {
            DestroyDecoder();
            Warn("[%p][VideoDecoder::AddVideoStream] The decoder has already added stream,old decoder will be destroyed", this);
        }

        m_pAVCodec = info.m_nCodecID == AV_CODEC_ID_H264 ? avcodec_find_decoder_by_name("h264_v4l2m2m") :
            avcodec_find_decoder((AVCodecID)info.m_nCodecID);
        if (m_pAVCodec == nullptr)
        {
            DestroyDecoder();
            Error("[%p][VideoDecoder::AddVideoStream] Can not find decoder.AVCodecID:%d", this, info.m_nCodecID);
            return -1;
        }

        m_pAVContext = avcodec_alloc_context3(m_pAVCodec);
        if (m_pAVContext == nullptr)
        {
            DestroyDecoder();
            Error("[%p][VideoDecoder::AddVideoStream] Alloc AVCodecContext fail", this);
            return -2;
        }

        if ((AVCodecID)info.m_nCodecID == AV_CODEC_ID_H264 || (AVCodecID)info.m_nCodecID == AV_CODEC_ID_HEVC)
        {
            m_pAVParserContext = av_parser_init((AVCodecID)info.m_nCodecID);
            if (m_pAVParserContext == nullptr)
            {
                Trace("[%p][VideoDecoder::AddVideoStream] Init AvParser  fail", this);
            }
        }

        m_pAVContext->thread_count = std::max(1, std::min(av_cpu_count(), MAX_CPU_COUNT));
        if (m_bEnableLowDlay)
        {
            m_pAVParserContext->flags |= AV_CODEC_FLAG_LOW_DELAY;
        }

        m_pAVContext->codec_type = AVMEDIA_TYPE_VIDEO;
        m_pAVContext->pix_fmt = AV_PIX_FMT_YUVJ420P;
        m_pAVContext->width = info.m_nWidth;
        m_pAVContext->height = info.m_nHight;
        m_pAVContext->coded_width = info.m_nWidth;
        m_pAVContext->coded_height = info.m_nHight;

        if (info.m_pExtraData != nullptr && info.m_uiExtraDataLen > 0)
        {
            uint8_t* extra = (uint8_t*)av_malloc(info.m_uiExtraDataLen);
            if (extra != nullptr)
            {
                memcpy(extra, info.m_pExtraData, info.m_uiExtraDataLen);
                m_pAVContext->extradata = extra;
                m_pAVContext->extradata_size = info.m_uiExtraDataLen;
            }
            else
            {
                Error("[%p][VideoDecoder::AddVideoStream] Malloc extra data fail", this);
            }
        }
        else
        {
            Trace("[%p][VideoDecoder::AddVideoStream] No extra data", this);
        }

        int ret = avcodec_open2(m_pAVContext, m_pAVCodec, nullptr);
        if (ret < 0)
        {
            char msg[128] = { 0 };
            av_strerror(ret, msg, sizeof(msg));
            Error("[%p][VideoDecoder::AddVideoStream] Open decoder fail,err:%s", this, msg);
            DestroyDecoder();
            return -3;
        }

        if (m_pFrame == nullptr)
        {
            m_pFrame = av_frame_alloc();
            if (m_pFrame == nullptr)
            {
                Error("[%p][VideoDecoder::AddVideoStream] Alloc frame fail", this);
                DestroyDecoder();
                return -4;
            }
        }

        if (m_pPacket == nullptr)
        {
            m_pPacket = av_packet_alloc();
            if (m_pPacket == nullptr)
            {
                Error("[%p][VideoDecoder::AddVideoStream] Alloc packet fail", this);
                DestroyDecoder();
                return -5;
            }
        }

        if (m_pVideoInfo != nullptr)
        {
            delete m_pVideoInfo;
        }
        m_pVideoInfo = new VideoInfo(info);

    }

    return 0;
}

int32_t VideoDecoder::SetVideoFrameCallBack(VideoFrameCallbaclk callback)
{
    {
        std::lock_guard<std::mutex> lock(m_DecoderLock);
        if (m_pVideoFrameCallbaclk != nullptr)
        {
            Warn("[%p][VideoDecoder::SetVideoFrameCallBack] The callback has been set", this);
        }
        m_pVideoFrameCallbaclk = callback;
    }

    return 0;
}

int32_t VideoDecoder::RecvVideoPacket(std::shared_ptr<VideoPacket>& packet)
{
    {
        std::lock_guard<std::mutex> lock(m_DecoderLock);
        if (m_pAVContext == nullptr)
        {
            Error("[%p][VideoDecoder::RecvVideoPacket] Decoder have not add stream", this);
            return false;
        }

        m_pPacket->dts = packet->m_lDTS;
        m_pPacket->pts = packet->m_lPTS;

        if (m_pAVParserContext != nullptr)
        {
            uint8_t* data = packet->m_pData;
            uint32_t len = packet->m_nLength;
            while (len > 0)
            {
                int uselen = av_parser_parse2(m_pAVParserContext, m_pAVContext, &m_pPacket->data,
                    &m_pPacket->size, data, len, 0, 0, 0);

                data += uselen;
                len -= uselen;

                if (uselen == 0 && m_pPacket->size == 0)
                {
                    break;
                }

                if (m_pPacket->size > 0)
                {
                    DecodePacket(m_pPacket);
                }
            }
        }
        else
        {
            m_pPacket->data = packet->m_pData;
            m_pPacket->size = packet->m_nLength;
            DecodePacket(m_pPacket);
        }
    }

    return 0;
}

int32_t VideoDecoder::DecodePacket(const AVPacket* pPacket)
{
    int ret_send = avcodec_send_packet(m_pAVContext, pPacket);

    if (ret_send == AVERROR(EAGAIN) || ret_send == 0)
    {
        int ret_recv = 0;
        while (true)
        {
            ret_recv = avcodec_receive_frame(m_pAVContext, m_pFrame);

            if (ret_recv == AVERROR(EAGAIN) || ret_recv == AVERROR_EOF)
            {
                av_frame_unref(m_pFrame);
                break;
            }

            if (ret_recv == 0)
            {
                OutputVideoFrame(m_pFrame);
                av_frame_unref(m_pFrame);
            }
        }
    }
    else
    {
        Error("[%p][VideoDecoder::DecodePacket] Send packet fail,return:%d", this, ret_send);
        return -1;
    }

    return 0;
}

int32_t VideoDecoder::OutputVideoFrame(const AVFrame* pFrame)
{
    if (m_pVideoFrameCallbaclk == nullptr)
    {
        Error("[%p][VideoDecoder::OutputVideoFrame] m_pVideoFrameCallbaclk is null", this);
        return -1;
    }

    AVFrame* pYuv420Frame = nullptr;
    if (pFrame->format != AV_PIX_FMT_YUV420P && pFrame->format != AV_PIX_FMT_YUVJ420P)
    {
        int32_t ret = Resample(pFrame);
        if (ret != 0)
        {
            Error("[%p][VideoDecoder::OutputVideoFrame] Resample fail,retun:%d", this, ret);
            return -2;
        }
        pYuv420Frame = m_pResampleFrame;
    }
    else
    {
        pYuv420Frame = (AVFrame*)pFrame;
    }

    if (pYuv420Frame == nullptr)
    {
        Error("[%p][VideoDecoder::OutputVideoFrame] pYuv420Frame is null", this);
        return -3;
    }

    std::shared_ptr<VideoFrame> pVideoFrame = std::make_shared<VideoFrame>();
    pVideoFrame->m_pData = (uint8_t*)malloc(pYuv420Frame->width * pYuv420Frame->height * 3 / 2);
    if (pVideoFrame->m_pData == nullptr)
    {
        Error("[%p][VideoDecoder::OutputVideoFrame] malloc data faill", this);
        return -4;
    }
    else
    {
        pVideoFrame->m_nWidth = pYuv420Frame->width;
        pVideoFrame->m_nHeight = pYuv420Frame->height;

        memcpy(pVideoFrame->m_pData, pYuv420Frame->data[0], pYuv420Frame->width * pYuv420Frame->height);
        int uoffset = pYuv420Frame->width * pYuv420Frame->height;
        memcpy(pVideoFrame->m_pData + uoffset, pYuv420Frame->data[1], pYuv420Frame->width * pYuv420Frame->height / 4);
        int voffset = pYuv420Frame->width * pYuv420Frame->height * 5 / 4;
        memcpy(pVideoFrame->m_pData + voffset, pYuv420Frame->data[2], pYuv420Frame->width * pYuv420Frame->height / 4);

        pVideoFrame->m_nLength = pYuv420Frame->width * pYuv420Frame->height * 3 / 2;
        pVideoFrame->m_nFrameType = pYuv420Frame->format;
        pVideoFrame->m_lPTS = pFrame->pts;

        m_pVideoFrameCallbaclk(pVideoFrame);
        pVideoFrame = nullptr;
    }

    return 0;
}

int32_t VideoDecoder::FlushDecoder()
{
    {
        std::lock_guard<std::mutex> lock(m_DecoderLock);
        if (m_pAVContext != nullptr)
        {
            avcodec_flush_buffers(m_pAVContext);
        }
    }

    return 0;
}

int32_t VideoDecoder::Resample(const AVFrame* pFrame)
{
    if (m_nResampleWidth != pFrame->width || m_nResampleHight != pFrame->height
        || m_eResampFormat != pFrame->format)
    {
        if (m_pResampleContext != nullptr)
        {
            sws_freeContext(m_pResampleContext);
            m_pResampleContext = nullptr;
        }
        if (m_pResampleFrame != nullptr)
        {
            av_frame_free(&m_pResampleFrame);
        }
    }

    if (m_pResampleFrame == nullptr)
    {
        m_pResampleFrame = av_frame_alloc();
        if (m_pResampleFrame == nullptr)
        {
            Error("[%p][VideoDecoder::Resample] Alloc frame fail", this);
            return -1;
        }
        m_pResampleFrame->width = pFrame->width;
        m_pResampleFrame->height = pFrame->height;
        m_pResampleFrame->format = AV_PIX_FMT_YUVJ420P;
        int ret = av_frame_get_buffer(m_pResampleFrame, 32);
        if (ret != 0)
        {
            Error("[%p][VideoDecoder::Resample] av_frame_get_buffer  fail,return:%d", this, ret);
            av_frame_free(&m_pResampleFrame);
            return -2;
        }
    }

    if (m_pResampleContext == nullptr)
    {
        m_pResampleContext = sws_getContext(pFrame->width, pFrame->height, (AVPixelFormat)pFrame->format,
            pFrame->width, pFrame->height, AV_PIX_FMT_YUVJ420P, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
        if (m_pResampleContext == nullptr)
        {
            Error("[%p][VideoDecoder::Resample] sws_getContext fail,width:%d height:%d format:%d",
                this, pFrame->width, pFrame->height, pFrame->format);
            return -3;
        }
        m_nResampleWidth = pFrame->width;
        m_nResampleHight = pFrame->height;
        m_eResampFormat = (AVPixelFormat)pFrame->format;
    }

    int ret = sws_scale(m_pResampleContext, (uint8_t const**)pFrame->data, pFrame->linesize, 0, m_pResampleFrame->height,
        m_pResampleFrame->data, m_pResampleFrame->linesize);
    if (ret < 0)
    {
        Error("[%p][VideoDecoder::Resample] sws_scale fail,return:%d", ret);
        return -4;
    }

    return 0;
}