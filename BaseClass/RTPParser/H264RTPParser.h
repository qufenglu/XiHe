#pragma once

#include <mutex>
#include "RTPParser.h"
#include "CommonTools/FlexibleBuff.h"

class H264RTPParser : public RTPParser
{
public:
    H264RTPParser();
    virtual ~H264RTPParser();

    virtual void GetPaylodaType(uint8_t& type);
    virtual bool SetPacketCallbaclk(RTPParser::MediaPacketCallbaclk callback);
    virtual void GetSSRC(uint32_t& ssrc);
    virtual int32_t RecvPacket(const std::shared_ptr<Packet>& packet);

private:
    int32_t ReleaseAll();
    int32_t OutputMediaPacket();

private:
    FlexibleBuff m_PacketBuff;
    std::mutex m_PacketBuffLock;
    RTPParser::MediaPacketCallbaclk m_pMediaPacketCallbaclk;
    uint8_t m_nPaylodaType;
    uint32_t m_nSSRC;

    uint32_t m_nLastPackTime;
};