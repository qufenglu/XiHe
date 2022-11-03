#include"XiheClient.h"
#include "Log/Log.h"

XIheClient::XIheClient(const std::string& ip, const uint16_t port)
{
    m_strRemoteIp = ip;
    m_nRemotePort = port;
    m_pRTSPClient = nullptr;
    m_pVideoDecoder = nullptr;
    m_pVideoFrameCallback = nullptr;
    m_pDigitalTransport = nullptr;
    m_pControllertMsgCallback = nullptr;
    m_pRemoteMsgCallback = nullptr;
}

XIheClient::~XIheClient()
{
    ReleaseAll();
}

int32_t XIheClient::ReleaseAll()
{
    CloseDigitalTransport();

    delete m_pRTSPClient;
    m_pRTSPClient = nullptr;

    {
        std::lock_guard<std::mutex> lock(m_pVideoDecoderLock);
        delete m_pVideoDecoder;
        m_pVideoDecoder = nullptr;
    }

    return 0;
}

int32_t XIheClient::PlayDevice(const std::string& device)
{
    if (m_pRTSPClient == nullptr)
    {
        m_pRTSPClient = new RTSPClient();
        RTPParser::MediaPacketCallbaclk pVideoCallback = std::bind(&XIheClient::OnRecvVideoPacket, this, std::placeholders::_1);
        m_pRTSPClient->SetVideoPacketCallbaclk(pVideoCallback);
        RTSPClient::VideoReadyCallbaclk pVideoReadyCallbaclk = std::bind(&XIheClient::OnVideoReady, this, std::placeholders::_1);
        m_pRTSPClient->SetVideoReadyCallbaclk(pVideoReadyCallbaclk);
    }

    char url[1024];
    sprintf(url, "rtsp://%s:%d/device/%s", m_strRemoteIp.c_str(), m_nRemotePort, device.c_str());
    int ret = m_pRTSPClient->PlayUrl(url);
    if (ret != 0)
    {
        Error("[%p][XIheClient::PlayDevice] PlayUrl fail.return:%d", this, ret);
        return -1;
    }

    return 0;
}

int32_t XIheClient::PlayFile(const std::string& file)
{
    if (m_pRTSPClient == nullptr)
    {
        m_pRTSPClient = new RTSPClient();
        RTPParser::MediaPacketCallbaclk pVideoCallback = std::bind(&XIheClient::OnRecvVideoPacket, this, std::placeholders::_1);
        m_pRTSPClient->SetVideoPacketCallbaclk(pVideoCallback);
        RTSPClient::VideoReadyCallbaclk pVideoReadyCallbaclk = std::bind(&XIheClient::OnVideoReady, this, std::placeholders::_1);
        m_pRTSPClient->SetVideoReadyCallbaclk(pVideoReadyCallbaclk);
    }

    char url[1024];
    sprintf(url, "rtsp://%s:%d/file/%s", m_strRemoteIp.c_str(), m_nRemotePort, file.c_str());
    int ret = m_pRTSPClient->PlayUrl(url);
    if (ret != 0)
    {
        Error("[%p][XIheClient::PlayFile] PlayUrl fail.return:%d", this, ret);
        return -1;
    }

    return 0;
}

void XIheClient::OnRecvVideoPacket(std::shared_ptr<MediaPacket>& video)
{
    std::lock_guard<std::mutex> lock(m_pVideoDecoderLock);
    if (m_pVideoDecoder != nullptr)
    {
        m_pVideoDecoder->RecvVideoPacket(video);
    }
}

void XIheClient::OnRecvVideoFrame(std::shared_ptr<VideoFrame>& video)
{
    if (m_pVideoFrameCallback != nullptr)
    {
        m_pVideoFrameCallback(video);
    }
}

void XIheClient::OnVideoReady(const VideoInfo& info)
{
    std::lock_guard<std::mutex> lock(m_pVideoDecoderLock);
    delete m_pVideoDecoder;
    m_pVideoDecoder = new VideoDecoder();
    int ret = m_pVideoDecoder->AddVideoStream(info);
    if (ret != 0)
    {
        Error("[%p][XIheClient::PlayFile] PlayUrl fail.return:%d", this, ret);
        delete m_pVideoDecoder;
        m_pVideoDecoder = nullptr;
        return;
    }
    m_pVideoDecoder->SetVideoFrameCallBack(m_pVideoFrameCallback);
}

int32_t XIheClient::SetVideoFrameCallback(VideoDecoder::VideoFrameCallbaclk callback)
{
    m_pVideoFrameCallback = callback;
    {
        std::lock_guard<std::mutex> lock(m_pVideoDecoderLock);
        if (m_pVideoDecoder != nullptr)
        {
            m_pVideoDecoder->SetVideoFrameCallBack(m_pVideoFrameCallback);
        }
    }

    return 0;
}

int32_t XIheClient::OpenDigitalTransport()
{
    if (m_pDigitalTransport != nullptr)
    {
        Error("[%p][XIheClient::OpenDigitalTransport]  DigitalTransport is already open", this);
        return -1;
    }

    m_pDigitalTransport = new DigitalTransport();
    return 0;
}

int32_t XIheClient::CloseDigitalTransport()
{
    delete m_pDigitalTransport;
    m_pDigitalTransport = nullptr;
    return 0;
}

int32_t XIheClient::InitControllerTransport(const std::string protocol, void* param)
{
    if (m_pDigitalTransport == nullptr)
    {
        Error("[%p][XIheClient::InitControllerTransport]  DigitalTransport is not open", this);
        return -1;
    }

    DataChannel::MsgCallback callbback = std::bind(&XIheClient::OnRecvMsgFromeController, this, std::placeholders::_1);
    int ret = m_pDigitalTransport->InitControllerChannel(protocol, param, callbback);
    if (ret != 0)
    {
        Error("[%p][XIheClient::InitControllerTransport]  init controller channel fail,return:%d", this, ret);
        return -2;
    }

    return 0;
}

int32_t XIheClient::InitRemoteTransport(const std::string protocol, void* param)
{
    if (m_pDigitalTransport == nullptr)
    {
        Error("[%p][XIheClient::InitRemoteTransport]  DigitalTransport is not open", this);
        return -1;
    }

    DataChannel::MsgCallback callbback = std::bind(&XIheClient::OnRecvMsgFromeRemote, this, std::placeholders::_1);
    int ret = m_pDigitalTransport->InitRemoteChannel(protocol, param, callbback);
    if (ret != 0)
    {
        Error("[%p][XIheClient::InitRemoteTransport]  init remote channel fail,return:%d", this, ret);
        return -2;
    }

    return 0;
}

int32_t XIheClient::StartTransport()
{
    if (m_pDigitalTransport == nullptr)
    {
        Error("[%p][XIheClient::StartTransport]  DigitalTransport is not open", this);
        return -1;
    }

    int ret = m_pDigitalTransport->StartTransport();
    if (ret != 0)
    {
        Error("[%p][XIheClient::StartTransport]  start transport fail,return:%d", this, ret);
        return -2;
    }

    return 0;
}

void XIheClient::OnRecvMsgFromeController(std::shared_ptr<Packet> packet)
{
    if (m_pControllertMsgCallback != nullptr)
    {
        ProcessMsg(packet, m_pControllertMsgCallback);
    }
}

void XIheClient::OnRecvMsgFromeRemote(std::shared_ptr<Packet> packet)
{
    if (m_pRemoteMsgCallback != nullptr)
    {
        ProcessMsg(packet, m_pRemoteMsgCallback);
    }
}

int32_t XIheClient::SetControllertMsgCallback(DigitalTransportMsgCallback callback)
{
    m_pControllertMsgCallback = callback;
    return 0;
}

int32_t XIheClient::SetRemoteMsgCallback(DigitalTransportMsgCallback callback)
{
    m_pRemoteMsgCallback = callback;
    return 0;
}

void XIheClient::ProcessMsg(const std::shared_ptr<Packet>& packet, const DigitalTransportMsgCallback& callback)
{
    mavlink_message_t msg;
    mavlink_status_t status;
    for (int i = 0; i < packet->m_nLength; i++)
    {
        if (mavlink_parse_char(MAVLINK_COMM_0, packet->m_pData[i], &msg, &status))
        {
            callback(&msg);
        }
    }
}