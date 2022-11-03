#pragma once
#include <thread>
#include <mutex>
#include <list>
#include "DataChannel.h"

class UDPDataChannel : public DataChannel
{
public:
    enum WorkMode
    {
        WORK_MODE_NONE = 0,
        WORK_AS_CLIENT,
        WORK_AS_SERVER
    };
    typedef struct UDPDataChannelInitParam
    {
        std::string ip = "0.0.0.0";
        uint16_t port = 0;
        WorkMode mode = WORK_MODE_NONE;
    }UDPDataChannelInitParam;

public:
    UDPDataChannel();
    virtual ~UDPDataChannel();

    virtual int32_t SendMsg(std::shared_ptr<Packet> packet);
    virtual int32_t SetMsgCallback(MsgCallback callback);
    virtual int32_t GetProtocol(std::string& protocol);
    virtual int32_t CloseChannel();
    int32_t Init(const std::string& ip, uint16_t port, WorkMode mode);

private:
    int32_t InitAsClient(const std::string& ip, uint16_t port);
    int32_t InitAsServer(const std::string& ip, uint16_t port);
    int32_t ReInitAsServer(const std::string& ip, uint16_t port);
    int32_t ReleaseAll();
    int32_t MsgThread();

private:
    std::string m_strProtocol;
    uint16_t m_nPort;
    std::string m_strIp;
    int m_nSocketFd;
    WorkMode m_eWorkMode;
    MsgCallback m_pMsgCallback;
    bool m_bExitMsgThread;
    std::thread* m_pMsgThread;

    std::mutex m_cMsgListLock;
    std::list<std::shared_ptr<Packet>> m_pMsgList;
    bool m_bIsConnect = false;
};
