#pragma once
#include <functional>
#include <memory>
#include "Common.h"

#define MAX_RTP_LEN (1400)

class RTPPacketizer
{
public:
    typedef std::function<void(uint8_t* pRtpPacket, uint32_t size)> RtpPacketCallbaclk;

public:
    virtual ~RTPPacketizer() {};
    virtual bool SetPaylodaType(uint8_t type) = 0;
    virtual bool SetRtpPacketCallbaclk(RTPPacketizer::RtpPacketCallbaclk callback) = 0;
    virtual bool SetSSRC(uint32_t ssrc) = 0;
    virtual int32_t RecvPacket(std::shared_ptr<MediaPacket> packet) = 0;
};
