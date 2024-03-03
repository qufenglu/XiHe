#include <map>
#include "DigitalTransport.h"
#include "Log/Log.h"
#include "mavlink/ardupilotmega/mavlink.h"
#include "mavlink/common/common.h"

#define HEART_BEAT_TICKET 1000.0
#define BUFFER_LENGTH 2041
#define MAX_LIST_MSG_SIZE 5

DigitalTransport::DigitalTransport()
{
    m_bStopTransport = true;
    m_pTransportThread = nullptr;
    m_pDataChannel4Controller = nullptr;
    m_pDataChannel4Remote = nullptr;
}

DigitalTransport::~DigitalTransport()
{
    ReleaseAll();
}

int32_t DigitalTransport::ReleaseAll()
{
    m_bStopTransport = true;
    if (m_pTransportThread != nullptr)
    {
        if (m_pTransportThread->joinable())
        {
            m_pTransportThread->join();
        }

        delete m_pTransportThread;
        m_pTransportThread = nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(m_pControllerMsgListLock);
        while (m_pControllerMsgList.size() > 0)
        {
            std::shared_ptr<Packet> packet = m_pControllerMsgList.front();
            m_pControllerMsgList.pop_front();
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_pRemoteMsgListLock);
        while (m_pRemoteMsgList.size() > 0)
        {
            std::shared_ptr<Packet> packet = m_pRemoteMsgList.front();
            m_pRemoteMsgList.pop_front();
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_pDataChannel4ControllerLock);
        delete m_pDataChannel4Controller;
        m_pDataChannel4Controller = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(m_pDataChannel4RemoteLock);
        delete m_pDataChannel4Remote;
        m_pDataChannel4Remote = nullptr;
    }

    return 0;
}

static DataChannel* InitDataChannel(const std::string& protocol, void* param, DataChannel::MsgCallback calbbback)
{
    DataChannel* channel = nullptr;
    if (protocol == "uart")
    {
        channel = new UARTDataChannel();
        channel->SetMsgCallback(calbbback);
        int baud = *(int*)param;
        int ret = ((UARTDataChannel*)channel)->Init(baud);
        if (ret != 0)
        {
            Error("[InitDataChannel]  init channel fail,return:%d", ret);
            goto fail;
        }
    }
    else if (protocol == "udp")
    {
        channel = new UDPDataChannel();
        channel->SetMsgCallback(calbbback);
        UDPDataChannel::UDPDataChannelInitParam* initp = (UDPDataChannel::UDPDataChannelInitParam*)param;
        int ret = ((UDPDataChannel*)channel)->Init(initp->ip, initp->port, initp->mode);
        if (ret != 0)
        {
            Error("[InitDataChannel]  init channel fail,return:%d", ret);
            goto fail;
        }
    }
    else
    {
        Error("[InitDataChannel] not suppotr protocol:%s", protocol.c_str());
        goto fail;
    }

    return channel;
fail:
    delete channel;
    return nullptr;
}

int32_t DigitalTransport::InitControllerChannel(const std::string& protocol, void* param, const DataChannel::MsgCallback& calbbback)
{
    std::lock_guard<std::mutex> lock(m_pDataChannel4ControllerLock);
    delete m_pDataChannel4Controller;

    m_pDataChannel4Controller = InitDataChannel(protocol, param, calbbback);
    if (m_pDataChannel4Controller == nullptr)
    {
        Error("[%p][DigitalTransport::InitControllerChannel] InitDataChannel fali", this);
        return -1;
    }

    return 0;
}

int32_t DigitalTransport::InitRemoteChannel(const std::string& protocol, void* param, const DataChannel::MsgCallback& calbbback)
{
    std::lock_guard<std::mutex> lock(m_pDataChannel4RemoteLock);
    delete m_pDataChannel4Remote;

    m_pDataChannel4Remote = InitDataChannel(protocol, param, calbbback);
    if (m_pDataChannel4Remote == nullptr)
    {
        Error("[%p][DigitalTransport::InitRemoteChannel] InitDataChannel fali", this);
        return -1;
    }

    return 0;
}

int32_t DigitalTransport::StartTransport()
{
    if (m_pTransportThread != nullptr)
    {
        Error("[%p][DigitalTransport::StartTransport] Transport already started", this);
        return -1;
    }

    m_bStopTransport = false;
    m_pTransportThread = new std::thread(&DigitalTransport::TransportThread, this);

    return 0;
}

int32_t DigitalTransport::StopTransport()
{
    ReleaseAll();
    return 0;
}

int32_t DigitalTransport::SendMsg2Controller(std::shared_ptr<Packet> paclet)
{
    {
        std::lock_guard<std::mutex> lock(m_pDataChannel4ControllerLock);
        if (m_pDataChannel4Controller == nullptr)
        {
            Error("[%p][DigitalTransport::SendMsg2Controller] DataChannel for Controller is null", this);
            return -1;
        }

        std::lock_guard<std::mutex> lock1(m_pControllerMsgListLock);
        if (m_pControllerMsgList.size() > MAX_LIST_MSG_SIZE)
        {
            Error("[%p][DigitalTransport::SendMsg2Controller]  too much message queue cachet", this);
            return -2;
        }
        m_pControllerMsgList.push_back(paclet);
    }

    return 0;
}

int32_t DigitalTransport::SendMsg2Remote(std::shared_ptr<Packet> paclet)
{
    {
        std::lock_guard<std::mutex> lock(m_pDataChannel4RemoteLock);
        if (m_pDataChannel4Remote == nullptr)
        {
            Error("[%p][DigitalTransport::SendMsg2Remote] DataChannel for Remote is null", this);
            return -1;
        }

        std::lock_guard<std::mutex> lock1(m_pRemoteMsgListLock);
        if (m_pRemoteMsgList.size() > MAX_LIST_MSG_SIZE)
        {
            Error("[%p][DigitalTransport::SendMsg2Remote]  too much message queue cachet", this);
            return -1;
        }
        m_pRemoteMsgList.push_back(paclet);
    }

    return 0;
}

void DigitalTransport::SendHeartbeatMsg(std::shared_ptr<Packet> paclet)
{
    {
        std::lock_guard<std::mutex> lock(m_pDataChannel4ControllerLock);
        if (m_pDataChannel4Controller != nullptr)
        {
            std::lock_guard<std::mutex> lock1(m_pControllerMsgListLock);
            if (m_pControllerMsgList.size() < MAX_LIST_MSG_SIZE)
            {
                m_pControllerMsgList.push_back(paclet);
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_pDataChannel4RemoteLock);
        if (m_pDataChannel4Remote != nullptr)
        {
            std::lock_guard<std::mutex> lock1(m_pRemoteMsgListLock);
            if (m_pRemoteMsgList.size() < MAX_LIST_MSG_SIZE)
            {
                m_pRemoteMsgList.push_back(paclet);
            }
        }
    }
}

int32_t DigitalTransport::TransportThread()
{
    mavlink_message_t msg;
    m_cHeartbeatTimer.MakeTimePoint();
    uint8_t buf[BUFFER_LENGTH];
    uint16_t len;

    while (!m_bStopTransport)
    {
        bool bHasMsg = false;
        if (m_cHeartbeatTimer.GetDuration() > HEART_BEAT_TICKET)
        {
            m_cHeartbeatTimer.MakeTimePoint();
            mavlink_msg_heartbeat_pack(1, 200, &msg, MAV_TYPE_HELICOPTER, MAV_AUTOPILOT_GENERIC, MAV_MODE_GUIDED_ARMED, 0, MAV_STATE_ACTIVE);
            len = mavlink_msg_to_send_buffer(buf, &msg);
            std::shared_ptr<Packet> packet = std::make_shared<Packet>();
            packet->m_pData = (uint8_t*)malloc(len);
            if (packet->m_pData == nullptr)
            {
                Error("[%p][DigitalTransport::TransportThread]  malloc msg fail", this);
                continue;
            }
            memcpy(packet->m_pData, buf, len);
            packet->m_nLength = len;
            SendHeartbeatMsg(packet);
        }

        {
            std::lock_guard<std::mutex> lock(m_pDataChannel4ControllerLock);
            if (m_pDataChannel4Controller != nullptr)
            {
                std::lock_guard<std::mutex> lock1(m_pControllerMsgListLock);
                if (m_pControllerMsgList.size() > 0)
                {
                    std::shared_ptr<Packet> packet = m_pControllerMsgList.front();
                    m_pControllerMsgList.pop_front();
                    m_pDataChannel4Controller->SendMsg(packet);
                    bHasMsg = true;
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_pDataChannel4RemoteLock);
            if (m_pDataChannel4Remote != nullptr)
            {
                std::lock_guard<std::mutex> lock1(m_pRemoteMsgListLock);
                if (m_pRemoteMsgList.size() > 0)
                {
                    std::shared_ptr<Packet> packet = m_pRemoteMsgList.front();
                    m_pRemoteMsgList.pop_front();
                    m_pDataChannel4Remote->SendMsg(packet);
                    bHasMsg = true;
                }
            }
        }

        if (!bHasMsg)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    return 0;
}

int32_t DigitalTransport::RequestOSD()
{
    if (m_pDataChannel4Controller != nullptr)
    {
        mavlink_request_data_stream_t req;
        mavlink_message_t msg;
        uint8_t buf[BUFFER_LENGTH];
        uint16_t len;

        const uint8_t streams[] = {
            MAV_DATA_STREAM_RAW_SENSORS,
            MAV_DATA_STREAM_EXTENDED_STATUS,
            MAV_DATA_STREAM_RC_CHANNELS,
            MAV_DATA_STREAM_POSITION,
            MAV_DATA_STREAM_EXTRA1,
            MAV_DATA_STREAM_EXTRA2 };

        int totalStreams = sizeof(streams) / sizeof(uint8_t);

        for (int i = 0; i < totalStreams; i++)
        {
            req.req_message_rate = 5;
            req.req_stream_id = streams[i];
            req.start_stop = 1;
            req.target_component = 1;
            req.target_system = 1;
            mavlink_msg_request_data_stream_encode(1, MAV_COMP_ID_OSD, &msg, &req);
            len = mavlink_msg_to_send_buffer(buf, &msg);

            std::shared_ptr<Packet> packet = std::make_shared<Packet>();
            packet->m_pData = (uint8_t*)malloc(len);
            if (packet->m_pData == nullptr)
            {
                Error("[%p][DigitalTransport::InitControllerChannel]  malloc msg fail", this);
                continue;
            }
            memcpy(packet->m_pData, buf, len);
            packet->m_nLength = len;
            SendMsg2Controller(packet);
        }
    }
    else
    {
        Error("[%p][DigitalTransport::RequestOSD]  m_pDataChannel4Controller is null", this);
        return -1;
    }

    return 0;
}
