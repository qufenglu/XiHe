#include "XiheServer.h"
#include "Log/Log.h"
#include "DigitalTransport/mavlink/ardupilotmega/mavlink.h"

#define Pi 3.1415926f

XiheServer::XiheServer()
{
    m_pRTSPServer = nullptr;
    m_pDigitalTransport = nullptr;
}

XiheServer::~XiheServer()
{
    ReleaseAll();
}

int32_t XiheServer::ReleaseAll()
{
    CloseRTSPServer();
    CloseDigitalTransport();
    return 0;
}

int32_t XiheServer::OpenRTSPServer(uint16_t port)
{
    if (m_pRTSPServer != nullptr)
    {
        Error("[%p][XiheServer::OpenRTSPServer]  RTSPServer is already open", this);
        return -1;
    }

    int ret = 0;
    m_pRTSPServer = new RTSPServer();
    ret = m_pRTSPServer->OpenServer(7777);
    if (ret != 0)
    {
        Error("[%p][XiheServer::OpenRTSPServer]  OpenServer fail,return:%d", this, ret);
        ret = -2; goto fail;
    }
    m_pRTSPServer->EnableOSD(true);

    return 0;
fail:
    CloseRTSPServer();
    return ret;
}

int32_t XiheServer::CloseRTSPServer()
{
    delete m_pRTSPServer;
    m_pRTSPServer = nullptr;
    return 0;
}

int32_t XiheServer::OpenDigitalTransport()
{
    if (m_pDigitalTransport != nullptr)
    {
        Error("[%p][XiheServer::OpenDigitalTransport]  DigitalTransport is already open", this);
        return -1;
    }

    m_pDigitalTransport = new DigitalTransport();
    return 0;
}

int32_t XiheServer::CloseDigitalTransport()
{
    delete m_pDigitalTransport;
    m_pDigitalTransport = nullptr;
    return 0;
}

int32_t XiheServer::InitControllerTransport(const std::string protocol, void* param)
{
    if (m_pDigitalTransport == nullptr)
    {
        Error("[%p][XiheServer::InitControllerTransport]  DigitalTransport is not open", this);
        return -1;
    }

    DataChannel::MsgCallback callbback = std::bind(&XiheServer::OnRecvMsgFromeController, this, std::placeholders::_1);
    int ret = m_pDigitalTransport->InitControllerChannel(protocol, param, callbback);
    if (ret != 0)
    {
        Error("[%p][XiheServer::InitControllerTransport]  init controller channel fail,return:%d", this, ret);
        return -2;
    }
    m_pDigitalTransport->RequestOSD();

    return 0;
}

int32_t XiheServer::InitRemoteTransport(const std::string protocol, void* param)
{
    if (m_pDigitalTransport == nullptr)
    {
        Error("[%p][XiheServer::InitRemoteTransport]  DigitalTransport is not open", this);
        return -1;
    }

    DataChannel::MsgCallback callbback = std::bind(&XiheServer::OnRecvMsgFromeRemote, this, std::placeholders::_1);
    int ret = m_pDigitalTransport->InitRemoteChannel(protocol, param, callbback);
    if (ret != 0)
    {
        Error("[%p][XiheServer::InitRemoteTransport]  init remote channel fail,return:%d", this, ret);
        return -2;
    }

    return 0;
}

int32_t XiheServer::StartTransport()
{
    if (m_pDigitalTransport == nullptr)
    {
        Error("[%p][XiheServer::StartTransport]  DigitalTransport is not open", this);
        return -1;
    }

    int ret = m_pDigitalTransport->StartTransport();
    if (ret != 0)
    {
        Error("[%p][XiheServer::StartTransport]  start transport fail,return:%d", this, ret);
        return -2;
    }

    return 0;
}

void XiheServer::OnRecvMsgFromeController(std::shared_ptr<Packet> paclet)
{
    m_pDigitalTransport->SendMsg2Remote(paclet);

    mavlink_message_t msg;
    mavlink_status_t status;
    for (int i = 0; i < paclet->m_nLength; i++)
    {
        if (mavlink_parse_char(MAVLINK_COMM_0, paclet->m_pData[i], &msg, &status))
        {
            int ret = msg.msgid == MAVLINK_MSG_ID_VFR_HUD ? OnRecvHUDMsg(msg) :
                msg.msgid == MAVLINK_MSG_ID_ATTITUDE ? OnRecvAttitudeMsg(msg) :
                msg.msgid == MAVLINK_MSG_ID_HEARTBEAT ? OnRecvHeartbeat(msg) :
                msg.msgid == MAVLINK_MSG_ID_GPS_RAW_INT ? OnRecvGPSRaw(msg) :
                msg.msgid == MAVLINK_MSG_ID_SYS_STATUS ? OnSysStatus(msg) :
                msg.msgid == MAVLINK_MSG_ID_RC_CHANNELS ? OnRcChannels(msg) : 0;
            if (ret != 0)
            {
                Error("[%p][DigitalTransport::OnRecvMsgFromeController]  process mag fail,return:%d", this, ret);
            }
            //Trace("[%p][DigitalTransport::OnRecvMsgFromeController]  id:%d", this, msg.msgid);
        }
    }
}

void XiheServer::OnRecvMsgFromeRemote(std::shared_ptr<Packet> paclet)
{
    if (m_pDigitalTransport != nullptr)
    {
        m_pDigitalTransport->SendMsg2Controller(paclet);
    }
}

int32_t XiheServer::OnRecvHUDMsg(const mavlink_message_t& msg)
{
    mavlink_vfr_hud_t hud;
    mavlink_msg_vfr_hud_decode(&msg, &hud);
    return 0;
}

int32_t XiheServer::OnRecvAttitudeMsg(const mavlink_message_t& msg)
{
    mavlink_attitude_t attitude;
    mavlink_msg_attitude_decode(&msg, &attitude);

    float pitch = attitude.pitch * 180.0f / Pi;
    //pitch = pitch < 0 ? pitch + 360 : pitch;
    float roll = attitude.roll * 180.0f / Pi;
    //roll = roll < 0 ? roll + 360 : roll;
    float yaw = attitude.yaw * 180.0f / Pi;
   // yaw = yaw < 0 ? yaw + 360 : yaw;
    if (m_pRTSPServer != nullptr)
    {
        m_pRTSPServer->SetAttitude(pitch, roll, yaw);
    }

    //Trace("[%p][DigitalTransport::OnRecvATTITUDEMsg]  pitch:%f roll:%f yaw:%f", this, pitch, roll, yaw);
    return 0;
}

int32_t XiheServer::OnRecvHeartbeat(const mavlink_message_t& msg)
{
    mavlink_heartbeat_t heartbeat;
    mavlink_msg_heartbeat_decode(&msg, &heartbeat);
    return 0;
}

int32_t XiheServer::OnRecvGPSRaw(const mavlink_message_t& msg)
{
    mavlink_gps_raw_int_t gps;
    mavlink_msg_gps_raw_int_decode(&msg, &gps);
    if (m_pRTSPServer != nullptr)
    {
        m_pRTSPServer->SetGPS(gps.lat, gps.lon, gps.alt, gps.satellites_visible, gps.vel);
    }
    return 0;
}

int32_t XiheServer::OnSysStatus(const mavlink_message_t& msg)
{
    mavlink_sys_status_t status;
    mavlink_msg_sys_status_decode(&msg, &status);
    if (m_pRTSPServer != nullptr)
    {
        m_pRTSPServer->SetSysStatus(status.voltage_battery, status.current_battery, status.battery_remaining);
    }
    return 0;
}

int32_t XiheServer::OnRcChannels(const mavlink_message_t& msg)
{
    mavlink_rc_channels_t channels;
    mavlink_msg_rc_channels_decode(&msg, &channels);
    return 0;
}