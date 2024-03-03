#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "UDPDataChannel.h"
#include "Log/Log.h"

#define MAX_LIST_MSG_SIZE 5

UDPDataChannel::UDPDataChannel()
{
    m_strProtocol = "udp";
    m_nPort = 0;
    m_strIp = "0.0.0.0";
    m_eWorkMode = WORK_MODE_NONE;
    m_nSocketFd = -1;
    m_pMsgCallback = nullptr;
    m_bExitMsgThread = true;
    m_pMsgThread = nullptr;
    m_bIsConnect = false;
}

UDPDataChannel::~UDPDataChannel()
{
    ReleaseAll();
}

int32_t UDPDataChannel::ReleaseAll()
{
    m_bExitMsgThread = true;
    if (m_pMsgThread != nullptr)
    {
        if (m_pMsgThread->joinable())
        {
            m_pMsgThread->join();
        }

        delete m_pMsgThread;
        m_pMsgThread = nullptr;;
    }

    if (m_nSocketFd != -1)
    {
        close(m_nSocketFd);
        m_nSocketFd = -1;
    }
    m_nPort = 0;
    m_strIp = "0.0.0.0";
    m_eWorkMode = WORK_MODE_NONE;
    m_bIsConnect = false;

    {
        std::lock_guard<std::mutex> lock(m_cMsgListLock);
        while (m_pMsgList.size() > 0)
        {
            auto packet = m_pMsgList.front();
            m_pMsgList.pop_front();
        }
    }

    return 0;
}

int32_t UDPDataChannel::CloseChannel()
{
    return ReleaseAll();
}

int32_t UDPDataChannel::SendMsg(std::shared_ptr<Packet> packet)
{
    {
        std::lock_guard<std::mutex> lock(m_cMsgListLock);
        if (m_pMsgList.size() > MAX_LIST_MSG_SIZE)
        {
            Error("[%p][UDPDataChannel::SendMsg]  too much message queue cachet", this);
            return -1;
        }
        m_pMsgList.push_back(packet);
    }
    return 0;
}

int32_t UDPDataChannel::SetMsgCallback(MsgCallback callback)
{
    m_pMsgCallback = callback;
    return 0;
}

int32_t UDPDataChannel::GetProtocol(std::string& protocol)
{
    protocol = m_strProtocol;
    return 0;
}

int32_t UDPDataChannel::Init(const std::string& ip, uint16_t port, WorkMode mode)
{
    int32_t ret = mode == WORK_AS_CLIENT ? InitAsClient(ip, port) :
        mode == WORK_AS_SERVER ? InitAsServer(ip, port) : INT32_MIN;
    if (ret != 0)
    {
        Error("[%p][UDPDataChannel::Init]  Init fail,WorkMode:%d return:%d", this, mode, ret);
        return -1;
    }

    m_nPort = port;
    m_strIp = ip;
    m_eWorkMode = mode;

    return 0;
}

int32_t UDPDataChannel::InitAsClient(const std::string& ip, uint16_t port)
{
    int ret = 0;
    int flags = 0;
    struct sockaddr_in addr;
    int len = 0;
    if (m_nSocketFd != -1 || m_pMsgThread != nullptr)
    {
        Error("[%p][UDPDataChannel::InitAsClient]  data channel already running", this);
        ret = -1; goto fail;
    }
    if (m_pMsgCallback == nullptr)
    {
        Error("[%p][UDPDataChannel::InitAsClient]  MsgCallback is null", this);
        ret = -2; goto fail;
    }

    m_nSocketFd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_nSocketFd == -1)
    {
        Error("[%p][UDPDataChannel::InitAsClient] new socket fail,error:%d", this, errno);
        ret = -3; goto fail;
    }

    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    len = sizeof(addr);
    ret = connect(m_nSocketFd, (sockaddr*)&addr, len);
    if (ret == -1)
    {
        Error("[%p][UDPDataChannel::InitAsClient] connect socket fail,error:%d", this, errno);
        ret = -4; goto fail;
    }

    flags = fcntl(m_nSocketFd, F_GETFL, 0);
    ret = fcntl(m_nSocketFd, F_SETFL, flags | O_NONBLOCK);
    if (ret == -1)
    {
        Error("[%p][UDPDataChannel::InitAsClient] set NONBLOCK fail,error:%d", this, errno);
        ret = -5; goto fail;
    }
    m_bIsConnect = true;

    m_bExitMsgThread = false;
    m_pMsgThread = new std::thread(&UDPDataChannel::MsgThread, this);

    return 0;
fail:
    ReleaseAll();
    return ret;
}

int32_t UDPDataChannel::InitAsServer(const std::string& ip, uint16_t port)
{
    int ret = 0;
    int flags = 0;
    struct sockaddr_in addr;
    int len = 0;
    if (m_nSocketFd != -1 || m_pMsgThread != nullptr)
    {
        Error("[%p][UDPDataChannel::InitAsServer]  data channel already running", this);
        ret = -1; goto fail;
    }
    if (m_pMsgCallback == nullptr)
    {
        Error("[%p][UDPDataChannel::InitAsServer]  MsgCallback is null", this);
        ret = -2; goto fail;
    }

    m_nSocketFd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_nSocketFd == -1)
    {
        Error("[%p][UDPDataChannel::InitAsServer] new socket fail,error:%d", this, errno);
        ret = -3; goto fail;
    }

    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    len = sizeof(addr);
    ret = bind(m_nSocketFd, (sockaddr*)&addr, len);
    if (ret == -1)
    {
        Error("[%p][UDPDataChannel::InitAsServer] bind socket fail,error:%d", this, errno);
        ret = -4; goto fail;
    }
    flags = fcntl(m_nSocketFd, F_GETFL, 0);
    ret = fcntl(m_nSocketFd, F_SETFL, flags | O_NONBLOCK);
    if (ret == -1)
    {
        Error("[%p][UDPDataChannel::InitAsServer] set NONBLOCK fail,error:%d", this, errno);
        ret = -5; goto fail;
    }

    m_bExitMsgThread = false;
    m_pMsgThread = new std::thread(&UDPDataChannel::MsgThread, this);

    return 0;
fail:
    ReleaseAll();
    return ret;
}

int32_t UDPDataChannel::MsgThread()
{
    ssize_t len = 0;
    const size_t nBuffSize = 4 * 1024;
    uint8_t* pBuff = (uint8_t*)malloc(nBuffSize);
    if (pBuff == nullptr)
    {
        Error("[%p][UDPDataChannel::MsgThread]  malloc buff0 faii,size:%d", this, nBuffSize);
        return -1;
    }

    bool isBusy = false;
    struct sockaddr addr = { 0 };
    socklen_t addr_len = sizeof(addr);
    while (!m_bExitMsgThread)
    {
        isBusy = false;
        if (m_bIsConnect)
        {
            len = recv(m_nSocketFd, pBuff, nBuffSize, 0);
            if (len < 0)
            {
                int err = errno;
                if (err != EAGAIN)
                {
                    if (m_eWorkMode == WORK_AS_SERVER)
                    {
                        int ret = ReInitAsServer(m_strIp,m_nPort);
                        if (ret != 0)
                        {
                            Error("[%p][UDPDataChannel::MsgThread] ReInitAsServer fail,return:%d", this, ret);
                            return -1;
                        }
                        m_bIsConnect = false;
                        Trace("[%p][UDPDataChannel::MsgThread] disconnect to remote", this);
                    }
                }
            }
        }
        else
        {
            len = recvfrom(m_nSocketFd, pBuff, nBuffSize, 0, &addr, &addr_len);
            if (len > 0)
            {
                if (!m_bIsConnect)
                {
                    if (connect(m_nSocketFd, &addr, addr_len) != 0)
                    {
                        Error("[%p][UDPDataChannel::MsgThread] connect socket fail,error:%d", this, errno);
                    }
                    else
                    {
                        m_bIsConnect = true;
                        Trace("[%p][UDPDataChannel::MsgThread] connect to remote", this);
                    }
                }
            }
        }

        if (len > 0)
        {
            std::shared_ptr<Packet> packet = std::make_shared<Packet>();
            packet->m_pData = (uint8_t*)malloc(len);
            if (packet->m_pData == nullptr)
            {
                Error("[%p][UDPDataChannel::SendMsg]  malloc data faii,size:%d", this, len);
                continue;
            }
            memcpy(packet->m_pData, pBuff, len);
            packet->m_nLength = len;

            m_pMsgCallback(packet);
            isBusy = true;
        }

        {
            std::lock_guard<std::mutex> lock(m_cMsgListLock);
            while (m_pMsgList.size() > 0)
            {
                auto packet = m_pMsgList.front();
                m_pMsgList.pop_front();
                if (m_bIsConnect)
                {
                    len = send(m_nSocketFd, packet->m_pData, packet->m_nLength, 0);
                }
            }
        }

        if (!isBusy)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    free(pBuff);
    return 0;
}

int32_t UDPDataChannel::ReInitAsServer(const std::string& ip, uint16_t port)
{
    close(m_nSocketFd);
    m_nSocketFd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_nSocketFd == -1)
    {
        Error("[%p][UDPDataChannel::ReInitAsServer] new socket fail,error:%d", this, errno);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_nPort);
    addr.sin_addr.s_addr = inet_addr(m_strIp.c_str());
    size_t len = sizeof(addr);
    int ret = bind(m_nSocketFd, (sockaddr*)&addr, len);
    if (ret == -1)
    {
        Error("[%p][UDPDataChannel::ReInitAsServer] bind socket fail,error:%d", this, errno);
        return -2;
    }
    int flags = fcntl(m_nSocketFd, F_GETFL, 0);
    ret = fcntl(m_nSocketFd, F_SETFL, flags | O_NONBLOCK);
    if (ret == -1)
    {
        Error("[%p][UDPDataChannel::ReInitAsServer] set NONBLOCK fail,error:%d", this, errno);
        return  -3;
    }

    return 0;
}