#include <linux/videodev2.h>
#include "ImageTransoprt.h"
#include "Log/Log.h"
#include "RTPPacketizer/H264RTPpacketizer.h"

#define MAX_CAPTURE_VIDEO_NUM (1)

ImageTransoprt::ImageTransoprt()
{
    m_bEnableOSD = false;
    m_pVideoCapture = nullptr;
    m_pVideoEncoder = nullptr;
    m_pVideoDecoder = nullptr;
    m_pRTPPacketizer = nullptr;
    m_bStopTransoprt = true;

    m_pTransoprtThread = nullptr;
    m_pDecodeThread = nullptr;
    m_pEncoderThread = nullptr;

    m_pRtpPacketCallbaclk = nullptr;
}

ImageTransoprt::~ImageTransoprt()
{
    ReleaseAll();
}

int32_t ImageTransoprt::ReleaseAll()
{
    m_bStopTransoprt = true;
    if (m_pTransoprtThread != nullptr)
    {
        if (m_pTransoprtThread->joinable())
        {
            m_pTransoprtThread->join();
        }
        delete m_pTransoprtThread;
        m_pTransoprtThread = nullptr;
    }

    if (m_pDecodeThread != nullptr)
    {
        if (m_pDecodeThread->joinable())
        {
            m_pDecodeThread->join();
        }
        delete m_pDecodeThread;
        m_pDecodeThread = nullptr;
    }

    if (m_pEncoderThread != nullptr)
    {
        if (m_pEncoderThread->joinable())
        {
            m_pEncoderThread->join();
        }
        delete m_pEncoderThread;
        m_pEncoderThread = nullptr;
    }

    delete m_pVideoCapture;
    m_pVideoCapture = nullptr;
    delete m_pVideoEncoder;
    m_pVideoEncoder = nullptr;
    delete m_pVideoDecoder;
    m_pVideoDecoder = nullptr;
    delete m_pRTPPacketizer;
    m_pRTPPacketizer = nullptr;

    {
        std::lock_guard<std::mutex> lock(m_CaptureVideoListLock);
        for (auto& pVideo : m_CaptureVideoList)
        {
            pVideo = nullptr;
        }
        m_CaptureVideoList.clear();
    }

    {
        std::lock_guard<std::mutex> lock(m_DecodedFrameListLock);
        for (auto& pVideo : m_DecodedFrameList)
        {
            pVideo = nullptr;
        }
        m_DecodedFrameList.clear();
    }

    {
        std::lock_guard<std::mutex> lock(m_EncodedPacketListLock);
        for (auto& pVideo : m_EncodedPacketList)
        {
            pVideo = nullptr;
        }
        m_EncodedPacketList.clear();
    }

    m_pRtpPacketCallbaclk = nullptr;
    m_bEnableOSD = false;

    return 0;
}

int32_t ImageTransoprt::StartTransoprt(std::string device, const VideoCapture::VideoCaptureCapability& capability)
{
    Trace("[%p][ImageTransoprt::StartTransoprt] StartTransoprt", this);

    if (m_pTransoprtThread != nullptr)
    {
        Error("[%p][ImageTransoprt::StartTransoprt] Transmission is already in progress, please stop the previous transmission first", this);
        return -1;
    }

    ReleaseAll();

    m_pVideoCapture = new VideoCapture();
    VideoCapture::CaptureVideoCallbaclk pCaptureVideoCallbaclk = std::bind(&ImageTransoprt::OnCaptureVideo, this, std::placeholders::_1);
    m_pVideoCapture->SetCaptureVideoCallbaclk(pCaptureVideoCallbaclk);

    if (capability.m_nVideoType != V4L2_PIX_FMT_YUV420)
    {
        m_pVideoDecoder = new VideoDecoder();

        VideoInfo info;
        info.m_nCodecID = AV_CODEC_ID_MJPEG;
        info.m_nWidth = capability.m_nWidth;
        info.m_nHight = capability.m_nHeight;

        int32_t ret = m_pVideoDecoder->AddVideoStream(info);
        if (ret < 0)
        {
            Error("[%p][ImageTransoprt::StartTransoprt] AddVideoStream fail,return:%d", this, ret);
            ReleaseAll();
            return -2;
        }

        VideoDecoder::VideoFrameCallbaclk pVideoDecoderFrameCallbaclk = std::bind(&ImageTransoprt::OnRecvDecodedFrame, this, std::placeholders::_1);
        m_pVideoDecoder->SetVideoFrameCallBack(pVideoDecoderFrameCallbaclk);
    }

    m_pVideoEncoder = new VideoEncoder();
    VideoEncoder::VideoPacketCallbaclk pVideoPacketCallbaclk = std::bind(&ImageTransoprt::OnRecvEncodedPacket, this, std::placeholders::_1);
    m_pVideoEncoder->SetVideoPacketCallback(pVideoPacketCallbaclk);


    VideoEncoder::EncodParam encodParam;
    encodParam.m_nBitRate = 2 * 1024 * 1024;
    encodParam.m_nHeight = capability.m_nHeight;
    encodParam.m_nWidth = capability.m_nWidth;

    int32_t ret = m_pVideoEncoder->OpenEncoder(encodParam);
    if (ret < 0)
    {
        Error("[%p][ImageTransoprt::StartTransoprt] OpenEncoder fail,return:%d", this, ret);
        ReleaseAll();
        return -3;
    }

    m_pRTPPacketizer = new H264RTPpacketizer();
    ret = ((H264RTPpacketizer*)m_pRTPPacketizer)->Init();
    if (ret < 0)
    {
        Error("[%p][ImageTransoprt::StartTransoprt] init H264RTPpacketizer fail,return:%d", this, ret);
        ReleaseAll();
        return -4;
    }
    m_pRTPPacketizer->SetPaylodaType(96);
    m_pRTPPacketizer->SetSSRC(0x12345678);
    RTPPacketizer::RtpPacketCallbaclk pRtpPacketCallbaclk = std::bind(&ImageTransoprt::OnRecvRtpPacket, this, std::placeholders::_1, std::placeholders::_2);
    m_pRTPPacketizer->SetRtpPacketCallbaclk(pRtpPacketCallbaclk);

    {
        const uint8_t* data = nullptr;
        uint32_t size = 0;

        data = m_pVideoEncoder->GetSPS(size);
        if (data != nullptr && size > 0)
        {
            ((H264RTPpacketizer*)m_pRTPPacketizer)->SetSPS(data, size);
        }

        data = m_pVideoEncoder->GetPPS(size);
        if (data != nullptr && size > 0)
        {
            ((H264RTPpacketizer*)m_pRTPPacketizer)->SetPPS(data, size);
        }
    }

    m_bStopTransoprt = false;
    ret = m_pVideoCapture->StartCapture(device, capability);
    {
        if (ret < 0)
        {
            Error("[%p][ImageTransoprt::StartTransoprt] StartCapture video fail,return:%d", this, ret);
            ReleaseAll();
            return -4;
        }
    }

    m_pTransoprtThread = new std::thread(&ImageTransoprt::TransoprtThread, this);
    m_pEncoderThread = new std::thread(&ImageTransoprt::EncoderThread, this);
    m_pDecodeThread = new std::thread(&ImageTransoprt::DecodeThread, this);

    return 0;
}

void ImageTransoprt::OnCaptureVideo(std::shared_ptr<VideoFrame>& pVideo)
{
    std::lock_guard<std::mutex> lock(m_CaptureVideoListLock);
    m_CaptureVideoList.push_back(pVideo);

    while (m_CaptureVideoList.size() > MAX_CAPTURE_VIDEO_NUM)
    {
        auto pVideoFrame = m_CaptureVideoList.front();
        m_CaptureVideoList.pop_front();
        Warn("[%p][ImageTransoprt::OnCaptureVideo] Capture Video List  size > %d,discard", this, MAX_CAPTURE_VIDEO_NUM);
    }
}

void ImageTransoprt::OnRecvDecodedFrame(std::shared_ptr<VideoFrame>& pVideo)
{
    std::lock_guard<std::mutex> lock(m_DecodedFrameListLock);
    while (m_DecodedFrameList.size() > MAX_CAPTURE_VIDEO_NUM)
    {
        auto pVideoFrame = m_DecodedFrameList.front();
        m_DecodedFrameList.pop_front();
        Warn("[%p][ImageTransoprt::OnRecvDecodedFrame] Decoded Frame List size > %d,discard", this, MAX_CAPTURE_VIDEO_NUM);
    }

    {
        std::lock_guard<std::mutex> lock(m_pOSDLock);
        if (m_bEnableOSD)
        {
            m_cOSD.AddOSD2VideoFrame(pVideo);
        }
    }
    m_DecodedFrameList.push_back(pVideo);
}

void ImageTransoprt::OnRecvEncodedPacket(std::shared_ptr<VideoPacket>& pVideo)
{
    std::lock_guard<std::mutex> lock(m_EncodedPacketListLock);
    m_EncodedPacketList.push_back(pVideo);

    while (m_EncodedPacketList.size() > MAX_CAPTURE_VIDEO_NUM)
    {
        auto pVideoPacket = m_EncodedPacketList.front();
        m_EncodedPacketList.pop_front();
        Warn("[%p][ImageTransoprt::OnRecvEncodedPacket] Encoded Packet List  size > %d,discard", this, MAX_CAPTURE_VIDEO_NUM);
    }
}

bool ImageTransoprt::SetRtpPacketCallbaclk(ImageTransoprt::RtpPacketCallbaclk callback)
{
    m_pRtpPacketCallbaclk = callback;
    return true;
}

void ImageTransoprt::OnRecvRtpPacket(uint8_t* pRtpPacket, uint32_t size)
{
    if (m_pRtpPacketCallbaclk == nullptr)
    {
        return;
    }

    uint8_t* data = (uint8_t*)malloc(size);
    if (data == nullptr)
    {
        Error("[%p][ImageTransoprt::OnRecvRtpPacket] malloc data fail", this);
        return;
    }
    memcpy(data, pRtpPacket, size);

    std::shared_ptr<Packet> pRTPPacke = std::make_shared<Packet>();
    pRTPPacke->m_pData = data;
    pRTPPacke->m_nLength = size;
    m_pRtpPacketCallbaclk(pRTPPacke);

    pRTPPacke = nullptr;
}

int32_t ImageTransoprt::StopTransoprt(std::string device)
{
    Trace("[%p][ImageTransoprt::StopTransoprt] StopTransoprt", this);
    if (m_pVideoCapture != nullptr)
    {
        m_pVideoCapture->StopCapture();
    }
    ReleaseAll();

    return 0;
}

void ImageTransoprt::DecodeThread()
{
    Trace("[%p][ImageTransoprt::DecodeThread] start DecodeThread", this);
    while (!m_bStopTransoprt)
    {
        std::shared_ptr<VideoFrame> pCaptureVideo = nullptr;

        {
            std::lock_guard<std::mutex> lock(m_CaptureVideoListLock);
            if (m_CaptureVideoList.size() > 0)
            {
                pCaptureVideo = m_CaptureVideoList.front();
                m_CaptureVideoList.pop_front();
            }
        }

        if (pCaptureVideo == nullptr)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        if (pCaptureVideo->m_nFrameType != V4L2_PIX_FMT_YUV420)
        {
            std::shared_ptr<VideoPacket> pVideoPacket = std::make_shared<VideoPacket>();
            pVideoPacket->m_lDTS = pCaptureVideo->m_lPTS;
            pVideoPacket->m_lPTS = pCaptureVideo->m_lPTS;
            pVideoPacket->m_nFrameType = pCaptureVideo->m_nFrameType;
            pVideoPacket->m_nLength = pCaptureVideo->m_nLength;
            pVideoPacket->m_pData = pCaptureVideo->m_pData;
            pCaptureVideo->m_nLength = 0;
            pCaptureVideo->m_pData = nullptr;
            m_pVideoDecoder->RecvVideoPacket(pVideoPacket);
        }
        else
        {
            OnRecvDecodedFrame(pCaptureVideo);
        }

        pCaptureVideo = nullptr;
    }

    Trace("[%p][ImageTransoprt::DecodeThread] exit DecodeThread", this);
}

void ImageTransoprt::EncoderThread()
{
    Trace("[%p][ImageTransoprt::EncoderThread] start EncoderThread", this);
    while (!m_bStopTransoprt)
    {
        std::shared_ptr<VideoFrame> m_DecodedFrame = nullptr;

        {
            std::lock_guard<std::mutex> lock(m_DecodedFrameListLock);
            if (m_DecodedFrameList.size() > 0)
            {
                m_DecodedFrame = m_DecodedFrameList.front();
                m_DecodedFrameList.pop_front();
            }
        }

        if (m_DecodedFrame == nullptr)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        m_pVideoEncoder->EncodeFrame(m_DecodedFrame);

        m_DecodedFrame = nullptr;
    }

    Trace("[%p][ImageTransoprt::EncoderThread] exit EncoderThread", this);
}

void ImageTransoprt::TransoprtThread()
{
    Trace("[%p][ImageTransoprt::TransoprtThread] start TransoprtThread", this);
    while (!m_bStopTransoprt)
    {
        std::shared_ptr<VideoPacket> pEncodedPacket = nullptr;

        {
            std::lock_guard<std::mutex> lock(m_EncodedPacketListLock);
            if (m_EncodedPacketList.size() > 0)
            {
                pEncodedPacket = m_EncodedPacketList.front();
                m_EncodedPacketList.pop_front();
            }
        }

        if (pEncodedPacket == nullptr)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        m_pRTPPacketizer->RecvPacket(pEncodedPacket);
        pEncodedPacket = nullptr;
    }

    Trace("[%p][ImageTransoprt::TransoprtThread] exit TransoprtThread", this);
}

int32_t ImageTransoprt::EnableOSD(bool enable)
{
    m_bEnableOSD = enable;
    return 0;
}

int32_t ImageTransoprt::AddMarker(const std::string& name)
{
    std::lock_guard<std::mutex> lock(m_pOSDLock);
    int32_t ret = m_cOSD.AddMarker(name);
    if (ret != 0)
    {
        Error("[%p][ImageTransoprt::AddMarker] AddMarker fali,return:%d", this, ret);
        return -1;
    }

    return 0;
}

int32_t ImageTransoprt::RemoveMarker(const std::string& name)
{
    std::lock_guard<std::mutex> lock(m_pOSDLock);
    int32_t ret = m_cOSD.RemoveMarker(name);
    if (ret != 0)
    {
        Error("[%p][ImageTransoprt::RemoveMarker] RemoveMarker fali,return:%d", this, ret);
        return -1;
    }

    return 0;
}

int32_t ImageTransoprt::SetMarkKey(const std::string& name, const std::string& key, const Marker::Color& clolr, int32_t x, int32_t y)
{
    std::lock_guard<std::mutex> lock(m_pOSDLock);
    int32_t ret = m_cOSD.SetKey(name, key, clolr, x, y);
    if (ret != 0)
    {
        Error("[%p][ImageTransoprt::SetMarkKey] SetKey fali,return:%d", this, ret);
        return -1;
    }

    return 0;
}

int32_t ImageTransoprt::SetMarkValue(const std::string& name, const std::string& value, const Marker::Color& clolr, int32_t x, int32_t y)
{
    std::lock_guard<std::mutex> lock(m_pOSDLock);
    int32_t ret = m_cOSD.SetValue(name, value, clolr, x, y);
    if (ret != 0)
    {
        Error("[%p][ImageTransoprt::SetMarkValue] SetValue fali,return:%d", this, ret);
        return -1;
    }

    return 0;
}

int32_t ImageTransoprt::SetMarkKey(const std::string& name, Bitmap& key, const Marker::Color& clolr, int32_t x, int32_t y)
{
    std::lock_guard<std::mutex> lock(m_pOSDLock);
    int32_t ret = m_cOSD.SetKey(name, key, clolr, x, y);
    if (ret != 0)
    {
        Error("[%p][ImageTransoprt::SetMarkKey] SetKey fali,return:%d", this, ret);
        return -1;
    }

    return 0;
}

int32_t ImageTransoprt::SetMarkValue(const std::string& name, Bitmap& value, const Marker::Color& clolr, int32_t x, int32_t y)
{
    std::lock_guard<std::mutex> lock(m_pOSDLock);
    int32_t ret = m_cOSD.SetValue(name, value, clolr, x, y);
    if (ret != 0)
    {
        Error("[%p][ImageTransoprt::SetMarkValue] SetValue fali,return:%d", this, ret);
        return -1;
    }

    return 0;
}