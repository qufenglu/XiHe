#pragma once
#include <thread>
#include <mutex>
#include <list>
#include "DataChannel.h"
#include "UARTDataChannel.h"
#include "UDPDataChannel.h"
#include "CommonTools/TimeCounter.h"

class DigitalTransport
{
public:
    DigitalTransport();
    ~DigitalTransport();
    int32_t InitControllerChannel(const std::string& protocol, void* param, const DataChannel::MsgCallback &calbbback);
    int32_t InitRemoteChannel(const std::string& protocol, void* param, const DataChannel::MsgCallback& calbbback);
    int32_t StartTransport();
    int32_t StopTransport();
    int32_t SendMsg2Controller(std::shared_ptr<Packet> paclet);
    int32_t SendMsg2Remote(std::shared_ptr<Packet> paclet);
    int32_t RequestOSD();

private:
    int32_t ReleaseAll();
    void SendHeartbeatMsg(std::shared_ptr<Packet> paclet);
    int32_t TransportThread();

private:
    bool m_bStopTransport;
    std::thread* m_pTransportThread;

    std::mutex m_pDataChannel4ControllerLock;
    DataChannel* m_pDataChannel4Controller;
    std::mutex m_pControllerMsgListLock;
    std::list<std::shared_ptr<Packet>> m_pControllerMsgList;

    std::mutex m_pDataChannel4RemoteLock;
    DataChannel* m_pDataChannel4Remote;
    std::mutex m_pRemoteMsgListLock;
    std::list<std::shared_ptr<Packet>> m_pRemoteMsgList;
    TimeCounter m_cHeartbeatTimer;
};