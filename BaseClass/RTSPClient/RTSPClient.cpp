#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include "RTSPClient.h"
#include "Log/Log.h"
#include "CommonTools/RtspParser.h"
#include "CommonTools/SdpParser.h"
#include "RTPParser/H264RTPParser.h"
#include "RTPParser/MJPEGRTPParser.h"

#define RECV_BUFF_SIZE (1024*4)
#define HEART_BEAT_CYCLE (15*1000)
#define HEART_BEAT_TIMEOUT (60*1000)
#define RECV_TIMEOUT 10*1000

RTSPClient::RTSPClient()
{
    m_nClientSocketfd = -1;
    m_nClientPort = 0;
    m_nServerPort = 0;
    m_bCloseClient = true;
    m_pClientThread = nullptr;
    m_bIsPlaying = false;
    m_strPlayUrl = "";
    m_strPlayUrlNoExParam = "";
    m_nSeq = 0;
    m_bIsRecord = false;
    m_bEnableFec = false;

    m_bSetupVideo = false;
    m_bSetupAudio = false;
    m_nVideoPT = 96;
    m_nAudioPT = 0;
    m_nVideoTrackID = -1;
    m_nAudioTrackID = -1;
    m_eVideoFormat = AV_CODEC_ID_NONE;
    m_eAudioFormat = AV_CODEC_ID_NONE;
    m_eVideoTransport = TransportType::UDP;
    m_eAudioTransport = TransportType::UDP;
    m_nVideoRtpfd = -1;
    m_nVideoRtcpfd = -1;
    m_nAudioRtpfd = -1;
    m_nAudioRtcpfd = -1;

    m_pVideoParser = nullptr;
    m_pFECDecoder = nullptr;
    m_pAudioParser = nullptr;
    m_pVideoPacketCallbaclk = nullptr;
    m_pAudioPacketCallbaclk = nullptr;
    m_pVideoReadyCallbaclk = nullptr;
}

RTSPClient::~RTSPClient()
{
    ReleaseAll();
}

int32_t RTSPClient::ReleaseAll()
{
    if (m_bIsPlaying)
    {
        Teardown();
        m_bIsPlaying = false;
    }

    m_bCloseClient = true;
    if (m_pClientThread != nullptr)
    {
        if (m_pClientThread->joinable())
        {
            m_pClientThread->join();
        }
        delete m_pClientThread;
        m_pClientThread = nullptr;
    }

    if (m_nClientSocketfd != -1)
    {
        close(m_nClientSocketfd);
        m_nClientSocketfd = -1;
    }
    if (m_nVideoRtpfd != -1)
    {
        close(m_nVideoRtpfd);
        m_nVideoRtpfd = -1;
    }
    if (m_nVideoRtcpfd != -1)
    {
        close(m_nVideoRtcpfd);
        m_nVideoRtcpfd = -1;
    }
    if (m_nAudioRtpfd != -1)
    {
        close(m_nAudioRtpfd);
        m_nAudioRtpfd = -1;
    }
    if (m_nAudioRtcpfd != -1)
    {
        close(m_nAudioRtcpfd);
        m_nAudioRtcpfd = -1;
    }

    m_strClientIP = "";
    m_nClientPort = 0;
    m_strServerIP = "";
    m_nServerPort = 0;

    m_eVideoFormat = AV_CODEC_ID_NONE;
    m_eAudioFormat = AV_CODEC_ID_NONE;
    m_bSetupVideo = false;
    m_bSetupAudio = false;
    m_nVideoPT = 96;
    m_nAudioPT = 0;
    m_nVideoTrackID = -1;
    m_nAudioTrackID = -1;
    m_eVideoTransport = TransportType::UDP;
    m_eAudioTransport = TransportType::UDP;

    m_strPlayUrl = "";
    m_strPlayUrlNoExParam = "";
    m_ClientBuff.ClearBuff(0);
    {
        std::lock_guard<std::mutex> lock(m_SignalObjectMapLock);
        m_SignalObjectMap.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_RtspResponseMapLock);
        m_RtspResponseMap.clear();
    }
    m_nSeq = 0;
    m_strSessionId = "";

    delete m_pAudioParser;
    m_pAudioParser = nullptr;
    delete m_pVideoParser;
    m_pVideoParser = nullptr;
    delete m_pFECDecoder;
    m_pFECDecoder = nullptr;

    return 0;
}

int32_t RTSPClient::CloseClient()
{
    return ReleaseAll();
}

int32_t RTSPClient::PlayUrl(const std::string& url, TransportType t)
{
    Trace("[%p][RTSPClient::PlayUrl] play url:%s TransportType:%d", this, url.c_str(), t);
    CloseClient();

    std::string ip;
    uint16_t port;
    int32_t ret = 0;
    int32_t ret1 = 0;
    bool bSetupSucess = false;
    struct sockaddr_in addr;
    int flags = 0;
    int len = 0;
    struct timeval timeout;
    std::vector<std::string> temp;
    m_eAudioTransport = t;
    m_eVideoTransport = t;

    if (!AnalyzeUrl(url, ip, port))
    {
        Error("[%p][RTSPClient::PlayUrl] can not get ip and port from url:%s ", this, url.c_str());
        ret = -1;
        goto fail;
    }
    m_nClientSocketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_nClientSocketfd == -1)
    {
        Error("[%p][RTSPClient::PlayUrl] open socket fail,errno:%d", this, errno);
        ret = -2;
        goto fail;
    }

    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    ret = setsockopt(m_nClientSocketfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(struct timeval));

    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    len = sizeof(addr);
    if (connect(m_nClientSocketfd, (sockaddr*)&addr, len) < 0)
    {
        int t = errno;
        Error("[%p][RTSPClient::PlayUrl] connect socket fail,errno:%d", this, t);
        ret = -3;
        goto fail;
    }

    flags = fcntl(m_nClientSocketfd, F_GETFL, 0);
    if (fcntl(m_nClientSocketfd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        Error("[%p][RTSPClient::PlayUrl] set NONBLOCK fail,errno:%d", this, errno);
        ret = -3;
        goto fail;
    }

    m_strPlayUrl = url;
    split(m_strPlayUrl, temp, "?");
    m_strPlayUrlNoExParam = temp[0];

    GetLocalIPAndPort(m_nClientSocketfd, m_strClientIP, m_nClientPort);
    GetRemoteIPAndPort(m_nClientSocketfd, m_strServerIP, m_nServerPort);

    m_bCloseClient = false;
    m_pClientThread = new std::thread(&RTSPClient::ClientThread, this);

    ret1 = Options();
    if (ret1 != 0)
    {
        Error("[%p][RTSPClient::PlayUrl] Options fail,return:%d", this, ret1);
        ret = -5;
        goto fail;
    }
    ret1 = Describe();
    if (ret1 != 0)
    {
        Error("[%p][RTSPClient::PlayUrl] Describe fail,return:%d", this, ret1);
        ret = -5;
        goto fail;
    }
    if (m_eVideoFormat != AV_CODEC_ID_NONE)
    {
        ret1 = Setup(m_nVideoTrackID);
        if (ret1 != 0)
        {
            Error("[%p][RTSPClient::PlayUrl] Setup track:%d fail,return:%d", this, m_nVideoTrackID, ret1);
        }
        else
        {
            bSetupSucess = true;
        }
    }
    if (m_eAudioFormat != AV_CODEC_ID_NONE)
    {
        ret1 = Setup(m_nAudioTrackID);
        if (ret1 != 0)
        {
            Error("[%p][RTSPClient::PlayUrl] Setup track:%d fail,return:%d", this, m_nAudioTrackID, ret1);
        }
        else
        {
            bSetupSucess = true;
        }
    }
    if (!bSetupSucess)
    {
        Error("[%p][RTSPClient::PlayUrl] Setup fail", this);
        ret = -6;
        goto fail;
    }
    ret1 = Play();
    if (ret1 != 0)
    {
        Error("[%p][RTSPClient::PlayUrl] Play fail,return:%d", this, ret1);
        ret = -7;
        goto fail;
    }

    return 0;
fail:
    CloseClient();
    return ret;
}

bool RTSPClient::AnalyzeUrl(const std::string& url, std::string& ip, uint16_t& port)
{
    ip = "";
    port = 554;
    std::string strTempUrl = url;
    if (strTempUrl.find("rtsp://") != 0)
    {
        Error("[%p][RTSPClient::AnalyzeUrl] analyze url:%s fali,not find rtsp://", this, url.c_str());
        return false;
    }
    strTempUrl = strTempUrl.substr(strlen("rtsp://"));
    size_t pos = strTempUrl.find("/");
    if (pos != std::string::npos)
    {
        strTempUrl = strTempUrl.substr(0, pos);
    }
    pos = strTempUrl.find(":");
    if (pos != std::string::npos)
    {
        port = atoi(strTempUrl.substr(pos + 1).c_str());
    }
    ip = strTempUrl.substr(0, pos);

    return true;
}

void RTSPClient::ClientThread()
{
    Trace("[%p][RTSPClient::ClientThread] start ClientThread", this);
    uint8_t* pRecvBuff = (uint8_t*)malloc(RECV_BUFF_SIZE);
    if (pRecvBuff == nullptr)
    {
        Error("[%p][RTSPClient::ClientThread] malloc recv buff fail", this);
        return;
    }

    bool m_bNeedWait;
    m_HeartBeatCycleTimer.MakeTimePoint();

    while (!m_bCloseClient)
    {
        m_bNeedWait = false;
        ssize_t len = recv(m_nClientSocketfd, pRecvBuff, RECV_BUFF_SIZE, 0);
        if (len == -1)
        {
            int err = errno;
            if (err != EAGAIN)
            {
                Error("[%p][RTSPClient::ClientThread]  recv error:%d", this, err);
                break;
            }
            m_bNeedWait = true;
        }
        else
        {
            m_ClientBuff.Append(pRecvBuff, len);
            HandleMsg();
        }

        if (m_HeartBeatCycleTimer.GetDuration() > HEART_BEAT_CYCLE)
        {
            SendKeepAliveRequest();
            m_HeartBeatCycleTimer.MakeTimePoint();
        }

        if (RecvUDPMedia(pRecvBuff, RECV_BUFF_SIZE) > 0)
        {
            m_bNeedWait = false;
        }

        if (m_bNeedWait)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    free(pRecvBuff);
    Trace("[%p][RTSPClient::ClientThread] exit ClientThread", this);

    std::thread ReleaseThread([&]() {
        Teardown();
        CloseClient();
        });
    ReleaseThread.detach();
}

bool RTSPClient::IsRtspRequestMsg(uint8_t* const  msg, const uint32_t size)
{
    bool ret = strcmp((char*)msg, "OPTIONS ") == 0 ? true :
        strcmp((char*)msg, "DESCRIBE ") == 0 ? true :
        strcmp((char*)msg, "PAUSE ") == 0 ? true :
        strcmp((char*)msg, "PLAY ") == 0 ? true :
        strcmp((char*)msg, "SETUP ") == 0 ? true :
        strcmp((char*)msg, "RECORD ") == 0 ? true :
        strcmp((char*)msg, "GET_PARAMETER ") == 0 ? true :
        strcmp((char*)msg, "SET_PARAMETER ") == 0 ? true :
        strcmp((char*)msg, "TEARDOWN ") == 0 ? true :
        strcmp((char*)msg, "ANNOUNCE ") == 0 ? true :
        strcmp((char*)msg, "REDIRECT ") == 0 ? true : false;

    return ret;
}

bool RTSPClient::IsRtspResponseMsg(uint8_t* const  msg, const uint32_t size)
{
    bool ret = msg[0] != 'R' ? false :
        msg[1] != 'T' ? false :
        msg[2] != 'S' ? false :
        msg[3] != 'P' ? false :
        msg[4] != '/' ? false :
        msg[5] != '1' ? false :
        msg[6] != '.' ? false :
        msg[7] != '0' ? false :
        msg[8] != ' ' ? false : true;

    return ret;
}

bool RTSPClient::IsRtcpMsg(uint8_t* const  msg, const uint32_t size)
{
    return (msg[0] == 0x24 && (msg[1] % 2 == 1));
}

bool RTSPClient::IsRtpMsg(uint8_t* const  msg, const uint32_t size)
{
    return (msg[0] == 0x24 && (msg[1] % 2 == 0));
}

bool RTSPClient::FindRtspMsg(uint8_t* const  msg, const uint32_t size, uint32_t& msgSize)
{
    msgSize = 0;
    bool bIsMsgComplete = false;

    uint32_t len = 0;
    for (uint32_t i = 0; i <= size - 4; i++)
    {
        if (msg[i] == '\r' && msg[i + 1] == '\n' && msg[i + 2] == '\r' && msg[i + 3] == '\n')
        {
            bIsMsgComplete = true;
            len += 4;
            break;
        }
        len++;
    }

    if (bIsMsgComplete)
    {
        msgSize = len;
    }

    return bIsMsgComplete;
}

bool RTSPClient::FindRtcpMsg(uint8_t* const  msg, const uint32_t size, uint32_t& msgSize)
{
    msgSize = 0;
    bool bIsMsgComplete = false;

    uint16_t len = ((msg[2] << 8) | msg[3]);
    if (len + 4 <= size)
    {
        msgSize = len + 4;
        bIsMsgComplete = true;
    }

    return bIsMsgComplete;
}

bool RTSPClient::FindRtpMsg(uint8_t* const  msg, const uint32_t size, uint32_t& msgSize)
{
    msgSize = 0;
    bool bIsMsgComplete = false;

    uint16_t len = ((msg[2] << 8) | msg[3]);
    if (len + 4 <= size)
    {
        msgSize = len + 4;
        bIsMsgComplete = true;
    }

    return bIsMsgComplete;
}

int32_t RTSPClient::HandleMsg()
{
    uint8_t* pRawData;
    uint32_t nDataSize;
    bool handed = false;

    while (true)
    {
        handed = false;
        m_ClientBuff.GetRawData(pRawData, nDataSize);
        if (nDataSize > 4)
        {
            if (IsRtpMsg(pRawData, nDataSize))
            {
                handed = true;
                uint32_t nMsgSize;
                if (FindRtpMsg(pRawData, nDataSize, nMsgSize))
                {
                    m_ClientBuff.Read(pRawData, nMsgSize);
                    OnRecvRtp(pRawData, nMsgSize);
                }
                else
                {
                    break;
                }
            }
            else if (IsRtcpMsg(pRawData, nDataSize))
            {
                handed = true;
                uint32_t nMsgSize;
                if (FindRtcpMsg(pRawData, nDataSize, nMsgSize))
                {
                    m_ClientBuff.Read(pRawData, nMsgSize);
                    OnRecvRtcp(pRawData, nMsgSize);
                }
                else
                {
                    break;
                }
            }

            if (handed)
            {
                continue;
            }
            //"RTSP 200 OK CSeq: X" "XXXX rtsp:// RTSP/1.0 CSeq: X"
            if (nDataSize < 19)
            {
                break;
            }

            if (IsRtspResponseMsg(pRawData, nDataSize))
            {
                uint32_t nMsgSize;
                if (FindRtspMsg(pRawData, nDataSize, nMsgSize))
                {
                    std::shared_ptr<RtspParser::RtspResponse> rsp = std::make_shared<RtspParser::RtspResponse>();
                    std::string strMsg = std::string((char*)pRawData, nMsgSize);
                    uint32_t ret = RtspParser::ParseRtspResponse(strMsg, *rsp.get());
                    if (ret == 0)
                    {
                        if (rsp->m_FieldsMap.find("Content-length") != rsp->m_FieldsMap.end())
                        {
                            int nContentLength = atoi(rsp->m_FieldsMap.at("Content-length").c_str());
                            if (nMsgSize + nContentLength <= nDataSize)
                            {
                                m_ClientBuff.Read(pRawData, nMsgSize);
                                m_ClientBuff.Read(pRawData, nContentLength);
                                rsp->m_StrContent = std::string((char*)pRawData, nContentLength);
                                Trace("[%p][RTSPClient::HandleMsg] Recv rtsp response:%s", this, strMsg.c_str());
                                OnRecvRtspResponse(rsp);
                            }
                            else
                            {
                                break;
                            }
                        }
                        else
                        {
                            m_ClientBuff.Read(pRawData, nMsgSize);
                            Trace("[%p][RTSPClient::HandleMsg] Recv rtsp response:%s", this, strMsg.c_str());
                            OnRecvRtspResponse(rsp);
                        }
                    }
                    else
                    {
                        m_ClientBuff.Read(pRawData, nMsgSize);
                        Error("[%p][RTSPClient::HandleMsg] ParseRtspResponse fail,ret:%d msg:%s", this, ret, strMsg.c_str());
                    }
                }
                else
                {
                    break;
                }
            }
            else if (IsRtspRequestMsg(pRawData, nDataSize))
            {
                uint32_t nMsgSize;
                if (FindRtspMsg(pRawData, nDataSize, nMsgSize))
                {
                    RtspParser::RtspRequest req;
                    std::string strMsg = std::string((char*)pRawData, nMsgSize);
                    uint32_t ret = RtspParser::ParseRtspRequest(strMsg, req);
                    if (ret == 0)
                    {
                        if (req.m_FieldsMap.find("Content-length") != req.m_FieldsMap.end())
                        {
                            int nContentLength = atoi(req.m_FieldsMap.at("Content-length").c_str());
                            if (nMsgSize + nContentLength <= nDataSize)
                            {
                                m_ClientBuff.Read(pRawData, nMsgSize);
                                m_ClientBuff.Read(pRawData, nContentLength);
                                req.m_StrContent = std::string((char*)pRawData, nContentLength);
                                Trace("[%p][RTSPClient::HandleMsg] Recv rtsp request:%s", this, strMsg.c_str());
                                OnRecvRtspRequest(req);
                            }
                            else
                            {
                                break;
                            }
                        }
                        else
                        {
                            m_ClientBuff.Read(pRawData, nMsgSize);
                            Trace("[%p][RTSPClient::HandleMsg] Recv rtsp request:%s", this, strMsg.c_str());
                            OnRecvRtspRequest(req);
                        }
                    }
                    else
                    {
                        m_ClientBuff.Read(pRawData, nMsgSize);
                        Error("[%p][RTSPClient::HandleMsg] ParseRtspRequest fail,ret:%d msg:%s", this, ret, strMsg.c_str());
                    }
                }
                else
                {
                    break;
                }
            }
            else
            {
                Error("[%p][RTSPClient::HandleMsg] recv err msg:%s", this, pRawData);
                m_ClientBuff.ClearBuff();
            }
        }
        else
        {
            break;
        }
    }

    return 0;
}

int32_t RTSPClient::OnRecvRtspRequest(const RtspParser::RtspRequest& req)
{
    int32_t ret = req.m_RtspMethod == RtspParser::RTSP_METHOD_DESCRIBE ? HandleDescribeRequest(req) :
        req.m_RtspMethod == RtspParser::RTSP_METHOD_ANNOUNCE ? HandleAnnounceRequest(req) :
        req.m_RtspMethod == RtspParser::RTSP_METHOD_GET_PARAMETER ? HandleGetParameterRequest(req) :
        req.m_RtspMethod == RtspParser::RTSP_METHOD_OPTIONS ? HandleOptionsRequest(req) :
        req.m_RtspMethod == RtspParser::RTSP_METHOD_PAUSE ? HandlePauseRequest(req) :
        req.m_RtspMethod == RtspParser::RTSP_METHOD_PLAY ? HandlePlayRequest(req) :
        req.m_RtspMethod == RtspParser::RTSP_METHOD_RECORD ? HandleRecordRequest(req) :
        req.m_RtspMethod == RtspParser::RTSP_METHOD_REDIRECT ? HandleRedirectRequest(req) :
        req.m_RtspMethod == RtspParser::RTSP_METHOD_SETUP ? HandleSetupdRequest(req) :
        req.m_RtspMethod == RtspParser::RTSP_METHOD_SET_PARAMETER ? HandleSetParameterRequest(req) :
        req.m_RtspMethod == RtspParser::RTSP_METHOD_TEARDOWN ? HandleTeardownRequest(req) : -999;

    if (ret != 0)
    {
        Error("[%p][RTSPClient::OnRecvRtspRequest] handle request:%d fail,return:%d", this, req.m_RtspMethod, ret);
        return -1;
    }

    if (req.m_FieldsMap.find("CSeq") != req.m_FieldsMap.end())
    {
        m_nSeq = atoi(req.m_FieldsMap.at("CSeq").c_str());
    }

    return 0;
}

int32_t RTSPClient::OnRecvRtspResponse(const  std::shared_ptr<RtspParser::RtspResponse>& rsp)
{
    bool bFindSeq = false;
    int seq = 0;

    if (rsp->m_FieldsMap.find("CSeq") != rsp->m_FieldsMap.end())
    {
        seq = atoi(rsp->m_FieldsMap.at("CSeq").c_str());
        {
            std::lock_guard<std::mutex> lock(m_RtspResponseMapLock);
            m_RtspResponseMap[seq] = rsp;
        }

        std::lock_guard<std::mutex> lock(m_SignalObjectMapLock);
        if (m_SignalObjectMap.find(seq) != m_SignalObjectMap.end())
        {
            m_SignalObjectMap[seq]->Signal();
        }
    }

    return 0;
}

int32_t RTSPClient::OnRecvRtcp(uint8_t* const  msg, const uint32_t size)
{
    return 0;
}

int32_t RTSPClient::OnRecvRtp(uint8_t* const  msg, const uint32_t size)
{
    int32_t trackId = (msg[1] >> 1) + 1;
    if (trackId == m_nVideoTrackID)
    {
        OnRecvVideo(msg + 4, size - 4);
    }
    else if (trackId == m_nAudioTrackID)
    {
        OnRecvAudio(msg + 4, size - 4);
    }
    else
    {
        Error("[%p][RTSPClient::OnRecvRtp] recv unknow trackid:%d", this, trackId);
        return -1;
    }
    return 0;
}

void AddCSeqAndSession(const RtspParser::RtspRequest& req, RtspParser::RtspResponse& rsp)
{
    if (req.m_FieldsMap.find("CSeq") != req.m_FieldsMap.end())
    {
        rsp.m_FieldsMap["CSeq"] = req.m_FieldsMap.at("CSeq");
    }
    if (req.m_FieldsMap.find("Session") != req.m_FieldsMap.end())
    {
        rsp.m_FieldsMap["Session"] = req.m_FieldsMap.at("Session");
    }
}

int32_t RTSPClient::HandleDescribeRequest(const RtspParser::RtspRequest& req)
{
    RtspParser::RtspResponse rsp;
    rsp.m_StrVersion = "RTSP/1.0";
    rsp.m_StrErrcode = "400";
    rsp.m_StrReason = "Bad Request";
    AddCSeqAndSession(req, rsp);

    return SendRtspResponse(rsp);
}

int32_t RTSPClient::HandleAnnounceRequest(const RtspParser::RtspRequest& req)
{
    RtspParser::RtspResponse rsp;
    rsp.m_StrVersion = "RTSP/1.0";
    rsp.m_StrErrcode = "400";
    rsp.m_StrReason = "Bad Request";
    AddCSeqAndSession(req, rsp);

    return SendRtspResponse(rsp);
}

int32_t RTSPClient::HandleGetParameterRequest(const RtspParser::RtspRequest& req)
{
    RtspParser::RtspResponse rsp;
    rsp.m_StrVersion = "RTSP/1.0";
    rsp.m_StrErrcode = "400";
    rsp.m_StrReason = "Bad Request";
    AddCSeqAndSession(req, rsp);

    return SendRtspResponse(rsp);
}

int32_t RTSPClient::HandleOptionsRequest(const RtspParser::RtspRequest& req)
{
    RtspParser::RtspResponse rsp;
    rsp.m_StrVersion = "RTSP/1.0";
    rsp.m_StrErrcode = "200";
    rsp.m_StrReason = "OK";
    AddCSeqAndSession(req, rsp);

    rsp.m_FieldsMap["Public"] = "OPTIONS,TEARDOWN";

    return SendRtspResponse(rsp);
}

int32_t RTSPClient::HandlePauseRequest(const RtspParser::RtspRequest& req)
{
    RtspParser::RtspResponse rsp;
    rsp.m_StrVersion = "RTSP/1.0";
    rsp.m_StrErrcode = "400";
    rsp.m_StrReason = "Bad Request";
    AddCSeqAndSession(req, rsp);

    return SendRtspResponse(rsp);
}

int32_t RTSPClient::HandlePlayRequest(const RtspParser::RtspRequest& req)
{
    RtspParser::RtspResponse rsp;
    rsp.m_StrVersion = "RTSP/1.0";
    rsp.m_StrErrcode = "400";
    rsp.m_StrReason = "Bad Request";
    AddCSeqAndSession(req, rsp);

    return SendRtspResponse(rsp);
}

int32_t RTSPClient::HandleRecordRequest(const RtspParser::RtspRequest& req)
{
    RtspParser::RtspResponse rsp;
    rsp.m_StrVersion = "RTSP/1.0";
    rsp.m_StrErrcode = "400";
    rsp.m_StrReason = "Bad Request";
    AddCSeqAndSession(req, rsp);

    return SendRtspResponse(rsp);
}

int32_t RTSPClient::HandleRedirectRequest(const RtspParser::RtspRequest& req)
{
    RtspParser::RtspResponse rsp;
    rsp.m_StrVersion = "RTSP/1.0";
    rsp.m_StrErrcode = "400";
    rsp.m_StrReason = "Bad Request";
    AddCSeqAndSession(req, rsp);

    return SendRtspResponse(rsp);
}

int32_t RTSPClient::HandleSetupdRequest(const RtspParser::RtspRequest& req)
{
    RtspParser::RtspResponse rsp;
    rsp.m_StrVersion = "RTSP/1.0";
    rsp.m_StrErrcode = "400";
    rsp.m_StrReason = "Bad Request";
    AddCSeqAndSession(req, rsp);

    return SendRtspResponse(rsp);
}

int32_t RTSPClient::HandleSetParameterRequest(const RtspParser::RtspRequest& req)
{
    RtspParser::RtspResponse rsp;
    rsp.m_StrVersion = "RTSP/1.0";
    rsp.m_StrErrcode = "400";
    rsp.m_StrReason = "Bad Request";
    AddCSeqAndSession(req, rsp);

    return SendRtspResponse(rsp);
}

int32_t RTSPClient::HandleTeardownRequest(const RtspParser::RtspRequest& req)
{
    RtspParser::RtspResponse rsp;
    rsp.m_StrVersion = "RTSP/1.0";
    rsp.m_StrErrcode = "200";
    rsp.m_StrReason = "OK";
    AddCSeqAndSession(req, rsp);

    int32_t ret = SendRtspResponse(rsp);
    m_bCloseClient = true;
    return ret;
}

int32_t RTSPClient::SendRtspResponse(const RtspParser::RtspResponse& rsp)
{
    std::string strMsg;
    int32_t ret = RtspParser::GetStrResponse(strMsg, rsp);
    if (ret != 0)
    {
        return -1;
    }

    ret = send(m_nClientSocketfd, strMsg.c_str(), strMsg.length(), 0);
    if (ret < 0)
    {
        Error("[%p][RTSPClient::SendRtspResponse] send error,errno:%d", this, errno);
        return -2;
    }

    return 0;
}

int32_t RTSPClient::SendKeepAliveRequest()
{
    RtspParser::RtspRequest req;
    req.m_RtspMethod = RtspParser::RtspMethod::RTSP_METHOD_OPTIONS;
    int ret = SendRtspRequest(req);
    if (ret != 0)
    {
        Error("[%p][RTSPClient::SendKeepAliveRequest] SendRtspRequest:%d fail,return:%d", this, req.m_RtspMethod, ret);
        return -1;
    }

    return 0;
}

int32_t RTSPClient::SendRtspRequest(RtspParser::RtspRequest& req)
{
    m_nSeq++;
    req.m_FieldsMap["CSeq"] = std::to_string(m_nSeq);
    if (m_strSessionId != "")
    {
        req.m_FieldsMap["Session"] = m_strSessionId;
    }
    if (req.m_FieldsMap.find("User-Agent") == req.m_FieldsMap.end())
    {
        req.m_FieldsMap["User-Agent"] = "XiHe";
    }
    if (req.m_StrUrl.size() == 0)
    {
        req.m_StrUrl = m_strPlayUrlNoExParam;
    }
    if (req.m_StrVersion.size() == 0)
    {
        req.m_StrVersion = "RTSP/1.0";
    }

    std::string strMsg;
    int32_t ret = RtspParser::GetStrRequest(strMsg, req);
    if (ret != 0)
    {
        return -1;
    }

    ret = send(m_nClientSocketfd, strMsg.c_str(), strMsg.length(), 0);
    if (ret < 0)
    {
        Error("[%p][RTSPClient::SendRtspResponse] send error,errno:%d", this, errno);
        return -2;
    }

    return 0;
}

int32_t RTSPClient::Options()
{
    RtspParser::RtspRequest req;
    req.m_RtspMethod = RtspParser::RTSP_METHOD_OPTIONS;
    int32_t ret = SendRtspRequest(req);
    if (ret != 0)
    {
        Error("[%p][RTSPClient::Options] send Options request fail,return:%d", this, ret);
        return -1;
    }

    int seq = atoi(req.m_FieldsMap["CSeq"].c_str());
    SignalObject signal;
    {
        std::lock_guard<std::mutex> lock(m_SignalObjectMapLock);
        m_SignalObjectMap[seq] = &signal;
    }
    bool bSignal = signal.Wait(RECV_TIMEOUT);
    if (!bSignal)
    {
        Error("[%p][RTSPClient::Options] wait Options response timeout", this);
        return -2;
    }

    std::shared_ptr<RtspParser::RtspResponse> rsp;
    {
        std::lock_guard<std::mutex> lock(m_RtspResponseMapLock);
        rsp = m_RtspResponseMap[seq];
        m_RtspResponseMap.erase(seq);
    }
    if (rsp == nullptr)
    {
        Error("[%p][RTSPClient::Options] Options request fail,rsp is null", this);
        return -3;
    }

    if (rsp->m_StrErrcode != "200")
    {
        Error("[%p][RTSPClient::Options] Options request fail,code:%s", this, rsp->m_StrErrcode.c_str());
        return -4;
    }

    return 0;
}

int32_t RTSPClient::Describe()
{
    RtspParser::RtspRequest req;
    req.m_RtspMethod = RtspParser::RTSP_METHOD_DESCRIBE;
    req.m_FieldsMap["Accept"] = "application/sdp";
    req.m_StrUrl = m_strPlayUrl;
    int32_t ret = SendRtspRequest(req);
    if (ret != 0)
    {
        Error("[%p][RTSPClient::Describe] send Describe request fail,return:%d", this, ret);
        return -1;
    }

    int seq = atoi(req.m_FieldsMap["CSeq"].c_str());
    SignalObject signal;
    {
        std::lock_guard<std::mutex> lock(m_SignalObjectMapLock);
        m_SignalObjectMap[seq] = &signal;
    }
    bool bSignal = signal.Wait(RECV_TIMEOUT);
    if (!bSignal)
    {
        Error("[%p][RTSPClient::Describe] wait Describe response timeout", this);
        return -2;
    }

    std::shared_ptr<RtspParser::RtspResponse> rsp;
    {
        std::lock_guard<std::mutex> lock(m_RtspResponseMapLock);
        rsp = m_RtspResponseMap[seq];
        m_RtspResponseMap.erase(seq);
    }
    if (rsp == nullptr)
    {
        Error("[%p][RTSPClient::Describe] not recv rsp", this);
        return -3;
    }
    if (rsp->m_StrErrcode != "200")
    {
        Error("[%p][RTSPClient::Describe] Describe request fail,code:%s", this, rsp->m_StrErrcode.c_str());
        return -3;
    }
    if (rsp->m_StrContent.size() == 0)
    {
        Error("[%p][RTSPClient::Describe] not recv sdp", this);
        return -4;
    }

    SdpParser parser;
    ret = parser.Parse(rsp->m_StrContent);
    if (ret != 0)
    {
        Error("[%p][RTSPClient::Describe] parse sdp:%s fail", this, rsp->m_StrContent.c_str());
        return -5;
    }

    bool bHasFindMedia = false;
    for (auto& description : parser.GetMediaDescription())
    {
        std::string media = description.field.at("m");
        std::vector<std::string> items;
        split(media, items, " ");
        if (items[0] == "video")
        {
            Trace("[%p][RTSPClient::Describe] video media:%s", this, media.c_str());
            m_nVideoPT = description.nPayloadType;
            m_eVideoFormat =
                description.strMediaFormat == "H264" ? AV_CODEC_ID_H264 :
                description.strMediaFormat == "H265" ? AV_CODEC_ID_H265 :
                description.strMediaFormat == "MJPG" ? AV_CODEC_ID_MJPEG : AV_CODEC_ID_NONE;
            if (m_eVideoFormat == AV_CODEC_ID_NONE)
            {
                Error("[%p][RTSPClient::Describe] not support video:%s", this, description.strMediaFormat.c_str());
                return -6;
            }
            m_nVideoTrackID = description.nTrackID;
            m_nVideoClockRate = description.nClockRate;

            if (m_pVideoReadyCallbaclk != nullptr)
            {
                VideoInfo info;
                info.m_nCodecID = m_eVideoFormat;
                info.m_nWidth = 1280;
                info.m_nHight = 720;
                m_pVideoReadyCallbaclk(info);
            }
        }
        else if (items[0] == "audio")
        {
            Trace("[%p][RTSPClient::Describe] audio media:%s", this, media.c_str());
            m_nAudioPT = description.nPayloadType;
            m_eAudioFormat = description.strMediaFormat == "PCMA" ? AV_CODEC_ID_PCM_ALAW :
                description.strMediaFormat == "PCMU" ? AV_CODEC_ID_PCM_MULAW : AV_CODEC_ID_NONE;
            if (m_eAudioFormat == AV_CODEC_ID_NONE)
            {
                Error("[%p][RTSPClient::Describe] not support audio:%s", this, description.strMediaFormat.c_str());
                return -7;
            }
            m_nVideoTrackID = description.nTrackID;
            m_nAideoClockRate = description.nClockRate;
        }
        else
        {
            Error("[%p][RTSPClient::Describe] unknow media:%s", this, media.c_str());
        }
    }

    return 0;
}

int32_t RTSPClient::Pause()
{
    RtspParser::RtspRequest req;
    req.m_RtspMethod = RtspParser::RTSP_METHOD_PAUSE;
    int32_t ret = SendRtspRequest(req);
    if (ret != 0)
    {
        Error("[%p][RTSPClient::Pause] send Pause request fail,return:%d", this, ret);
        return -1;
    }

    int seq = atoi(req.m_FieldsMap["CSeq"].c_str());
    SignalObject signal;
    {
        std::lock_guard<std::mutex> lock(m_SignalObjectMapLock);
        m_SignalObjectMap[seq] = &signal;
    }
    bool bSignal = signal.Wait(RECV_TIMEOUT);
    if (!bSignal)
    {
        Error("[%p][RTSPClient::Pause] wait Pause response timeout", this);
        return -2;
    }

    std::shared_ptr<RtspParser::RtspResponse> rsp;
    {
        std::lock_guard<std::mutex> lock(m_RtspResponseMapLock);
        rsp = m_RtspResponseMap[seq];
        m_RtspResponseMap.erase(seq);
    }
    if (rsp == nullptr)
    {
        Error("[%p][RTSPClient::Pause] not recv rsp", this);
        return -3;
    }
    if (rsp->m_StrErrcode != "200")
    {
        Error("[%p][RTSPClient::Pause] Pause request fail,code:%s", this, rsp->m_StrErrcode.c_str());
        return -4;
    }

    return 0;
}

int32_t RTSPClient::Play()
{
    RtspParser::RtspRequest req;
    req.m_RtspMethod = RtspParser::RTSP_METHOD_PLAY;
    int32_t ret = SendRtspRequest(req);
    if (ret != 0)
    {
        Error("[%p][RTSPClient::Play] send Play request fail,return:%d", this, ret);
        return -1;
    }

    int seq = atoi(req.m_FieldsMap["CSeq"].c_str());
    SignalObject signal;
    {
        std::lock_guard<std::mutex> lock(m_SignalObjectMapLock);
        m_SignalObjectMap[seq] = &signal;
    }
    bool bSignal = signal.Wait(RECV_TIMEOUT);
    if (!bSignal)
    {
        Error("[%p][RTSPClient::Play] wait Play response timeout", this);
        return -2;
    }

    std::shared_ptr<RtspParser::RtspResponse> rsp;
    {
        std::lock_guard<std::mutex> lock(m_RtspResponseMapLock);
        rsp = m_RtspResponseMap[seq];
        m_RtspResponseMap.erase(seq);
    }
    if (rsp == nullptr)
    {
        Error("[%p][RTSPClient::Play] not recv rsp", this);
        return -3;
    }
    if (rsp->m_StrErrcode != "200")
    {
        Error("[%p][RTSPClient::Play] Play request fail,code:%s", this, rsp->m_StrErrcode.c_str());
        return -4;
    }

    m_bIsPlaying = true;

    return 0;
}

int32_t RTSPClient::Setup(int32_t trackid)
{
    if (trackid != m_nVideoTrackID && trackid != m_nAudioTrackID)
    {
        Error("[%p][RTSPClient::Setup] unknow trackid:%d", this, trackid);
        return -1;
    }

    RtspParser::RtspRequest req;
    req.m_RtspMethod = RtspParser::RTSP_METHOD_SETUP;
    req.m_StrUrl = m_strPlayUrlNoExParam + "/trackID=" + std::to_string(trackid);
    if (trackid == m_nVideoTrackID && m_eVideoTransport == TransportType::TCP)
    {
        int32_t id = (trackid - 1) << 1;
        req.m_FieldsMap["Transport"] = "RTP/AVP/TCP;unicast;interleaver=" +
            std::to_string(id) + "-" + std::to_string(id + 1);
    }
    else
    {
        uint16_t nRtpPort = 0;
        uint16_t nRtcpPort = 0;
        int32_t nRtpfd = -1;
        int32_t nRtcpfd = -1;
        if (AllocUdpMediaSocket(m_strServerIP, nRtpPort, nRtcpPort, nRtpfd, nRtcpfd) != 0)
        {
            Error("[%p][RTSPClient::Setup] alloc udp media socket fail", this);
            return -2;
        }
        if (trackid == m_nVideoTrackID)
        {
            m_nVideoRtpfd = nRtpfd;
            m_nVideoRtcpfd = nRtcpfd;
        }
        else
        {
            m_nAudioRtpfd = nRtpfd;
            m_nAudioRtcpfd = nRtcpfd;
        }

        req.m_FieldsMap["Transport"] = "RTP/AVP/UDP;unicast;client_port=" +
            std::to_string(nRtpPort) + "-" + std::to_string(nRtcpPort);
    }

    int32_t ret = SendRtspRequest(req);
    if (ret != 0)
    {
        Error("[%p][RTSPClient::Setup] send Play request fail,return:%d", this, ret);
        return -3;
    }

    int seq = atoi(req.m_FieldsMap["CSeq"].c_str());
    SignalObject signal;
    {
        std::lock_guard<std::mutex> lock(m_SignalObjectMapLock);
        m_SignalObjectMap[seq] = &signal;
    }
    bool bSignal = signal.Wait(RECV_TIMEOUT);
    if (!bSignal)
    {
        Error("[%p][RTSPClient::Setup] wait Setup response timeout", this);
        return -4;
    }

    std::shared_ptr<RtspParser::RtspResponse> rsp;
    {
        std::lock_guard<std::mutex> lock(m_RtspResponseMapLock);
        rsp = m_RtspResponseMap[seq];
        m_RtspResponseMap.erase(seq);

        if (rsp == nullptr)
        {
            Error("[%p][RTSPClient::Setup] not recv rsp", this);
            return -5;
        }

        if (rsp->m_FieldsMap.find("Session") != rsp->m_FieldsMap.end())
        {
            m_strSessionId = rsp->m_FieldsMap["Session"];
        }
    }
    if (rsp->m_StrErrcode != "200")
    {
        Error("[%p][RTSPClient::Setup] Setup request fail,code:%s", this, rsp->m_StrErrcode.c_str());
        return -6;
    }

    if (trackid == m_nVideoTrackID)
    {
        if (m_pVideoParser != nullptr)
        {
            delete m_pVideoParser;
        }
        m_pVideoParser =
            m_eVideoFormat == AV_CODEC_ID_H264 ? (RTPParser*)new H264RTPParser() :
            m_eVideoFormat == AV_CODEC_ID_MJPEG ? (RTPParser*)new MJPEGRTPParser() : nullptr;
        m_pVideoParser->SetPacketCallbaclk(m_pVideoPacketCallbaclk);

        delete m_pFECDecoder;
        if (m_eVideoTransport == UDP)
        {
            m_bEnableFec = true;
            m_pFECDecoder = new RFC8627FECDecoder();
            RFC8627FECDecoder::FECDecoderPacketCallback pFECDecoderPacketCallback = std::bind(&RTSPClient::OnRecvFECDecoderPacket, this, std::placeholders::_1);
            m_pFECDecoder->SetDecoderPacketCallback(pFECDecoderPacketCallback);
            RFC8627FECDecoder::NackPacketCallback pNackPacketCallback = std::bind(&RTSPClient::OnRecvNackPacket, this, std::placeholders::_1);
            m_pFECDecoder->SetNackPacketCallback(pNackPacketCallback);
            m_pFECDecoder->SetPayloadType(109);
            m_pFECDecoder->SetSSRC(0x23456789);
            m_pFECDecoder->Init(7, 7, 5);
        }
    }
    else
    {

    }

    return 0;
}

int32_t RTSPClient::Teardown()
{
    RtspParser::RtspRequest req;
    req.m_RtspMethod = RtspParser::RTSP_METHOD_TEARDOWN;
    int32_t ret = SendRtspRequest(req);
    if (ret != 0)
    {
        Error("[%p][RTSPClient::Teardown] send Teardown request fail,return:%d", this, ret);
        return -1;
    }

    int seq = atoi(req.m_FieldsMap["CSeq"].c_str());
    SignalObject signal;
    {
        std::lock_guard<std::mutex> lock(m_SignalObjectMapLock);
        m_SignalObjectMap[seq] = &signal;
    }
    bool bSignal = signal.Wait(RECV_TIMEOUT);
    if (!bSignal)
    {
        Error("[%p][RTSPClient::Teardown] wait Teardown response timeout", this);
        return -2;
    }

    std::shared_ptr<RtspParser::RtspResponse> rsp;
    {
        std::lock_guard<std::mutex> lock(m_RtspResponseMapLock);
        rsp = m_RtspResponseMap[seq];
        m_RtspResponseMap.erase(seq);
    }
    if (rsp == nullptr)
    {
        Error("[%p][RTSPClient::Teardown] not recv rsp", this);
        return -3;
    }
    if (rsp->m_StrErrcode != "200")
    {
        Error("[%p][RTSPClient::Teardown] Teardown request fail,code:%s", this, rsp->m_StrErrcode.c_str());
        return -4;
    }

    m_bCloseClient = true;

    return 0;
}

int32_t AllocClientUdpSocket(const std::string& ip, uint16_t& port)
{
    int32_t fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd == -1)
    {
        Error("[AllocClientUdpSocket] new socket fail,error:%d", errno);
        return fd;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    //addr.sin_addr.s_addr = inet_addr(ip.c_str());
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    int len = sizeof(addr);
    int32_t ret = bind(fd, (sockaddr*)&addr, len);
    if (ret == -1)
    {
        Error("[AllocClientUdpSocket] connect socket fail,error:%d", errno);
        close(fd);
        return -1;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    ret = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (ret == -1)
    {
        Error("[AllocClientUdpSocket] set NONBLOCK fail,errno:%d", errno);
        close(fd);
        return -1;
    }

    return fd;
}

int32_t AllocUdpMediaSocket(const std::string& ip, uint16_t& port1, uint16_t& port2, int32_t& fd1, int32_t& fd2)
{
    uint16_t port = rand() % 50000 + 1024;
    port &= 0xfffe;
    uint16_t alloc = port;
    bool bSucess = false;

    while (alloc > 1024)
    {
        bSucess = true;
        port1 = alloc;
        port2 = alloc + 1;
        alloc -= 2;
        fd1 = AllocClientUdpSocket(ip, port1);
        fd2 = AllocClientUdpSocket(ip, port2);

        if (fd1 == -1 || fd2 == -1)
        {
            bSucess = false;
            if (fd1 != -1)
            {
                close(fd1);
                fd1 = -1;
            }
            if (fd2 != -1)
            {
                close(fd2);
                fd2 = -1;
            }
        }

        if (bSucess)
        {
            return 0;
        }
    }

    while (alloc < 65530)
    {
        bSucess = true;
        port1 = alloc;
        port2 = alloc + 1;
        alloc += 2;
        fd1 = AllocClientUdpSocket(ip, port1);
        fd2 = AllocClientUdpSocket(ip, port2);

        if (fd1 == -1 || fd2 == -1)
        {
            bSucess = false;
            if (fd1 != -1)
            {
                close(fd1);
                fd1 = -1;
            }
            if (fd2 != -1)
            {
                close(fd2);
                fd2 = -1;
            }
        }

        if (bSucess)
        {
            return 0;
        }
    }

    return -1;
}

int32_t GetLocalIPAndPort(int32_t fd, std::string& ip, uint16_t& port)
{
    struct sockaddr addr;
    socklen_t addr_len = sizeof(addr);
    memset(&addr, 0, sizeof(addr));

    if (getsockname(fd, &addr, &addr_len) != 0)
    {
        Error("[GetLocalIPAndPort] getsockname fail.errno:%d", errno);
        return -1;
    }

    sockaddr_in* addr_v4 = (sockaddr_in*)&addr;
    ip = inet_ntoa(addr_v4->sin_addr);
    port = ntohs(addr_v4->sin_port);

    return 0;
}

int32_t GetRemoteIPAndPort(int32_t fd, std::string& ip, uint16_t& port)
{
    struct sockaddr addr;
    socklen_t addr_len = sizeof(addr);
    memset(&addr, 0, sizeof(addr));

    if (getpeername(fd, &addr, &addr_len) != 0)
    {
        Error("[GetRemoteIPAndPort] getpeername fail.errno:%d", errno);
        return -1;
    }

    sockaddr_in* addr_v4 = (sockaddr_in*)&addr;
    ip = inet_ntoa(addr_v4->sin_addr);
    port = ntohs(addr_v4->sin_port);

    return 0;
}

int32_t RTSPClient::OnRecvVideo(uint8_t* const  msg, const uint32_t size)
{
    uint8_t* data = (uint8_t*)malloc(size);
    if (data == nullptr)
    {
        Error("[%p][RTSPClient::OnRecvVideo] malloc data fail", this);
        return -1;
    }
    memcpy(data, msg, size);
    std::shared_ptr<Packet> packet = std::make_shared<Packet>();
    packet->m_pData = data;
    packet->m_nLength = size;

    if (m_bEnableFec)
    {
        if (m_pFECDecoder != nullptr)
        {
            m_pFECDecoder->RecvPacket(packet);
        }
    }
    else
    {
        if (m_pVideoParser != nullptr)
        {
            m_pVideoParser->RecvPacket(packet);
        }
    }

    return 0;
}

void RTSPClient::OnRecvFECDecoderPacket(const std::shared_ptr<Packet>& packet)
{
    if (m_pVideoParser != nullptr)
    {
        m_pVideoParser->RecvPacket(packet);
    }
}

void RTSPClient::OnRecvNackPacket(const std::shared_ptr<Packet>& packet)
{

}

int32_t RTSPClient::OnRecvAudio(uint8_t* const  msg, const uint32_t size)
{
    return 0;
}

int32_t RTSPClient::SetVideoPacketCallbaclk(RTPParser::MediaPacketCallbaclk callback)
{
    m_pVideoPacketCallbaclk = callback;
    return 0;
}

int32_t RTSPClient::SetAudioPacketCallbaclk(RTPParser::MediaPacketCallbaclk callback)
{
    m_pAudioPacketCallbaclk = callback;
    return 0;
}

int32_t RTSPClient::SetVideoReadyCallbaclk(RTSPClient::VideoReadyCallbaclk callback)
{
    m_pVideoReadyCallbaclk = callback;
    return 0;
}

int32_t RTSPClient::RecvUDPMedia(uint8_t* pRecvBuff, int32_t size)
{
    int32_t nRecv = 0;
    if (m_nVideoRtpfd != -1)
    {
        ssize_t len = recv(m_nVideoRtpfd, pRecvBuff, size, 0);
        if (len > 0)
        {
            OnRecvVideo(pRecvBuff, len);
            nRecv += len;
        }
    }

    if (m_nAudioRtpfd != -1)
    {
        ssize_t len = recv(m_nAudioRtpfd, pRecvBuff, size, 0);
        if (len > 0)
        {
            OnRecvAudio(pRecvBuff, len);
            nRecv += len;
        }
    }

    return nRecv;
}
