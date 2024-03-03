#pragma once
#include <mutex>
#include "RTPPacketizer.h"
#include "Common.h"

class MJPEGRTPpacketizer : public RTPPacketizer
{
public:
	MJPEGRTPpacketizer();
    virtual ~MJPEGRTPpacketizer();

    int32_t Init();
    virtual bool SetPaylodaType(uint8_t type);
    virtual bool SetRtpPacketCallbaclk(RTPPacketizer::RtpPacketCallbaclk callback);
    virtual bool SetSSRC(uint32_t ssrc);
    virtual int32_t RecvPacket(std::shared_ptr<MediaPacket> packet);

private:
    int32_t ReleaseAll();

    int32_t PacketAsSingleNalu(uint8_t* data, uint32_t size, uint32_t time);
    int32_t PacketAsFUANalu(uint8_t* data, uint32_t size, uint32_t time);

    int32_t PacketAsFUAStart(uint8_t* data, uint32_t size, uint8_t type, uint32_t time);
    int32_t PacketAsFUAMiddle(uint8_t* data, uint32_t size, uint8_t type, uint32_t time);
    int32_t PacketAsFUAEnd(uint8_t* data, uint32_t size, uint8_t type, uint32_t time);

private:
    uint8_t m_nPayloadType;
    uint32_t m_nSSRC;
    uint16_t m_nSeqNum;
    uint8_t* m_pRtpBuff;

    RTPPacketizer::RtpPacketCallbaclk m_pRtpPacketCallbaclk;
    std::mutex m_PacketizerLock;		//由于RtpBuff共用，为了防止重入异常加锁
};
