#pragma once
#include <mutex>
#include "RTPPacketizer.h"
#include "Common.h"

class H264RTPpacketizer : public RTPPacketizer
{
public:
    H264RTPpacketizer();
    virtual ~H264RTPpacketizer();

    int32_t Init();
    virtual bool SetPaylodaType(uint8_t type);
    virtual bool SetRtpPacketCallbaclk(RTPPacketizer::RtpPacketCallbaclk callback);
    virtual bool SetSSRC(uint32_t ssrc);
    virtual int32_t RecvPacket(std::shared_ptr<MediaPacket> packet);

    int32_t SetSPS(const uint8_t* sps, uint32_t size);
    int32_t SetPPS(const uint8_t* pps, uint32_t size);
private:
    int32_t ReleaseAll();

    int32_t PacketAsSingleNalu(uint8_t* data, uint32_t size, uint32_t time);
    int32_t PacketAsFUANalu(uint8_t* data, uint32_t size, uint32_t time);

    int32_t PacketAsFUAStart(uint8_t* data, uint32_t size, uint8_t type, uint32_t time);
    int32_t PacketAsFUAMiddle(uint8_t* data, uint32_t size, uint8_t type, uint32_t time);
    int32_t PacketAsFUAEnd(uint8_t* data, uint32_t size, uint8_t type, uint32_t time);

    int32_t PacketSPS(uint32_t time);
    int32_t PacketPPS(uint32_t time);

    int32_t UpdateSPSAndPPS(uint8_t* data, uint32_t size);

private:
    uint8_t m_nPayloadType;
    uint32_t m_nSSRC;
    uint16_t m_nSeqNum;
    uint8_t* m_pRtpBuff;

    uint8_t* m_pSPS;
    uint32_t m_nSPSLen;
    uint8_t* m_pPPS;
    uint32_t m_nPPSLen;

    bool m_bHasSendSPSBeforeIFrame;
    bool m_bHasSendPPSBeforeIFrame;

    RTPPacketizer::RtpPacketCallbaclk m_pRtpPacketCallbaclk;

    std::mutex m_PacketizerLock;		//由于RtpBuff共用，为了防止重入异常加锁
};