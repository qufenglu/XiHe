#pragma once
#include <functional>
#include <memory>
#include "Common.h"

class RTPParser
{
public:
    typedef std::function<void(std::shared_ptr<MediaPacket>&)> MediaPacketCallbaclk;

public:
    virtual ~RTPParser() {};
    virtual void GetPaylodaType(uint8_t& type) = 0;
    virtual bool SetPacketCallbaclk(RTPParser::MediaPacketCallbaclk callback) = 0;
    virtual void GetSSRC(uint32_t& ssrc) = 0;
    virtual int32_t RecvPacket(const std::shared_ptr<Packet>& packet) = 0;
};
