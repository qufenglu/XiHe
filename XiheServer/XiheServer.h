#pragma once
#include "RTSPServer/RTSPServer.h"
#include "DigitalTransport/DigitalTransport.h"
#include "DigitalTransport/mavlink/mavlink_types.h"

class XiheServer
{
public:
    XiheServer();
    ~XiheServer();

    int32_t OpenRTSPServer(uint16_t port);
    int32_t CloseRTSPServer();
    int32_t OpenDigitalTransport();
    int32_t CloseDigitalTransport();
    int32_t InitControllerTransport(const std::string protocol, void* param);
    int32_t InitRemoteTransport(const std::string protocol, void* param);
    int32_t StartTransport();

private:
    int32_t ReleaseAll();
    void OnRecvMsgFromeController(std::shared_ptr<Packet> paclet);
    void OnRecvMsgFromeRemote(std::shared_ptr<Packet> paclet);
    int32_t OnRecvHUDMsg(const mavlink_message_t& msg);
    int32_t OnRecvAttitudeMsg(const mavlink_message_t& msg);
    int32_t OnRecvHeartbeat(const mavlink_message_t& msg);
    int32_t OnRecvGPSRaw(const mavlink_message_t& msg);
    int32_t OnSysStatus(const mavlink_message_t& msg);
    int32_t OnRcChannels(const mavlink_message_t& msg);

private:
    RTSPServer* m_pRTSPServer;
    DigitalTransport* m_pDigitalTransport;
};
