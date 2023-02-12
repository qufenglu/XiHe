#pragma once

#include <mutex>
#include "MediaCapture/VideoCapture.h"
#include "MediaDecoder/VideoDecoder.h"
#include "MediaEncoder/VideoEncoder.h"
#include "RTPPacketizer/RTPPacketizer.h"
#include "OSD/OSD.h"
#include "FEC/FECEncoder.h"

class ImageTransoprt
{
public:
    typedef std::function<void(const std::shared_ptr<Packet>&)> RtpPacketCallbaclk;

public:
    ImageTransoprt(bool enableFec);
    ~ImageTransoprt();

    int32_t StartTransoprt(std::string device, const VideoCapture::VideoCaptureCapability& capability);
    int32_t StopTransoprt(std::string device);
    bool SetRtpPacketCallbaclk(ImageTransoprt::RtpPacketCallbaclk callback);
    inline bool IsEnableOSD() { return m_bEnableOSD; };

    int32_t EnableOSD(bool enable);
    int32_t AddMarker(const std::string& name);
    int32_t RemoveMarker(const std::string& name);
    int32_t SetMarkKey(const std::string& name, const std::string& key, const Marker::Color& clolr, int32_t x, int32_t y);  //x,y绝对位置
    int32_t SetMarkValue(const std::string& name, const std::string& value, const Marker::Color& clolr, int32_t x, int32_t y);  //x,y相对于key的位置
    int32_t SetMarkKey(const std::string& name, Bitmap& key, const Marker::Color& clolr, int32_t x, int32_t y);//x,y绝对位置
    int32_t SetMarkValue(const std::string& name, Bitmap& value, const Marker::Color& clolr, int32_t x, int32_t y);//x,y相对于key的位置

private:
    int32_t ReleaseAll();

    void TransoprtThread();
    void DecodeThread();
    void EncoderThread();

    void OnCaptureVideo(std::shared_ptr<VideoFrame>& pVideo);
    void OnRecvDecodedFrame(std::shared_ptr<VideoFrame>& pVido);
    void OnRecvEncodedPacket(std::shared_ptr<VideoPacket>& pVideo);
    void OnRecvRtpPacket(uint8_t* pRtpPacket, uint32_t size);
    void OnRecvFECEncoderPacket(const std::shared_ptr<Packet>& packet);

private:
    OSD m_cOSD;
    bool m_bEnableOSD;
    std::mutex m_pOSDLock;
    VideoCapture* m_pVideoCapture;
    VideoEncoder* m_pVideoEncoder;
    VideoDecoder* m_pVideoDecoder;
    RTPPacketizer* m_pRTPPacketizer;
    RFC8627FECEncoder* m_pFECEncoder;

    bool m_bStopTransoprt;
    std::thread* m_pDecodeThread;
    std::thread* m_pEncoderThread;
    std::thread* m_pTransoprtThread;
    bool m_bEnableFec;

    std::mutex m_CaptureVideoListLock;
    std::list <std::shared_ptr<VideoFrame>> m_CaptureVideoList;

    std::mutex m_DecodedFrameListLock;
    std::list <std::shared_ptr<VideoFrame>> m_DecodedFrameList;

    std::mutex m_EncodedPacketListLock;
    std::list <std::shared_ptr<VideoPacket>> m_EncodedPacketList;

    ImageTransoprt::RtpPacketCallbaclk m_pRtpPacketCallbaclk;
};
