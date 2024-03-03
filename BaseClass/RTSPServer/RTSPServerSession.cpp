#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <linux/videodev2.h>
#include "RTSPServerSession.h"
#include "Log/Log.h"
#include "MediaCapture/VideoCapture.h"

#define RECV_BUFF_SIZE (1024*4)
#define HEART_BEAT_CYCLE (15*1000)
#define HEART_BEAT_TIMEOUT (60*1000)
#define MAX_RTP_CACHE_NUM (200)
#define MAX_PACKET_SIZE 1600
extern int g_nCaptureWidth;
extern int g_nCaptureHeight;

RTSPServerSession::RTSPServerSession(uint32_t fd, std::string strRemoteIP)
{
    m_nSessionfd = fd;
    m_strSessionId = "";
    m_nSeq = 0;
    m_strUrl = "";

    m_bStopSession = true;
    m_pSessionThread = nullptr;

    m_strResouceType = "";
    m_strResouce = "";
    m_nVideoWidth = 1280;
    m_nVideoHight = 720;
    m_eVideoType = VIDEO_TYPE_H264;
    m_nFps = 25;

    uint16_t port;
    GetLocalIPAndPort(m_nSessionfd, m_strLocalIP, port);
    m_strRemoteIP = strRemoteIP;

    m_nVideoTrackId = -1;
    m_nAudioTrackId = -1;
    m_eVideoTransport = UDP;
    m_eAudioTransport = UDP;
    m_nVideoRtpfd = -1;
    m_nVideoRtcpfd = 1;
    m_nAudioRtpfd = -1;
    m_nAudioRtcpfd = -1;

    m_pImageTransoprt = nullptr;
    m_bStopSendMedia = true;
    m_pSendMediaThread = nullptr;
    m_bSessionFinished = false;
    m_bEnableOSD = false;
    m_pSendBuff = nullptr;
}

RTSPServerSession::~RTSPServerSession()
{
    ReleaseAll();
}

int32_t RTSPServerSession::ReleaseAll()
{
    if (m_pImageTransoprt != nullptr)
    {
        delete m_pImageTransoprt;
        m_pImageTransoprt = nullptr;
    }

    m_bStopSendMedia = true;
    if (m_pSendMediaThread != nullptr)
    {
        if (m_pSendMediaThread->joinable())
        {
            m_pSendMediaThread->join();
        }
        delete m_pSendMediaThread;
        m_pSendMediaThread = nullptr;
    }

    m_bStopSession = true;
    m_pSessionThread = nullptr;
    /*if (m_pSessionThread != nullptr)
    {
        if (m_pSessionThread->joinable())
        {
            m_pSessionThread->join();
        }
        delete m_pSessionThread;
        m_pSessionThread = nullptr;
    }*/

    if (m_nSessionfd != -1)
    {
        close(m_nSessionfd);
        m_nSessionfd = -1;
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

    m_SessionBuff.ClearBuff(0);
    m_nSeq = 0;
    m_strUrl = "";
    m_strResouceType = "";
    m_strResouce = "";
    m_nVideoWidth = 1280;
    m_nVideoHight = 720;
    m_strLocalIP = "";
    m_strRemoteIP = "";
    m_nVideoTrackId = -1;
    m_nAudioTrackId = -1;
    m_eVideoTransport = UDP;
    m_eAudioTransport = UDP;
    m_bEnableOSD = false;
    free(m_pSendBuff);
    m_pSendBuff = nullptr;

    return 0;
}

int32_t RTSPServerSession::StartSession()
{
    if (m_pSessionThread != nullptr)
    {
        Error("[%p][RTSPServer::StartSession] RTSPServerSession has been started", this);
        return -1;
    }

    int flags = fcntl(m_nSessionfd, F_GETFL, 0);
    int ret = fcntl(m_nSessionfd, F_SETFL, flags | O_NONBLOCK);
    if (ret == -1)
    {
        Error("[%p][RTSPServer::StartSession] set NONBLOCK fail,errno:%d", this, errno);
        return -2;
    }

    m_bStopSession = false;
    m_pSessionThread = new std::thread(&RTSPServerSession::SessionThread, this);
    m_pSessionThread->detach();

    return 0;
}

void RTSPServerSession::SessionThread()
{
    Trace("[%p][RTSPServer::SessionThread] start SessionThread", this);
    uint8_t* pRecvBuff = (uint8_t*)malloc(RECV_BUFF_SIZE);
    if (pRecvBuff == nullptr)
    {
        Error("[%p][RTSPServer::SessionThread] malloc recv buff fail", this);
        return;
    }

    m_HeartBeatimeoutTimer.MakeTimePoint();

    bool m_bNeedWait;
    while (!m_bStopSession)
    {
        m_bNeedWait = false;

        ssize_t len = recv(m_nSessionfd, pRecvBuff, RECV_BUFF_SIZE, 0);
        if (len == -1)
        {
            int err = errno;
            if (err != EAGAIN)
            {
                Error("[%p][RTSPServer::ServerThread]  recv error:%d", this, err);
                m_bStopSession = true;
                break;
            }

            m_bNeedWait = true;
        }
        else
        {
            m_SessionBuff.Append(pRecvBuff, len);
            HandleMsg();
        }

        if (m_HeartBeatimeoutTimer.GetDuration() > HEART_BEAT_TIMEOUT)
        {
            Error("[%p][RTSPServer::ServerThread]  recv heart timeout:%d", this, HEART_BEAT_TIMEOUT);
            m_bStopSession = true;
            break;
        }

        if (m_bNeedWait)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    free(pRecvBuff);
    Trace("[%p][RTSPServer::SessionThread] exit SessionThread", this);
    StopSession();
}

int32_t RTSPServerSession::StopSession()
{
    int32_t ret = ReleaseAll();
    m_bSessionFinished = true;
    return ret;
}

bool RTSPServerSession::IsRtspRequestMsg(uint8_t* const  msg, const uint32_t size)
{
    std::string strMsg((char*)msg);
    bool ret = strMsg.find("OPTIONS ") == 0 ? true :
        strMsg.find("DESCRIBE ") == 0 ? true :
        strMsg.find("PAUSE ") == 0 ? true :
        strMsg.find("PLAY ") == 0 ? true :
        strMsg.find("SETUP ") == 0 ? true :
        strMsg.find("RECORD ") == 0 ? true :
        strMsg.find("GET_PARAMETER ") == 0 ? true :
        strMsg.find("SET_PARAMETER ") == 0 ? true :
        strMsg.find("TEARDOWN ") == 0 ? true :
        strMsg.find("ANNOUNCE ") == 0 ? true :
        strMsg.find("REDIRECT ") == 0 ? true : false;

    return ret;
}

bool RTSPServerSession::IsRtspResponseMsg(uint8_t* const  msg, const uint32_t size)
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

bool RTSPServerSession::IsRtcpMsg(uint8_t* const  msg, const uint32_t size)
{
    return (msg[0] == 0x24 && (msg[1] % 2 == 0));
}

bool RTSPServerSession::IsRtpMsg(uint8_t* const  msg, const uint32_t size)
{
    return (msg[0] == 0x24 && (msg[1] % 2 == 1));
}

bool RTSPServerSession::FindRtspMsg(uint8_t* const  msg, const uint32_t size, uint32_t& msgSize)
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

bool RTSPServerSession::FindRtcpMsg(uint8_t* const  msg, const uint32_t size, uint32_t& msgSize)
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

bool RTSPServerSession::FindRtpMsg(uint8_t* const  msg, const uint32_t size, uint32_t& msgSize)
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

int32_t RTSPServerSession::HandleMsg()
{
    uint8_t* pRawData;
    uint32_t nDataSize;
    bool handed = false;

    while (true)
    {
        handed = false;
        m_SessionBuff.GetRawData(pRawData, nDataSize);
        if (nDataSize > 4)
        {
            if (IsRtpMsg(pRawData, nDataSize))
            {
                handed = true;
                uint32_t nMsgSize;
                if (FindRtpMsg(pRawData, nDataSize, nMsgSize))
                {
                    m_SessionBuff.Read(pRawData, nMsgSize);
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
                    m_SessionBuff.Read(pRawData, nMsgSize);
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
                    RtspParser::RtspResponse rsp;
                    std::string strMsg = std::string((char*)pRawData, nMsgSize);
                    uint32_t ret = RtspParser::ParseRtspResponse(strMsg, rsp);
                    if (ret == 0)
                    {
                        if (rsp.m_FieldsMap.find("Content-length") != rsp.m_FieldsMap.end())
                        {
                            int nContentLength = atoi(rsp.m_FieldsMap.at("Content-length").c_str());
                            if (nMsgSize + nContentLength <= nDataSize)
                            {
                                m_SessionBuff.Read(pRawData, nMsgSize);
                                m_SessionBuff.Read(pRawData, nContentLength);
                                rsp.m_StrContent = std::string((char*)pRawData, nContentLength);
                                Trace("[%p][RTSPServer::HandleMsg] Recv rtsp response:%s", this, strMsg.c_str());
                                OnRecvRtspResponse(rsp);
                            }
                            else
                            {
                                break;
                            }
                        }
                        else
                        {
                            m_SessionBuff.Read(pRawData, nMsgSize);
                            Trace("[%p][RTSPServer::HandleMsg] Recv rtsp response:%s", this, strMsg.c_str());
                            OnRecvRtspResponse(rsp);
                        }
                    }
                    else
                    {
                        m_SessionBuff.Read(pRawData, nMsgSize);
                        Error("[%p][RTSPServer::HandleMsg] ParseRtspResponse fail,ret:%d msg:%s", this, ret, strMsg.c_str());
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
                                m_SessionBuff.Read(pRawData, nMsgSize);
                                m_SessionBuff.Read(pRawData, nContentLength);
                                req.m_StrContent = std::string((char*)pRawData, nContentLength);
                                Trace("[%p][RTSPServer::HandleMsg] Recv rtsp request:%s", this, strMsg.c_str());
                                OnRecvRtspRequest(req);
                            }
                            else
                            {
                                break;
                            }
                        }
                        else
                        {
                            m_SessionBuff.Read(pRawData, nMsgSize);
                            Trace("[%p][RTSPServer::HandleMsg] Recv rtsp request:%s", this, strMsg.c_str());
                            OnRecvRtspRequest(req);
                        }
                    }
                    else
                    {
                        m_SessionBuff.Read(pRawData, nMsgSize);
                        Error("[%p][RTSPServer::HandleMsg] ParseRtspRequest fail,ret:%d msg:%s", this, ret, strMsg.c_str());
                    }
                }
                else
                {
                    break;
                }
            }
            else
            {
                Error("[%p][RTSPServer::HandleMsg] recv err msg:%s", this, pRawData);
                m_SessionBuff.ClearBuff();
            }
        }
        else
        {
            break;
        }
    }

    return 0;
}

int32_t RTSPServerSession::OnRecvRtspRequest(const RtspParser::RtspRequest& req)
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
        Error("[%p][RTSPServer::OnRecvRtspRequest] handle request:%d fail,return:%d", this, req.m_RtspMethod, ret);
        return -1;
    }

    if (req.m_FieldsMap.find("CSeq") != req.m_FieldsMap.end())
    {
        m_nSeq = atoi(req.m_FieldsMap.at("CSeq").c_str());
    }

    if (m_strUrl.size() == 0)
    {
        std::vector<std::string> result;
        split(req.m_StrUrl, result, "?");
        m_strUrl = result[0];
    }

    return 0;
}

int32_t RTSPServerSession::OnRecvRtspResponse(const RtspParser::RtspResponse& rsp)
{
    return 0;
}

int32_t RTSPServerSession::OnRecvRtcp(uint8_t* const  msg, const uint32_t size)
{
    return 0;
}

int32_t RTSPServerSession::OnRecvRtp(uint8_t* const  msg, const uint32_t size)
{
    return 0;
}

int32_t RTSPServerSession::SendRtspRequest(RtspParser::RtspRequest& req)
{
    m_nSeq++;
    req.m_FieldsMap["CSeq"] = std::to_string(m_nSeq);
    if (m_strSessionId != "")
    {
        req.m_FieldsMap["Session"] = m_strSessionId;
    }
    req.m_FieldsMap["User-Agent"] = "XiHe";
    req.m_StrUrl = m_strUrl;

    std::string strMsg;
    int32_t ret = RtspParser::GetStrRequest(strMsg, req);
    if (ret != 0)
    {
        return -1;
    }

    ret = send(m_nSessionfd, strMsg.c_str(), strMsg.length(), 0);
    if (ret < 0)
    {
        Error("[%p][RTSPClient::SendRtspResponse] send error,errno:%d", this, errno);
        return -2;
    }

    return 0;
}

int32_t RTSPServerSession::SendRtspResponse(const RtspParser::RtspResponse& rsp)
{
    std::string strMsg;
    int32_t ret = RtspParser::GetStrResponse(strMsg, rsp);
    if (ret != 0)
    {
        return -1;
    }

    ret = send(m_nSessionfd, strMsg.c_str(), strMsg.length(), 0);
    if (ret < 0)
    {
        Error("[%p][RTSPServer::SendRtspResponse] send error,errno:%d", this, errno);
        return -2;
    }

    return 0;
}

int32_t RTSPServerSession::SetVideoType(const std::string& type)
{
    Trace("[%p][RTSPServer::SetVideoType] set video type:%s", this, type.c_str());
    VideoType eVideoType =
        type == "mpeg" ? VIDEO_TYPE_MJPG :
        type == "h264" ? VIDEO_TYPE_H264 :
        VIDEO_TYPE_H264;
    m_eVideoType = eVideoType;
    return 0;
}

int32_t RTSPServerSession::SetResolution(const std::string& resolution)
{
    Trace("[%p][RTSPServer::SetResolution] set resolution:%s", this, resolution.c_str());
    std::vector<std::string> temp;
    split(resolution, temp, "*");
    if (temp.size() != 2)
    {
        Error("[%p][RTSPServer::SetResolution] input resolution:%s error", this, resolution.c_str());
        return -1;
    }

    int w = atoi(temp[0].c_str());
    int h = atoi(temp[1].c_str());
    if (w <= 0 || h <= 0)
    {
        Error("[%p][RTSPServer::SetResolution] input resolution:%s error", this, resolution.c_str());
        return -2;
    }
    m_nVideoWidth = w;
    m_nVideoHight = h;

    return 0;
}

int32_t RTSPServerSession::SetFps(const std::string& fps)
{
    Trace("[%p][RTSPServer::SetFps] set fps:%s", this, fps.c_str());
    int f = atoi(fps.c_str());
    if (f <= 0)
    {
        Error("[%p][RTSPServer::SetFps] input fps:%s error", this, fps.c_str());
        return -1;
    }
    m_nFps = f;

    return 0;
}

int32_t RTSPServerSession::ParseExtendedParame(const std::string& strExParam)
{
    if (strExParam != "")
    {
        std::vector<std::string> param;
        std::vector<std::string> temp;
        split(strExParam, param, "&");
        for (const auto& item : param)
        {
            split(item, temp, "=");
            if (temp.size() == 2)
            {
                int ret =
                    temp[0] == "image" ? SetVideoType(temp[1]) :
                    temp[0] == "resolution" ? SetResolution(temp[1]) :
                    temp[0] == "fps" ? SetFps(temp[1]) : -999;

                if (ret != 0)
                {
                    Error("[%p][RTSPServer::ParseExtendedParame] parse key:%s value:%s fail,return:%d", this, temp[0].c_str(), temp[1].c_str(), ret);
                }
            }
        }
    }

    return 0;
}

int32_t RTSPServerSession::HandleDescribeRequest(const RtspParser::RtspRequest& req)
{
    RtspParser::RtspResponse rsp;
    rsp.m_StrVersion = "RTSP/1.0";

    if (req.m_FieldsMap.find("CSeq") != req.m_FieldsMap.end())
    {
        rsp.m_FieldsMap["CSeq"] = req.m_FieldsMap.at("CSeq");
    }
    if (req.m_FieldsMap.find("Session") != req.m_FieldsMap.end())
    {
        rsp.m_FieldsMap["Session"] = req.m_FieldsMap.at("Session");
    }

    int32_t ret = 0;
    do
    {
        std::vector<std::string> temp;
        split(req.m_StrUrl, temp, "://");
        if (temp.size() != 2)
        {
            rsp.m_StrErrcode = "400";
            rsp.m_StrReason = "Bad Request";
            Error("[%p][RTSPServer::HandleDescribeRequest] describe url:%s err", this, req.m_StrUrl.c_str());
            ret = -1;
            break;
        }
        if (temp[0] != "rtsp")
        {
            rsp.m_StrErrcode = "400";
            rsp.m_StrReason = "Bad Request";
            Error("[%p][RTSPServer::HandleDescribeRequest] describe url:%s err", this, req.m_StrUrl.c_str());
            ret = -2;
            break;
        }

        std::string strExParam = "";
        std::vector<std::string> param;
        //去除?后面带的参数
        split(temp[1], param, "?");
        temp[1] = param[0];
        if (param.size() == 2)
        {
            strExParam = param[1];
            ParseExtendedParame(strExParam);
        }

        split(temp[1], param, "/");
        if (param.size() != 3)
        {
            rsp.m_StrErrcode = "400";
            rsp.m_StrReason = "Bad Request";
            Error("[%p][RTSPServer::HandleDescribeRequest] describe url:%s err", this, req.m_StrUrl.c_str());
            ret = -3;
            break;
        }

        m_strResouceType = param[1];
        if (m_strResouceType == "device")
        {
            m_strResouce = "/dev/" + param[2];
        }
        else if (m_strResouceType == "file")
        {
            m_strResouce = param[2];
        }
        else
        {
            rsp.m_StrErrcode = "400";
            rsp.m_StrReason = "not support resouce type";
            Error("[%p][RTSPServer::HandleDescribeRequest] not support resouce type:%s", this, m_strResouceType.c_str());
            ret = -4;
            break;
        }
        param.clear();
        temp.clear();

        std::string sdp;
        int32_t mMakeSdpRet = MakeSdp(sdp, req);
        if (mMakeSdpRet != 0)
        {
            rsp.m_StrErrcode = "400";
            rsp.m_StrReason = "Resource Not Reachable";
            Error("[%p][RTSPServer::HandleDescribeRequest] MakeSdp fail,return:%d", this, mMakeSdpRet);
            ret = -5;
            break;
        }

        rsp.m_StrErrcode = "200";
        rsp.m_StrReason = "OK";
        rsp.m_StrContent = sdp;
        rsp.m_FieldsMap["Content-type"] = "application/sdp";
    } while (0);

    int32_t result = SendRtspResponse(rsp);
    if (result != 0)
    {
        Error("[%p][RTSPServer::HandleDescribeRequest] SendRtspResponse fail,return:%d", this, result);
        ret = -5;
    }

    return ret;
}

int32_t RTSPServerSession::HandleAnnounceRequest(const RtspParser::RtspRequest& req)
{
    return 0;
}

int32_t RTSPServerSession::HandleGetParameterRequest(const RtspParser::RtspRequest& req)
{
    return 0;
}

int32_t RTSPServerSession::HandleOptionsRequest(const RtspParser::RtspRequest& req)
{
    m_HeartBeatimeoutTimer.MakeTimePoint();

    RtspParser::RtspResponse rsp;
    rsp.m_StrVersion = "RTSP/1.0";
    rsp.m_StrErrcode = "200";
    rsp.m_StrReason = "OK";

    if (req.m_FieldsMap.find("CSeq") != req.m_FieldsMap.end())
    {
        rsp.m_FieldsMap["CSeq"] = req.m_FieldsMap.at("CSeq");
    }
    if (req.m_FieldsMap.find("Session") != req.m_FieldsMap.end())
    {
        rsp.m_FieldsMap["Session"] = req.m_FieldsMap.at("Session");
    }

    rsp.m_FieldsMap["Public"] = "DESCRIBE,ANNOUNCE,GET_PARAMETER,OPTIONS,PAUSE,PLAY,RECORD,REDIRECT,SETUP,SET_PARAMETER,TEARDOWN";

    return SendRtspResponse(rsp);
}

int32_t RTSPServerSession::HandlePauseRequest(const RtspParser::RtspRequest& req)
{
    return 0;
}

int32_t RTSPServerSession::HandlePlayRequest(const RtspParser::RtspRequest& req)
{
    RtspParser::RtspResponse rsp;
    rsp.m_StrVersion = "RTSP/1.0";
    if (req.m_FieldsMap.find("CSeq") != req.m_FieldsMap.end())
    {
        rsp.m_FieldsMap["CSeq"] = req.m_FieldsMap.at("CSeq");
    }
    if (req.m_FieldsMap.find("Session") != req.m_FieldsMap.end())
    {
        rsp.m_FieldsMap["Session"] = req.m_FieldsMap.at("Session");
    }

    m_bStopSendMedia = false;
    if (m_pSendMediaThread == nullptr)
    {
        m_pSendMediaThread = new std::thread(&RTSPServerSession::SendMediaThread, this);
    }

    VideoCapture::VideoCaptureCapability capability;
    capability.m_nWidth = m_nVideoWidth;
    capability.m_nHeight = m_nVideoHight;
    capability.m_nFPS = m_nFps;
    capability.m_bInterlaced = false;
    capability.m_nVideoType = V4L2_PIX_FMT_MJPEG;

    delete m_pImageTransoprt;
    bool bIsEnableFec = m_eVideoTransport == UDP ? true : false;
    m_pImageTransoprt = new ImageTransoprt(bIsEnableFec);
    int ret = m_pImageTransoprt->StartTransoprt(m_strResouce, capability, m_eVideoType);
    if (ret != 0)
    {
        delete m_pImageTransoprt;
        m_pImageTransoprt = nullptr;

        m_bStopSendMedia = true;
        m_pSendMediaThread->join();
        delete m_pSendMediaThread;
        m_pSendMediaThread = nullptr;

        rsp.m_StrErrcode = "400";
        rsp.m_StrReason = "Open media fail";
        Error("[%p][RTSPServer::HandlePlayRequest] ImageTransopr StartTransoprt fail,return:%d ", this, ret);
        ret = -1;
    }
    else
    {
        EnableOSD(m_bEnableOSD);
        ImageTransoprt::RtpPacketCallbaclk callback = std::bind(&RTSPServerSession::OnRecvVideoPacket, this, std::placeholders::_1);
        m_pImageTransoprt->SetRtpPacketCallbaclk(callback);

        rsp.m_StrErrcode = "200";
        rsp.m_StrReason = "OK";
    }

    SendRtspResponse(rsp);
    Trace("[%p][RTSPServer::HandlePlayRequest]  handle play request finish", this);
    return ret;
}

int32_t RTSPServerSession::HandleRecordRequest(const RtspParser::RtspRequest& req)
{
    return 0;
}

int32_t RTSPServerSession::HandleRedirectRequest(const RtspParser::RtspRequest& req)
{
    return 0;
}

int32_t RTSPServerSession::HandleSetupdRequest(const RtspParser::RtspRequest& req)
{
    RtspParser::RtspResponse rsp;
    rsp.m_StrVersion = "RTSP/1.0";

    if (req.m_FieldsMap.find("CSeq") != req.m_FieldsMap.end())
    {
        rsp.m_FieldsMap["CSeq"] = req.m_FieldsMap.at("CSeq");
    }
    if (req.m_FieldsMap.find("Session") != req.m_FieldsMap.end())
    {
        rsp.m_FieldsMap["Session"] = req.m_FieldsMap.at("Session");
    }

    int32_t ret = 0;
    do
    {
        size_t pos = req.m_StrUrl.find_last_of("/trackID=");
        if (pos == std::string::npos)
        {
            rsp.m_StrErrcode = "400";
            rsp.m_StrReason = "Can not find track";
            Error("[%p][RTSPServer::HandleSetupdRequest] can not find track,url:%s", this, req.m_StrUrl.c_str());
            ret = -1;
            break;
        }

        std::string track = req.m_StrUrl.substr(pos + 1, 1);
        if (track == std::to_string(m_nVideoTrackId))
        {
            int32_t result = SetupVideo(req);
            if (result != 0)
            {
                rsp.m_StrErrcode = "400";
                rsp.m_StrReason = "Request resource error";
                Error("[%p][RTSPServer::HandleSetupdRequest] SetupVideo fail,return:%d", this, result);
                ret = -2;
                break;
            }

            std::string transport;
            if (m_eVideoTransport == TCP)
            {
                transport = "RTP/AVP/TCP;interleaved=0-1";
            }
            else
            {
                uint16_t nRtpPort;
                uint16_t nRtcpPort;
                std::string strLocalIp;
                GetLocalIPAndPort(m_nVideoRtpfd, strLocalIp, nRtpPort);
                GetLocalIPAndPort(m_nVideoRtcpfd, strLocalIp, nRtcpPort);

                transport = "RTP/AVP/UDP;server_port=";
                transport += std::to_string(nRtpPort);
                transport += "-";
                transport += std::to_string(nRtcpPort);
            }
            rsp.m_FieldsMap["transport"] = transport;
        }
        else if (track == std::to_string(m_nAudioTrackId))
        {
            int32_t result = SetupAudio(req);
            if (result != 0)
            {
                rsp.m_StrErrcode = "400";
                rsp.m_StrReason = "Request resource error";
                Error("[%p][RTSPServer::HandleSetupdRequest] SetupAudio fail,return:%d", this, result);
                ret = -3;
                break;
            }

            std::string transport;
            if (m_eAudioTransport == TCP)
            {
                transport = "RTP/AVP/TCP;interleaved=2-3";
            }
            else
            {
                uint16_t nRtpPort;
                uint16_t nRtcpPort;
                std::string strLocalIp;
                GetLocalIPAndPort(m_nAudioRtpfd, strLocalIp, nRtpPort);
                GetLocalIPAndPort(m_nAudioRtcpfd, strLocalIp, nRtcpPort);

                transport = "RTP/AVP/UDP;server_port=";
                transport += std::to_string(nRtpPort);
                transport += "-";
                transport += std::to_string(nRtcpPort);
            }
            rsp.m_FieldsMap["transport"] = transport;
        }
        else
        {
            rsp.m_StrErrcode = "400";
            rsp.m_StrReason = "Can not find track";
            Error("[%p][RTSPServer::HandleSetupdRequest] Can not find track:%s", this, track.c_str());
            ret = -4;
            break;
        }

        if (m_strSessionId == "")
        {
            time_t rawtime;
            time(&rawtime);
            m_strSessionId = std::to_string((long)this) + std::to_string(mktime(gmtime(&rawtime)));
            rsp.m_FieldsMap["Session"] = m_strSessionId;
        }

        rsp.m_StrErrcode = "200";
        rsp.m_StrReason = "OK";
    } while (0);

    int32_t result = SendRtspResponse(rsp);
    if (result != 0)
    {
        Error("[%p][RTSPServer::HandleDescribeRequest] SendRtspResponse fail,return:%d", this, result);
        ret = -5;
    }

    return ret;
}

int32_t RTSPServerSession::HandleSetParameterRequest(const RtspParser::RtspRequest& req)
{
    return 0;
}

int32_t RTSPServerSession::HandleTeardownRequest(const RtspParser::RtspRequest& req)
{
    RtspParser::RtspResponse rsp;
    rsp.m_StrVersion = "RTSP/1.0";
    if (req.m_FieldsMap.find("CSeq") != req.m_FieldsMap.end())
    {
        rsp.m_FieldsMap["CSeq"] = req.m_FieldsMap.at("CSeq");
    }
    if (req.m_FieldsMap.find("Session") != req.m_FieldsMap.end())
    {
        rsp.m_FieldsMap["Session"] = req.m_FieldsMap.at("Session");
    }
    rsp.m_StrErrcode = "200";
    rsp.m_StrReason = "OK";

    SendRtspResponse(rsp);
    StopSession();
    return 0;
}

int32_t RTSPServerSession::MakeSdp(std::string& sdp, const RtspParser::RtspRequest& req)
{
    if (m_strResouceType == "device")
    {
        return MakeDeviceSdp(sdp, m_strResouce, req);
    }
    else if (m_strResouceType == "file")
    {
        return MakeFileSdp(sdp, m_strResouce);
    }
    else
    {
        Error("[%p][RTSPServer::MakeSdp] ResouceType:%s error", this, m_strResouceType.c_str());
        return -3;
    }
}

int32_t RTSPServerSession::MakeDeviceSdp(std::string& sdp, const std::string dev, const RtspParser::RtspRequest& req)
{
    bool bFindDevice = false;
    std::list<std::string> devices;
    VideoCapture::ListDevices(devices);
    for (auto& device : devices)
    {
        if (device == m_strResouce)
        {
            bFindDevice = true;
            break;
        }
    }

    if (!bFindDevice)
    {
        Error("[%p][RTSPServer::MakeDeviceSdp] can not find device", this);
        return -1;
    }

    std::list<VideoCapture::VideoCaptureCapability*>* capabilitys = VideoCapture::GetDeviceCapabilities(m_strResouce);
    for (auto& capability : *capabilitys)
    {
        delete capability;
    }
    delete capabilitys;

    sdp += "v=0\r\n";
    sdp += "o=XiHe ";
    sdp += std::to_string((int64_t)this);
    sdp += " 1 IN IP4 ";
    sdp += m_strLocalIP;
    sdp += "\r\n";

    sdp += "t=0 0\r\n";
    sdp += "a=contol:";
    sdp += m_strUrl;
    sdp += "\r\n";

    sdp += "c=IN IP4 ";
    sdp += m_strLocalIP;
    sdp += "\r\n";

    sdp += "m=video 0 RTP/AVP 96\r\n";
    sdp += "b=AS:5000\r\n";
    sdp += "a=recvonly\r\n";
    sdp += "a=x-dimensions:";
    sdp += std::to_string(m_nVideoWidth);
    sdp += ",";
    sdp += std::to_string(m_nVideoHight);
    sdp += "\r\n";
    sdp += "a=control:";
    sdp += m_strUrl;
    sdp += "trackID=1";
    sdp += "\r\n";
    sdp += "a=rtpmap:96 ";
    std::string type =
        m_eVideoType == VIDEO_TYPE_H264 ? "H264" :
        m_eVideoType == VIDEO_TYPE_MJPG ? "MJPG" : "H264";
    sdp += type;
    sdp += "/90000\r\n";
    m_nVideoTrackId = 1;

    return 0;
}

int32_t RTSPServerSession::MakeFileSdp(std::string& sdp, const std::string file)
{
    return 0;
}

int32_t ConnectUdpSocket(const std::string& ip, uint16_t port)
{
    int32_t fd = -1;

    do
    {
        fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd == -1)
        {
            Error("[RTSPServer::AllocUdpSocket] create socket fail,errno:%d", errno);
            break;
        }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(struct sockaddr_in));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inet_addr(ip.c_str());
        int len = sizeof(addr);

        int ret = connect(fd, (sockaddr*)&addr, len);
        if (ret < 0)
        {
            Error("[RTSPServer::AllocUdpSocket] bind socket fail,errno:%d", errno);
            close(fd);
            fd = -1;
            break;
        }

    } while (false);

    return fd;
}

int32_t GetLocalIPAndPort(int32_t fd, std::string& ip, uint16_t& port)
{
    struct sockaddr addr;
    socklen_t addr_len = sizeof(addr);
    memset(&addr, 0, sizeof(addr));

    if (getsockname(fd, &addr, &addr_len) != 0)
    {
        Error("[RTSPServer::GetLocalIPAndPort] getsockname fail.errno:%d", errno);
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
        Error("[RTSPServer::GetRemoteIPAndPort] getpeername fail.errno:%d", errno);
        return -1;
    }

    sockaddr_in* addr_v4 = (sockaddr_in*)&addr;
    ip = inet_ntoa(addr_v4->sin_addr);
    port = ntohs(addr_v4->sin_port);

    return 0;
}

int32_t RTSPServerSession::SetupVideo(const RtspParser::RtspRequest& req)
{
    if (m_pImageTransoprt != nullptr)
    {
        Error("[%p][RTSPServer::SetupVideo] already setup", this);
        return -1;
    }

    if (req.m_FieldsMap.find("Transport") == req.m_FieldsMap.end())
    {
        Error("[%p][RTSPServer::SetupVideo] can not find transport", this);
        return -2;
    }

    std::string strTransport = req.m_FieldsMap.at("Transport");
    if (strTransport.find("TCP") != std::string::npos)
    {
        m_eVideoTransport = TCP;
    }
    else
    {
        m_eVideoTransport = UDP;
        size_t pos = strTransport.find("client_port=");
        if (pos == std::string::npos)
        {
            Error("[%p][RTSPServer::SetupVideo] can not find client_port", this);
            return -3;
        }

        std::string temp = strTransport.erase(0, pos + strlen("client_port="));
        std::vector<std::string> ports;
        split(temp, ports, "-");
        if (ports.size() < 2)
        {
            Error("[%p][RTSPServer::SetupVideo] can not find client_port", this);
            return -4;
        }

        m_nVideoRtpfd = ConnectUdpSocket(m_strRemoteIP, atoi(ports[0].c_str()));
        m_nVideoRtcpfd = ConnectUdpSocket(m_strRemoteIP, atoi(ports[1].c_str()));
        if (m_nVideoRtpfd == -1 || m_nVideoRtcpfd == -1)
        {
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

            Error("[%p][RTSPServer::SetupVideo] AllocUdpSocket fail", this);
            return -5;
        }
    }

    return 0;
}

int32_t RTSPServerSession::SetupAudio(const RtspParser::RtspRequest& req)
{
    Error("[%p][RTSPServer::HandleDescribeRequest] Not currently supported", this);
    return -1;
}

void RTSPServerSession::OnRecvVideoPacket(const std::shared_ptr<Packet>& packet)
{
    bool bHasDiscard = false;
    {
        std::lock_guard<std::mutex> lock(m_VideoRtpPacketListLock);
        m_VideoRtpPacketList.push_back(packet);

        if (m_VideoRtpPacketList.size() > (MAX_RTP_CACHE_NUM))
        {
            m_VideoRtpPacketList.clear();
        }
    }

    if (bHasDiscard)
    {
        Warn("[%p][RTSPServerSession::OnRecvRtpPacket] RtpPacketList Packet List  size > %d,discard", this, MAX_RTP_CACHE_NUM);
    }
}

void RTSPServerSession::SendMediaThread()
{
    if (m_pSendBuff == nullptr)
    {
        m_pSendBuff = (uint8_t*)malloc(MAX_PACKET_SIZE);
        if (m_pSendBuff == nullptr)
        {
            Error("[%p][RTSPServer::StartSession] maloc send buff fail", this);
            return;
        }
    }

    while (!m_bStopSendMedia)
    {
        bool bHasSendVideo;
        SendVideo(bHasSendVideo);
        bool bHasSendAudio;
        SendAudio(bHasSendAudio);

        if ((!bHasSendVideo) && (!bHasSendAudio))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

int32_t RTSPServerSession::SendVideo(bool& bHasSend)
{
    std::shared_ptr<Packet> packet = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_VideoRtpPacketListLock);
        if (m_VideoRtpPacketList.size() > 0)
        {
            packet = m_VideoRtpPacketList.front();
            m_VideoRtpPacketList.pop_front();
        }
    }

    if (packet == nullptr)
    {
        bHasSend = false;
        return 0;
    }

    bHasSend = true;
    int nSendfd = m_nVideoRtpfd;

    int32_t size = 0;
    if (m_eVideoTransport == TCP)
    {
        nSendfd = m_nSessionfd;
        m_pSendBuff[0] = 0x24;
        m_pSendBuff[1] = 0x00;
        m_pSendBuff[2] = packet->m_nLength >> 8;
        m_pSendBuff[3] = packet->m_nLength & 0xff;
        size = 4;
    }
    memcpy(m_pSendBuff + size, packet->m_pData, packet->m_nLength);
    size += packet->m_nLength;

    int nSend = 0;
    int ret = 0;
    while (nSend < size)
    {
    send:        
        ret = send(nSendfd, m_pSendBuff + nSend, size - nSend, 0);
        if(ret == -1)
        {
            if (errno == EAGAIN)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                goto send;
            }
            Error("[%p][RTSPServerSession::SendVideo] send packet fail,errno:%d", this, errno);
            break;
        }
        else
        {
            nSend += ret;
        }
    }

    return 0;

}

int32_t RTSPServerSession::SendAudio(bool& bHasSend)
{
    bHasSend = false;
    return 0;
}

int32_t RTSPServerSession::EnableOSD(bool enable)
{
    if (m_pImageTransoprt == nullptr)
    {
        Trace("[%p][RTSPServerSession::EnableOSD] m_pImageTransoprt is null", this);
        m_bEnableOSD = enable;
        return 0;
    }

    if (enable)
    {
        Marker::Color color;
        color.A = 255 * 0; color.R = 255; color.G = 255; color.B = 255;

        m_pImageTransoprt->AddMarker("pitch");
        m_pImageTransoprt->SetMarkKey("pitch", "俯仰:", color, 25, g_nCaptureHeight / 2 - 100);
        m_pImageTransoprt->AddMarker("roll");
        m_pImageTransoprt->SetMarkKey("roll", "横滚:", color, 25, g_nCaptureHeight / 2);
        m_pImageTransoprt->AddMarker("yaw");
        m_pImageTransoprt->SetMarkKey("yaw", "偏航:", color, 25, g_nCaptureHeight / 2 + 100);

        m_pImageTransoprt->AddMarker("vol");
        m_pImageTransoprt->SetMarkKey("vol", "电压:", color, g_nCaptureWidth - 160, g_nCaptureHeight / 2 - 100);
        m_pImageTransoprt->AddMarker("cur");
        m_pImageTransoprt->SetMarkKey("cur", "电流:", color, g_nCaptureWidth - 160, g_nCaptureHeight / 2);
        m_pImageTransoprt->AddMarker("rem");
        m_pImageTransoprt->SetMarkKey("rem", "电量:", color, g_nCaptureWidth - 160, g_nCaptureHeight / 2 + 100);

        m_pImageTransoprt->AddMarker("lat");
        m_pImageTransoprt->SetMarkKey("lat", "纬度:", color, g_nCaptureWidth / 5 * 0 + 25, g_nCaptureHeight - 45);
        m_pImageTransoprt->AddMarker("lon");
        m_pImageTransoprt->SetMarkKey("lon", "经度:", color, g_nCaptureWidth / 5 * 1 + 25, g_nCaptureHeight - 45);
        m_pImageTransoprt->AddMarker("alt");
        m_pImageTransoprt->SetMarkKey("alt", "海拔:", color, g_nCaptureWidth / 5 * 2 + 25, g_nCaptureHeight - 45);
        m_pImageTransoprt->AddMarker("sat");
        m_pImageTransoprt->SetMarkKey("sat", "卫星:", color, g_nCaptureWidth / 5 * 3 + 25, g_nCaptureHeight - 45);
        m_pImageTransoprt->AddMarker("vel");
        m_pImageTransoprt->SetMarkKey("vel", "速度:", color, g_nCaptureWidth / 5 * 4 + 25, g_nCaptureHeight - 45);
    }
    else
    {
        m_pImageTransoprt->RemoveMarker("pitch");
        m_pImageTransoprt->RemoveMarker("roll");
        m_pImageTransoprt->RemoveMarker("yaw");
        m_pImageTransoprt->RemoveMarker("lat");
        m_pImageTransoprt->RemoveMarker("lon");
        m_pImageTransoprt->RemoveMarker("alt");
        m_pImageTransoprt->RemoveMarker("sat");
        m_pImageTransoprt->RemoveMarker("vel");
        m_pImageTransoprt->RemoveMarker("vol");
        m_pImageTransoprt->RemoveMarker("cur");
        m_pImageTransoprt->RemoveMarker("rem");
    }

    m_pImageTransoprt->EnableOSD(enable);
    Trace("[%p][RTSPServerSession::EnableOSD] enable osd:%d", this, enable);

    return 0;
}

int32_t RTSPServerSession::SetAttitude(float pitch, float roll, float yaw)
{
    if (m_bEnableOSD && m_pImageTransoprt != nullptr && m_pImageTransoprt->IsEnableOSD())
    {
        Marker::Color color;
        color.A = 255 * 0; color.R = 255; color.G = 255; color.B = 255;
        m_pImageTransoprt->SetMarkValue("pitch", std::to_string(pitch).substr(0, 6), color, 0, 0);
        m_pImageTransoprt->SetMarkValue("roll", std::to_string(roll).substr(0, 6), color, 0, 0);
        m_pImageTransoprt->SetMarkValue("yaw", std::to_string(yaw).substr(0, 6), color, 0, 0);
    }
    return 0;
}

int32_t RTSPServerSession::SetGPS(int32_t lat, int32_t lon, int32_t alt, uint8_t satellites, uint16_t vel)
{
    if (m_bEnableOSD && m_pImageTransoprt != nullptr && m_pImageTransoprt->IsEnableOSD())
    {
        Marker::Color color;
        color.A = 255 * 0; color.R = 255; color.G = 255; color.B = 255;
        m_pImageTransoprt->SetMarkValue("lat", std::to_string(0.0000001 * lat).substr(0, 9), color, 0, 0);
        m_pImageTransoprt->SetMarkValue("lon", std::to_string(0.0000001 * lon).substr(0, 9), color, 0, 0);
        m_pImageTransoprt->SetMarkValue("alt", std::to_string(0.001 * alt).substr(0, 6), color, 0, 0);
        m_pImageTransoprt->SetMarkValue("sat", std::to_string(satellites).substr(0, 6), color, 0, 0);
        m_pImageTransoprt->SetMarkValue("vel", std::to_string(0.01 * vel * 3.6).substr(0, 6) + "Km/h", color, 0, 0);
    }
    return 0;
}

int32_t RTSPServerSession::SetSysStatus(uint16_t voltage, int16_t current, int8_t batteryRemaining)
{
    if (m_bEnableOSD && m_pImageTransoprt != nullptr && m_pImageTransoprt->IsEnableOSD())
    {
        Marker::Color color;
        color.A = 255 * 0; color.R = 255; color.G = 255; color.B = 255;
        m_pImageTransoprt->SetMarkValue("vol", std::to_string(0.001 * voltage).substr(0, 6) + "V", color, 0, 0);
        m_pImageTransoprt->SetMarkValue("cur", std::to_string(0.01 * current).substr(0, 5) + "A", color, 0, 0);
        m_pImageTransoprt->SetMarkValue("rem", std::to_string(batteryRemaining) + "%", color, 0, 0);
    }
    return 0;
}