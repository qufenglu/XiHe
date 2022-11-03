#pragma once
#include <thread>
#include <mutex>
#include <list>
#include "DataChannel.h"

class UARTDataChannel : public DataChannel
{
public:
    UARTDataChannel();
    virtual ~UARTDataChannel();

    virtual int32_t SendMsg(std::shared_ptr<Packet> packet);
    virtual int32_t SetMsgCallback(MsgCallback callback);
    virtual int32_t GetProtocol(std::string& protocol);
    virtual int32_t CloseChannel();
    int32_t Init(uint32_t baud);

private:
    int32_t ReleaseAll();
    int32_t MsgThread();

private:
    std::string m_strProtocol;
    int m_nSerialFd;
    MsgCallback m_pMsgCallback;
    bool m_bExitMsgThread;
    std::thread* m_pMsgThread;
    uint32_t m_nBaudRate;

    std::mutex m_cMsgListLock;
    std::list<std::shared_ptr<Packet>> m_pMsgList;
};
