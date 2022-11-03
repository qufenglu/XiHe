#include "VideoEncoder.h"
#include "Log/Log.h"


VideoEncoder::VideoEncoder() :
    m_EncoderLock()
{
    m_pVideoInfo = nullptr;
    m_pAVCodec = nullptr;
    m_pAVContext = nullptr;
    m_pFrame = nullptr;
    m_pPacket = nullptr;
    m_pVideoPacketCallback = nullptr;
}

VideoEncoder::~VideoEncoder()
{
    ReleaseAll();
}

int32_t VideoEncoder::ReleaseAll()
{
    if (m_pAVContext != nullptr)
    {
        avcodec_free_context(&m_pAVContext);
    }
    if (m_pFrame != nullptr)
    {
        av_frame_free(&m_pFrame);
    }
    if (m_pPacket != nullptr)
    {
        av_packet_free(&m_pPacket);
    }
    if (m_pVideoInfo != nullptr)
    {
        delete m_pVideoInfo;
        m_pVideoInfo = nullptr;
    }

    return 0;
}

int32_t VideoEncoder::OpenEncoder(const EncodParam& param)
{
    {
        std::lock_guard<std::mutex> lock(m_EncoderLock);

        if (m_pAVContext != nullptr)
        {
            Error("[%p][VideoEncoder::OpenEncoder] Encoder has been opened", this);
            return -1;
        }

        //m_pAVCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
        //m_pAVCodec = avcodec_find_encoder_by_name("h264_omx");
        //m_pAVCodec = avcodec_find_encoder_by_name("h264_mmal");
        m_pAVCodec = avcodec_find_encoder_by_name("h264_v4l2m2m");
        if (m_pAVCodec == nullptr)
        {
            ReleaseAll();
            Error("[%p][VideoEncoder::OpenEncoder] Can not find encoder", this);
            return -2;
        }
        m_pAVContext = avcodec_alloc_context3(m_pAVCodec);
        if (m_pAVContext == nullptr)
        {
            ReleaseAll();
            Error("[%p][VideoEncoder::OpenEncoder] Alloc AVCodecContext fail", this);
            return -3;
        }

        m_pAVContext->codec_id = AV_CODEC_ID_H264;
        m_pAVContext->codec_type = AVMEDIA_TYPE_VIDEO;
        m_pAVContext->pix_fmt = AV_PIX_FMT_YUV420P;
        m_pAVContext->width = param.m_nWidth;
        m_pAVContext->height = param.m_nHeight;
        m_pAVContext->time_base.num = 1;
        m_pAVContext->time_base.den = 25;
        m_pAVContext->bit_rate = param.m_nBitRate;
        m_pAVContext->gop_size = 50;
        m_pAVContext->qmin = 10;
        m_pAVContext->qmax = 51;
        m_pAVContext->max_b_frames = 0;

        AVDictionary* param = 0;
        if (m_pAVContext->codec_id == AV_CODEC_ID_H264)
        {
            av_dict_set(&param, "preset", "slow", 0);
            av_dict_set(&param, "tune", "zerolatency", 0);
        }
        //m_pAVContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        if (avcodec_open2(m_pAVContext, m_pAVCodec, nullptr) < 0)
        {
            ReleaseAll();
            Error("[%p][VideoEncoder::OpenEncoder] open avcodec fail", this);
            return -4;
        }

        if (m_pFrame == nullptr)
        {
            m_pFrame = av_frame_alloc();
            if (m_pFrame == nullptr)
            {
                Error("[%p][VideoEncoder::OpenEncoder] Alloc frame fail", this);
                ReleaseAll();
                return -5;
            }
        }

        if (m_pPacket == nullptr)
        {
            m_pPacket = av_packet_alloc();
            if (m_pPacket == nullptr)
            {
                Error("[%p][VideoEncoder::OpenEncoder] Alloc packet fail", this);
                ReleaseAll();
                return -6;
            }
        }

        if (m_pVideoInfo != nullptr)
        {
            delete m_pVideoInfo;
        }
        m_pVideoInfo = new VideoInfo();
        m_pVideoInfo->m_nCodecID = m_pAVContext->codec_id;
        m_pVideoInfo->m_nWidth = m_pAVContext->width;
        m_pVideoInfo->m_nHight = m_pAVContext->height;
        if (m_pAVContext->extradata_size > 0)
        {
            m_pVideoInfo->m_pExtraData = (uint8_t*)malloc(m_pAVContext->extradata_size);
            if (m_pVideoInfo->m_pExtraData != nullptr)
            {
                memcpy(m_pVideoInfo->m_pExtraData, m_pAVContext->extradata, m_pAVContext->extradata_size);
                m_pVideoInfo->m_uiExtraDataLen = m_pAVContext->extradata_size;
            }
        }
    }

    return 0;
}

int32_t VideoEncoder::CloseEncoder()
{
    {
        std::lock_guard<std::mutex> lock(m_EncoderLock);
        return ReleaseAll();
    }

    return 0;
}

int32_t VideoEncoder::EncodeFrame(std::shared_ptr<VideoFrame> pVideoPacket)
{
    if (pVideoPacket->m_nFrameType != AV_PIX_FMT_YUV420P && pVideoPacket->m_nFrameType != AV_PIX_FMT_YUVJ420P)
    {
        Error("[%p][VideoEncoder::EncodeFrame] not support FrameType:%d", this, pVideoPacket->m_nFrameType);
        return -1;
    }

    {
        std::lock_guard<std::mutex> lock(m_EncoderLock);

        //m_pFrame->format = pVideoPacket->m_nFrameType;
        m_pFrame->format = AV_PIX_FMT_YUVJ420P;
        m_pFrame->width = pVideoPacket->m_nWidth;
        m_pFrame->height = pVideoPacket->m_nHeight;
        m_pFrame->pts = pVideoPacket->m_lPTS;

        av_frame_get_buffer(m_pFrame, 0);
        av_frame_make_writable(m_pFrame);

        av_image_copy_plane(m_pFrame->data[0], m_pFrame->linesize[0], pVideoPacket->m_pData, pVideoPacket->m_nWidth, pVideoPacket->m_nWidth, pVideoPacket->m_nHeight);
        av_image_copy_plane(m_pFrame->data[1], m_pFrame->linesize[1], pVideoPacket->m_pData + (pVideoPacket->m_nWidth * pVideoPacket->m_nHeight),
            pVideoPacket->m_nWidth >> 1, pVideoPacket->m_nWidth >> 1, pVideoPacket->m_nHeight >> 1);
        av_image_copy_plane(m_pFrame->data[2], m_pFrame->linesize[2], pVideoPacket->m_pData + (pVideoPacket->m_nWidth * pVideoPacket->m_nHeight * 5 / 4),
            pVideoPacket->m_nWidth >> 1, pVideoPacket->m_nWidth >> 1, pVideoPacket->m_nHeight >> 1);


        /*memcpy(m_pFrame->data[0], pVideoPacket->m_pData, pVideoPacket->m_nWidth * pVideoPacket->m_nHeight);
        memcpy(m_pFrame->data[1], pVideoPacket->m_pData + (pVideoPacket->m_nWidth * pVideoPacket->m_nHeight), (pVideoPacket->m_nWidth * pVideoPacket->m_nHeight) >> 2);
        memcpy(m_pFrame->data[2], pVideoPacket->m_pData + (pVideoPacket->m_nWidth * pVideoPacket->m_nHeight * 5 / 4), (pVideoPacket->m_nWidth * pVideoPacket->m_nHeight) >> 2);*/

        int nRetSend = avcodec_send_frame(m_pAVContext, m_pFrame);
        av_frame_unref(m_pFrame);

        if (nRetSend == AVERROR(EAGAIN) || nRetSend == 0)
        {
            int nRetRecv = 0;
            while (true)
            {
                nRetRecv = avcodec_receive_packet(m_pAVContext, m_pPacket);

                if (nRetRecv == AVERROR(EAGAIN) || nRetRecv == AVERROR_EOF)
                {
                    av_packet_unref(m_pPacket);
                    break;
                }
                else if (nRetRecv == 0)
                {
                    OutputVideoPacket();
                    av_packet_unref(m_pPacket);
                }
                else
                {
                    Error("[%p][VideoEncoder::EncodeFrame] receive packet err,ret:%d", this, nRetRecv);
                }
            }
        }

    }

    return 0;
}

int32_t VideoEncoder::EncodeFrame(const AVFrame* pFrame)
{
    {
        std::lock_guard<std::mutex> lock(m_EncoderLock);

        int nRetSend = avcodec_send_frame(m_pAVContext, pFrame);

        if (nRetSend == AVERROR(EAGAIN) || nRetSend == 0)
        {
            int nRetRecv = 0;
            while (true)
            {
                nRetRecv = avcodec_receive_packet(m_pAVContext, m_pPacket);

                if (nRetRecv == AVERROR(EAGAIN) || nRetRecv == AVERROR_EOF)
                {
                    av_packet_unref(m_pPacket);
                    break;
                }
                else if (nRetRecv == 0)
                {
                    OutputVideoPacket();
                    av_packet_unref(m_pPacket);
                }
                else
                {
                    Error("[%p][VideoEncoder::EncodeFrame] receive packet err,ret:%d", this, nRetRecv);
                }
            }
        }

    }

    return 0;
}

int32_t VideoEncoder::SetVideoPacketCallback(VideoPacketCallbaclk pCallback)
{
    {
        std::lock_guard<std::mutex> lock(m_EncoderLock);
        m_pVideoPacketCallback = pCallback;
    }

    return 0;
}

int32_t VideoEncoder::OutputVideoPacket()
{
    if (m_pVideoPacketCallback == nullptr)
    {
        Error("[%p][VideoEncoder::OutputVideoPacket] VideoPacketCallback is null", this);
        return -1;
    }

    std::shared_ptr<VideoPacket> pVideoPacket = std::make_shared<VideoPacket>();
    if (pVideoPacket == nullptr)
    {
        Error("[%p][VideoEncoder::OutputVideoPacket] new VideoPacket fail", this);
        return -2;
    }

    pVideoPacket->m_lDTS = m_pPacket->dts;
    pVideoPacket->m_lPTS = m_pPacket->pts;
    pVideoPacket->m_nFrameType = m_pAVContext->codec_id;
    pVideoPacket->m_pData = (uint8_t*)malloc(m_pPacket->size - 4);
    if (pVideoPacket->m_pData == nullptr)
    {
        Error("[%p][VideoEncoder::OutputVideoPacket] malloc video data fail", this);
        pVideoPacket = nullptr;
        return -3;
    }
    memcpy(pVideoPacket->m_pData, m_pPacket->data + 4, m_pPacket->size - 4);
    pVideoPacket->m_nLength = m_pPacket->size - 4;

    m_pVideoPacketCallback(pVideoPacket);

    return 0;
}

const uint8_t* VideoEncoder::GetSPS(uint32_t& len)
{
    uint8_t* sps = nullptr;
    uint32_t size = 0;
    FindSPS(m_pVideoInfo->m_pExtraData, m_pVideoInfo->m_uiExtraDataLen, sps, size);

    len = size;
    return sps;
}

const uint8_t* VideoEncoder::GetPPS(uint32_t& len)
{
    uint8_t* pps = nullptr;
    uint32_t size = 0;
    FindPPS(m_pVideoInfo->m_pExtraData, m_pVideoInfo->m_uiExtraDataLen, pps, size);

    len = size;
    return pps;
}

bool FindSPS(uint8_t* data, uint32_t size, uint8_t*& sps, uint32_t& spsSize)
{
    sps = nullptr;
    spsSize = 0;

    if (data == nullptr || size < 4)
    {
        return false;
    }

    uint32_t pos = 4;
    while (pos < size)
    {
        if ((data[pos] & 0x1f) == 0x07)
        {
            if (data[pos - 1] == 1 && data[pos - 2] == 0 && data[pos - 3] == 0 && data[pos - 4] == 0)
            {
                sps = &data[pos];
                break;
            }
        }
        pos++;
    }

    uint32_t endpos = pos + 1;
    bool hasFindStart = false;
    while (endpos < size - 3)
    {
        if (data[endpos] == 0)
        {
            if (data[endpos + 1] == 0 && data[endpos + 2] == 0 && data[endpos + 3] == 1)
            {
                hasFindStart = true;
                break;
            }
        }
        endpos++;
    }

    if (hasFindStart)
    {
        spsSize = endpos - pos;
    }
    else
    {
        spsSize = size - pos;
    }

    return sps != nullptr;
}

bool FindPPS(uint8_t* data, uint32_t size, uint8_t*& pps, uint32_t& ppsSize)
{
    pps = nullptr;
    ppsSize = 0;

    if (data == nullptr || size == 0)
    {
        return false;
    }

    uint32_t pos = 4;
    while (pos < size)
    {
        if ((data[pos] & 0x1f) == 0x08)
        {
            if (data[pos - 1] == 1 && data[pos - 2] == 0 && data[pos - 3] == 0 && data[pos - 4] == 0)
            {
                pps = &data[pos];
                break;
            }
        }
        pos++;
    }

    uint32_t endpos = pos + 1;
    bool hasFindStart = false;
    while (endpos < size - 3)
    {
        if (data[endpos] == 0)
        {
            if (data[endpos + 1] == 0 && data[endpos + 2] == 0 && data[endpos + 3] == 1)
            {
                hasFindStart = true;
                break;
            }
        }
        endpos++;
    }

    if (hasFindStart)
    {
        ppsSize = endpos - pos;
    }
    else
    {
        ppsSize = size - pos;
    }

    return pps;
}