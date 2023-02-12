#pragma once
#include <cstdint>
#include <thread>
#include <string>
#include <map>
#include  "CommonTools/SignalObject.h"
#include "CommonTools/TimeCounter.h"
#include "CommonTools/ExBuff.h"
#include "CommonTools/RtspParser.h"
#include "RTPParser/RTPParser.h"
#include "FEC/FECDecoder.h"

extern "C" {
#include "libavcodec/codec.h"
}

class RTSPClient
{
public:
    typedef std::function<void(const VideoInfo&)> VideoReadyCallbaclk;

public:
    RTSPClient();
    ~RTSPClient();
    int32_t PlayUrl(const std::string& ulr);
    int32_t CloseClient();
    int32_t SetVideoPacketCallbaclk(RTPParser::MediaPacketCallbaclk callback);
    int32_t SetVideoReadyCallbaclk(VideoReadyCallbaclk callback);
    int32_t SetAudioPacketCallbaclk(RTPParser::MediaPacketCallbaclk callback);

private:
    int32_t ReleaseAll();
    bool AnalyzeUrl(const std::string& ulr, std::string& ip, uint16_t& port);
    void ClientThread();

    int32_t Options();
    int32_t Describe();
    int32_t Pause();
    int32_t Play();
    int32_t Setup(int32_t trackid);
    int32_t Teardown();

    int32_t HandleMsg();
    int32_t OnRecvRtspRequest(const RtspParser::RtspRequest& req);
    int32_t OnRecvRtspResponse(const  std::shared_ptr<RtspParser::RtspResponse>& rsp);
    int32_t OnRecvRtcp(uint8_t* const  msg, const uint32_t size);
    int32_t OnRecvRtp(uint8_t* const  msg, const uint32_t size);

    bool IsRtspRequestMsg(uint8_t* const  msg, const uint32_t size);
    bool IsRtspResponseMsg(uint8_t* const  msg, const uint32_t size);
    bool IsRtcpMsg(uint8_t* const  msg, const uint32_t size);
    bool IsRtpMsg(uint8_t* const  msg, const uint32_t size);

    bool FindRtspMsg(uint8_t* const  msg, const uint32_t size, uint32_t& msgSize);
    bool FindRtcpMsg(uint8_t* const  msg, const uint32_t size, uint32_t& msgSize);
    bool FindRtpMsg(uint8_t* const  msg, const uint32_t size, uint32_t& msgSize);

    int32_t HandleDescribeRequest(const RtspParser::RtspRequest& req);
    int32_t HandleAnnounceRequest(const RtspParser::RtspRequest& req);
    int32_t HandleGetParameterRequest(const RtspParser::RtspRequest& req);
    int32_t HandleOptionsRequest(const RtspParser::RtspRequest& req);
    int32_t HandlePauseRequest(const RtspParser::RtspRequest& req);
    int32_t HandlePlayRequest(const RtspParser::RtspRequest& req);
    int32_t HandleRecordRequest(const RtspParser::RtspRequest& req);
    int32_t HandleRedirectRequest(const RtspParser::RtspRequest& req);
    int32_t HandleSetupdRequest(const RtspParser::RtspRequest& req);
    int32_t HandleSetParameterRequest(const RtspParser::RtspRequest& req);
    int32_t HandleTeardownRequest(const RtspParser::RtspRequest& req);

    int32_t SendRtspResponse(const RtspParser::RtspResponse& rsp);
    int32_t SendKeepAliveRequest();
    int32_t SendRtspRequest(RtspParser::RtspRequest& req);

    int32_t OnRecvVideo(uint8_t* const  msg, const uint32_t size);
    int32_t OnRecvAudio(uint8_t* const  msg, const uint32_t size);
    int32_t RecvUDPMedia(uint8_t* pRecvBuff, int32_t size);

    void OnRecvFECDecoderPacket(const std::shared_ptr<Packet>& packet);
    void OnRecvNackPacket(const std::shared_ptr<Packet>& packet);

private:
    typedef  enum TransportType
    {
        TCP = 1,
        UDP
    }TransportType;

private:
    int32_t m_nClientSocketfd;
    std::string m_strClientIP;
    uint16_t m_nClientPort;
    std::string m_strServerIP;
    uint16_t m_nServerPort;
    bool m_bCloseClient;
    std::thread* m_pClientThread;
    bool m_bIsPlaying;
    std::string m_strPlayUrl;
    uint32_t m_nSeq;
    std::string m_strSessionId;
    bool m_bIsRecord;
    bool m_bEnableFec;

    ExBuff m_ClientBuff;
    TimeCounter m_HeartBeatCycleTimer;

    std::mutex m_SignalObjectMapLock;
    std::map<int, SignalObject*> m_SignalObjectMap;

    std::mutex m_RtspResponseMapLock;
    std::map<int, std::shared_ptr<RtspParser::RtspResponse>> m_RtspResponseMap;

    bool m_bSetupVideo;
    bool m_bSetupAudio;

    uint8_t m_nVideoPT;
    uint8_t m_nAudioPT;
    AVCodecID m_eVideoFormat;
    AVCodecID m_eAudioFormat;
    int32_t m_nVideoTrackID;
    int32_t m_nAudioTrackID;
    int32_t m_nVideoClockRate;
    int32_t m_nAideoClockRate;
    TransportType m_eVideoTransport;
    TransportType m_eAudioTransport;
    int32_t m_nVideoRtpfd;
    int32_t m_nVideoRtcpfd;
    int32_t m_nAudioRtpfd;
    int32_t m_nAudioRtcpfd;

    RTPParser* m_pVideoParser;
    RFC8627FECDecoder* m_pFECDecoder;
    RTPParser* m_pAudioParser;
    RTPParser::MediaPacketCallbaclk m_pVideoPacketCallbaclk;
    RTPParser::MediaPacketCallbaclk m_pAudioPacketCallbaclk;
    VideoReadyCallbaclk m_pVideoReadyCallbaclk;
};

int32_t AllocUdpMediaSocket(const std::string& ip, uint16_t& port1, uint16_t& port2, int32_t& fd1, int32_t& fd2);

int32_t GetLocalIPAndPort(int32_t fd, std::string& ip, uint16_t& port);

int32_t GetRemoteIPAndPort(int32_t fd, std::string& ip, uint16_t& port);
