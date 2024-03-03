#include <unistd.h>
#include <wiringPi.h>
#include <wiringSerial.h>
#include <fcntl.h>
#include "UARTDataChannel.h"
#include "Log/Log.h"
#include "string.h"

#define MAX_LIST_MSG_SIZE 5

UARTDataChannel::UARTDataChannel()
{
    m_strProtocol = "uart";
    m_nSerialFd = -1;
    m_pMsgCallback = nullptr;
    m_bExitMsgThread = true;
    m_pMsgThread = nullptr;
    m_nBaudRate = 0;
}

UARTDataChannel::~UARTDataChannel()
{
    ReleaseAll();
}

int32_t UARTDataChannel::ReleaseAll()
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

    if (m_nSerialFd != -1)
    {
        serialClose(m_nSerialFd);
        m_nSerialFd = -1;
    }
    m_nBaudRate = 0;

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

int32_t UARTDataChannel::CloseChannel()
{
    return ReleaseAll();
}

int32_t UARTDataChannel::SendMsg(std::shared_ptr<Packet> packet)
{
    {
        std::lock_guard<std::mutex> lock(m_cMsgListLock);
        if (m_pMsgList.size() > MAX_LIST_MSG_SIZE)
        {
            Error("[%p][UARTDataChannel::SendMsg]  too much message queue cachet", this);
            return -1;
        }
        m_pMsgList.push_back(packet);
    }
    return 0;
}

int32_t UARTDataChannel::SetMsgCallback(MsgCallback callback)
{
    m_pMsgCallback = callback;
    return 0;
}

int32_t UARTDataChannel::GetProtocol(std::string& protocol)
{
    protocol = m_strProtocol;
    return 0;
}

int32_t UARTDataChannel::Init(uint32_t baud)
{
    int ret = 0;
    int flags = 0;
    if (m_nSerialFd != -1 || m_pMsgThread != nullptr)
    {
        Error("[%p][UARTDataChannel::Init]  data channel already running", this);
        ret = -1;
        goto fail;
    }

    if (m_pMsgCallback == nullptr)
    {
        Error("[%p][UARTDataChannel::Init]  MsgCallback is null", this);
        ret = -2;
        goto fail;
    }

    if (wiringPiSetup() == -1)
    {
        Error("[%p][UARTDataChannel::Init]  setup wiringPi fail", this);
        ret = -3;
        goto fail;
    }
    m_nSerialFd = serialOpen("/dev/ttyAMA0", baud);
    if (m_nSerialFd == -1)
    {
        Error("[%p][UARTDataChannel::Init]  open setup fail", this);
        ret = -4;
        goto fail;
    }
    flags = fcntl(m_nSerialFd, F_GETFL, 0);
    if (fcntl(m_nSerialFd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        Error("[%p][UARTDataChannel::Init] set NONBLOCK fail,errno:%d", this, errno);
        ret = -5;
        goto fail;
    }

    m_bExitMsgThread = false;
    m_pMsgThread = new std::thread(&UARTDataChannel::MsgThread, this);

    return 0;
fail:
    ReleaseAll();
    return ret;
}

int32_t UARTDataChannel::MsgThread()
{
    int len = 0;
    const size_t nBuffSize = 4 * 1024;
    uint8_t* pBuff = (uint8_t*)malloc(nBuffSize);
    if (pBuff == nullptr)
    {
        Error("[%p][UARTDataChannel::MsgThread]  malloc buff0 faii,size:%d", this, nBuffSize);
        return -1;
    }

    bool isBusy = false;
    while (!m_bExitMsgThread)
    {
        isBusy = false;
        len = read(m_nSerialFd, pBuff, nBuffSize);
        if (len > 0)
        {
            std::shared_ptr<Packet> packet = std::make_shared<Packet>();
            packet->m_pData = (uint8_t*)malloc(len);
            if (packet->m_pData == nullptr)
            {
                Error("[%p][UARTDataChannel::SendMsg]  malloc data faii,size:%d", this, len);
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
                len = write(m_nSerialFd, packet->m_pData, packet->m_nLength);
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