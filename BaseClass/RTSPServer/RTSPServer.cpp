#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include "RTSPServer.h"
#include "Log/Log.h"

RTSPServer::RTSPServer()
{
    m_nServerSocketfd = -1;
    m_bCloseServer = true;
    m_pServerThread = nullptr;
    m_bEnableOSD = false;
}

RTSPServer::~RTSPServer()
{
    ReleaseAll();
}

int32_t RTSPServer::ReleaseAll()
{
    m_bCloseServer = true;
    if (m_pServerThread != nullptr)
    {
        if (m_pServerThread->joinable())
        {
            m_pServerThread->join();
        }
        delete m_pServerThread;
        m_pServerThread = nullptr;
    }

    if (m_nServerSocketfd != -1)
    {
        close(m_nServerSocketfd);
        m_nServerSocketfd = -1;
    }

    {
        std::lock_guard<std::mutex> lock(m_RTSPServerSessionSetLock);
        for (auto& pSession : m_RTSPServerSessionSet)
        {
            if (pSession != nullptr)
            {
                delete pSession;
            }
        }
        m_RTSPServerSessionSet.clear();
    }
    m_bEnableOSD = false;

    return 0;
}

int32_t RTSPServer::OpenServer(uint16_t port)
{
    Trace("[%p][RTSPServer::OpenServer] OpenServer port:%d", this, port);

    if (m_nServerSocketfd != -1)
    {
        Error("[%p][RTSPServer::OpenServer] RTSPServer has been opened", this);
        return -1;
    }

    m_nServerSocketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_nServerSocketfd == -1)
    {
        Error("[%p][RTSPServer::OpenServer] open socket fail,errno:%d", this, errno);
        return -2;
    }

    int flags = fcntl(m_nServerSocketfd, F_GETFL, 0);
    int ret = fcntl(m_nServerSocketfd, F_SETFL, flags | O_NONBLOCK);
    if (ret == -1)
    {
        Error("[%p][RTSPServer::OpenServer] set NONBLOCK fail,errno:%d", this, errno);
        return -3;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    ret = bind(m_nServerSocketfd, (struct sockaddr*)&addr, sizeof(addr));
    if (ret == -1)
    {
        Error("[%p][RTSPServer::OpenServer] bind socket fail,errno:%d", this, errno);
        return -4;
    }

    m_bCloseServer = false;
    m_pServerThread = new std::thread(&RTSPServer::ServerThread, this);

    return 0;
}

int32_t RTSPServer::CloseServer()
{
    Trace("[%p][RTSPServer::CloseServer] CloseServer", this);
    return ReleaseAll();
}

void RTSPServer::RemoveFinishedSession()
{
    std::lock_guard<std::mutex> lock(m_RTSPServerSessionSetLock);
    for (auto it = m_RTSPServerSessionSet.begin(); it != m_RTSPServerSessionSet.end();)
    {
        if ((*it)->IsSessionFinished())
        {
            delete (*it);
            it = m_RTSPServerSessionSet.erase(it);
        }
        else
        {
            it++;
        }
    }
}

void RTSPServer::ServerThread()
{
    Trace("[%p][RTSPServer::ServerThread]  start ServerThread", this);

    int ret = listen(m_nServerSocketfd, SOMAXCONN);
    if (ret == -1)
    {
        Error("[%p][RTSPServer::ServerThread]  listen error:%d", this, errno);
        m_bCloseServer = true;
        return;
    }

    struct sockaddr clientAddr;
    socklen_t len = sizeof(clientAddr);
    while (!m_bCloseServer)
    {
        RemoveFinishedSession();
        int clientfd = accept(m_nServerSocketfd, (struct sockaddr*)&clientAddr, &len);
        if (clientfd == -1)
        {
            int err = errno;
            if (err != EAGAIN)
            {
                Error("[%p][RTSPServer::ServerThread]  accept error:%d", this, err);
                return;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        sockaddr_in* addr = (sockaddr_in*)&clientAddr;
        std::string strClientIP = inet_ntoa(addr->sin_addr);

        RTSPServerSession* pRTSPServerSession = new RTSPServerSession(clientfd, strClientIP);
        ret = pRTSPServerSession->StartSession();
        if (ret == 0)
        {
            pRTSPServerSession->EnableOSD(m_bEnableOSD);
            std::lock_guard<std::mutex> lock(m_RTSPServerSessionSetLock);
            m_RTSPServerSessionSet.insert(pRTSPServerSession);
        }
        else
        {
            Error("[%p][RTSPServer::ServerThread]  StartSession fail return:%d", this, ret);
            delete pRTSPServerSession;
        }
    }

    Trace("[%p][RTSPServer::ServerThread]  exit ServerThread", this);
}

int32_t RTSPServer::EnableOSD(bool enable)
{
    m_bEnableOSD = enable;
    std::lock_guard<std::mutex> lock(m_RTSPServerSessionSetLock);
    for (auto item : m_RTSPServerSessionSet)
    {
        item->EnableOSD(enable);
    }
    return 0;
}

int32_t RTSPServer::SetAttitude(float pitch, float roll, float yaw)
{
    std::lock_guard<std::mutex> lock(m_RTSPServerSessionSetLock);
    for (auto item : m_RTSPServerSessionSet)
    {
        item->SetAttitude(pitch, roll, yaw);
    }
    return 0;
}

int32_t RTSPServer::SetGPS(int32_t lat, int32_t lon, int32_t alt, uint8_t satellites, uint16_t vel)
{
    std::lock_guard<std::mutex> lock(m_RTSPServerSessionSetLock);
    for (auto item : m_RTSPServerSessionSet)
    {
        item->SetGPS(lat, lon, alt, satellites, vel);
    }
    return 0;
}

int32_t RTSPServer::SetSysStatus(uint16_t voltage, int16_t current, int8_t batteryRemaining)
{
    std::lock_guard<std::mutex> lock(m_RTSPServerSessionSetLock);
    for (auto item : m_RTSPServerSessionSet)
    {
        item->SetSysStatus(voltage, current, batteryRemaining);
    }
    return 0;
}