#pragma once
#include "RTSPClient/RTSPClient.h"
#include "MediaDecoder/VideoDecoder.h"
#include "DigitalTransport/DigitalTransport.h"
#include "DigitalTransport/mavlink/ardupilotmega/mavlink.h"

class XIheClient
{
public:
    typedef std::function<void(mavlink_message_t* msg)> DigitalTransportMsgCallback;

public:
    XIheClient(const std::string& ip, const uint16_t port);
    ~XIheClient();
    int32_t PlayDevice(const std::string& device);
    int32_t PlayFile(const std::string& file);
    int32_t SetVideoFrameCallback(VideoDecoder::VideoFrameCallbaclk callback);
    int32_t OpenDigitalTransport();
    int32_t CloseDigitalTransport();
    int32_t InitControllerTransport(const std::string protocol, void* param);
    int32_t InitRemoteTransport(const std::string protocol, void* param);
    int32_t SetControllertMsgCallback(DigitalTransportMsgCallback callback);
    int32_t SetRemoteMsgCallback(DigitalTransportMsgCallback callback);
    int32_t StartTransport();
    inline std::string GetRemoteIp() { return m_strRemoteIp; };

private:
    int32_t ReleaseAll();
    void OnVideoReady(const VideoInfo& info);
    void OnRecvVideoPacket(std::shared_ptr<MediaPacket>& video);
    void OnRecvVideoFrame(std::shared_ptr<VideoFrame>& video);
    void OnRecvMsgFromeController(std::shared_ptr<Packet> packet);
    void OnRecvMsgFromeRemote(std::shared_ptr<Packet> packet);
    void ProcessMsg(const std::shared_ptr<Packet>& paclet, const DigitalTransportMsgCallback& callback);

private:
    std::string m_strRemoteIp;
    uint16_t m_nRemotePort;
    RTSPClient* m_pRTSPClient;
    std::mutex m_pVideoDecoderLock;
    VideoDecoder* m_pVideoDecoder;
    VideoDecoder::VideoFrameCallbaclk m_pVideoFrameCallback;
    DigitalTransport* m_pDigitalTransport;
    DigitalTransportMsgCallback m_pControllertMsgCallback;
    DigitalTransportMsgCallback m_pRemoteMsgCallback;
};