#pragma once
#include <thread>
#include "CommonTools/ExBuff.h"
#include "CommonTools/RtspParser.h"
#include "CommonTools/TimeCounter.h"
#include "ImageTransoprt/ImageTransoprt.h"

class RTSPServerSession
{
public:
    RTSPServerSession(uint32_t fd, std::string strRemoteIP);
    ~RTSPServerSession();

    int32_t StartSession();
    int32_t StopSession();
    inline bool IsSessionFinished() { return m_bSessionFinished; };
    int32_t EnableOSD(bool enable);
    int32_t SetAttitude(float pitch, float roll, float yaw);
    int32_t SetGPS(int32_t lat, int32_t lon, int32_t alt, uint8_t satellites, uint16_t vel);
    int32_t SetSysStatus(uint16_t voltage, int16_t current, int8_t batteryRemaining);

private:
    int32_t ReleaseAll();
    void SessionThread();

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
    int32_t SendRtspRequest(RtspParser::RtspRequest& req);

    int32_t HandleMsg();
    int32_t OnRecvRtspRequest(const RtspParser::RtspRequest& req);
    int32_t OnRecvRtspResponse(const RtspParser::RtspResponse& rsp);
    int32_t OnRecvRtcp(uint8_t* const  msg, const uint32_t size);
    int32_t OnRecvRtp(uint8_t* const  msg, const uint32_t size);

    bool IsRtspRequestMsg(uint8_t* const  msg, const uint32_t size);
    bool IsRtspResponseMsg(uint8_t* const  msg, const uint32_t size);
    bool IsRtcpMsg(uint8_t* const  msg, const uint32_t size);
    bool IsRtpMsg(uint8_t* const  msg, const uint32_t size);

    bool FindRtspMsg(uint8_t* const  msg, const uint32_t size, uint32_t& msgSize);
    bool FindRtcpMsg(uint8_t* const  msg, const uint32_t size, uint32_t& msgSize);
    bool FindRtpMsg(uint8_t* const  msg, const uint32_t size, uint32_t& msgSize);

    int32_t MakeSdp(std::string& sdp, const RtspParser::RtspRequest& req);
    int32_t MakeDeviceSdp(std::string& sdp, const std::string dev, const RtspParser::RtspRequest& req);
    int32_t MakeFileSdp(std::string& sdp, const std::string file);

    int32_t SetupVideo(const RtspParser::RtspRequest& req);
    int32_t SetupAudio(const RtspParser::RtspRequest& req);

    void OnRecvVideoPacket(const std::shared_ptr<Packet>& packet);
    void SendMediaThread();
    int32_t SendVideo(bool& bHasSend);
    int32_t SendAudio(bool& bHasSend);

private:
    typedef  enum TransportType
    {
        TCP = 1,
        UDP
    }TransportType;

private:
    int32_t m_nSessionfd;
    ExBuff m_SessionBuff;
    std::string m_strSessionId;
    uint32_t m_nSeq;
    std::string m_strUrl;

    TimeCounter m_HeartBeatimeoutTimer;

    bool m_bStopSession;
    std::thread* m_pSessionThread;

    std::string m_strResouceType;
    std::string m_strResouce;
    uint32_t m_nVideoWidth;
    uint32_t m_nVideoHight;

    std::string m_strLocalIP;
    std::string m_strRemoteIP;

    int32_t m_nVideoTrackId;
    int32_t m_nAudioTrackId;
    TransportType m_eVideoTransport;
    TransportType m_eAudioTransport;
    int32_t m_nVideoRtpfd;
    int32_t m_nVideoRtcpfd;
    int32_t m_nAudioRtpfd;
    int32_t m_nAudioRtcpfd;

    bool m_bEnableOSD;
    ImageTransoprt* m_pImageTransoprt;

    std::mutex m_VideoRtpPacketListLock;
    std::list <std::shared_ptr<Packet>> m_VideoRtpPacketList;
    bool m_bStopSendMedia;
    std::thread* m_pSendMediaThread;
    bool m_bSessionFinished;
};

static int32_t ConnectUdpSocket(const std::string& ip, uint16_t port);

static int32_t GetLocalIPAndPort(int32_t fd, std::string& ip, uint16_t& port);

static int32_t GetRemoteIPAndPort(int32_t fd, std::string& ip, uint16_t& port);
