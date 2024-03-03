#include <cstring>
#include "MJPEGRTPpacketizer.h"
#include "Log/Log.h"

MJPEGRTPpacketizer::MJPEGRTPpacketizer()
{
    m_nPayloadType = 97;
    m_nSSRC = 0x12345678;
    m_nSeqNum = rand() % 65535;
    m_pRtpBuff = nullptr;
    m_pRtpPacketCallbaclk = nullptr;
}

MJPEGRTPpacketizer::~MJPEGRTPpacketizer()
{
    ReleaseAll();
}

int32_t MJPEGRTPpacketizer::Init()
{
    int32_t ret = 0;
    {
        std::lock_guard<std::mutex> lock(m_PacketizerLock);

        if (m_pRtpBuff == nullptr)
        {
            m_pRtpBuff = (uint8_t*)malloc(MAX_RTP_LEN + 128);
            if (m_pRtpBuff == nullptr)
            {
                ret = -2;
                Error("[%p][MJPEGRTPpacketizer::Init] mallco RtpBuff  fail", this);
                goto FAIL;
            }
        }

    }
    return ret;

FAIL:
    ReleaseAll();
    return ret;
}

int32_t MJPEGRTPpacketizer::ReleaseAll()
{
    if (m_pRtpBuff != nullptr)
    {
        delete m_pRtpBuff;
        m_pRtpBuff = nullptr;
    }
    m_pRtpPacketCallbaclk = nullptr;

    return 0;
}

bool MJPEGRTPpacketizer::SetPaylodaType(uint8_t type)
{
    Debug("[%p][MJPEGRTPpacketizer::SetPaylodaType] SetPaylodaType:%d", this, type);

    if (type > 0x7f)
    {
        Error("[%p][MJPEGRTPpacketizer::SetPaylodaType] SetPaylodaType:%d", this, type);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_PacketizerLock);
        m_nPayloadType = type;
    }

    return true;
}

bool MJPEGRTPpacketizer::SetRtpPacketCallbaclk(RTPPacketizer::RtpPacketCallbaclk callback)
{
    std::lock_guard<std::mutex> lock(m_PacketizerLock);
    m_pRtpPacketCallbaclk = callback;

    return true;
}

int32_t MJPEGRTPpacketizer::RecvPacket(std::shared_ptr<MediaPacket> packet)
{
    uint8_t* data = packet->m_pData;
    uint32_t size = packet->m_nLength;
    uint32_t time = packet->m_lDTS;

    //{
    //    std::string path = "/usr/Video1/" + std::to_string(packet->m_lDTS) + ".jpg";
    //    FILE* fp = fopen(path.c_str(), "wb+");
    //    fwrite(packet->m_pData, 1, packet->m_nLength, fp);
    //    fclose(fp);
    //}

    if (data == nullptr || size == 0)
    {
        Error("[%p][MJPEGRTPpacketizer::RecvPacket] data:%p or size:%d error", this, data, size);
        return -1;
    }

    int32_t ret = 0;
    {
        std::lock_guard<std::mutex> lock(m_PacketizerLock);
        if (size <= MAX_RTP_LEN)
        {
            ret = PacketAsSingleNalu(data, size, time);
        }
        else
        {
            ret = PacketAsFUANalu(data, size, time);
        }
    }

    return ret;
}

int32_t MJPEGRTPpacketizer::PacketAsFUANalu(uint8_t* data, uint32_t size, uint32_t time)
{
    uint8_t* pPacked = data;
    uint32_t nPackedRemain = size;

    uint8_t type = data[0];
    pPacked += 1;
    nPackedRemain -= 1;

    PacketAsFUAStart(pPacked, MAX_RTP_LEN - 2, type, time);		//FU-A头部多一字节
    pPacked += (MAX_RTP_LEN - 2);
    nPackedRemain -= (MAX_RTP_LEN - 2);

    while (nPackedRemain > (MAX_RTP_LEN - 2))
    {
        PacketAsFUAMiddle(pPacked, MAX_RTP_LEN - 2, type, time);
        pPacked += (MAX_RTP_LEN - 2);
        nPackedRemain -= (MAX_RTP_LEN - 2);
    }

    PacketAsFUAEnd(pPacked, nPackedRemain, type, time);

    return 0;
}

bool MJPEGRTPpacketizer::SetSSRC(uint32_t ssrc)
{
    std::lock_guard<std::mutex> lock(m_PacketizerLock);
    m_nSSRC = ssrc;
    return true;
}

int32_t MJPEGRTPpacketizer::PacketAsSingleNalu(uint8_t* data, uint32_t size, uint32_t time)
{
    m_pRtpBuff[0] = 0x80;
    m_pRtpBuff[1] = (uint8_t)(0x80 | m_nPayloadType);
    m_pRtpBuff[2] = (uint8_t)(m_nSeqNum >> 8);
    m_pRtpBuff[3] = (uint8_t)(m_nSeqNum & 0xff);
    m_nSeqNum++;

    m_pRtpBuff[4] = (uint8_t)(time >> 24);
    m_pRtpBuff[5] = (uint8_t)((time >> 16) & 0xff);
    m_pRtpBuff[6] = (uint8_t)((time >> 8) & 0xff);
    m_pRtpBuff[7] = (uint8_t)(time & 0xff);

    m_pRtpBuff[8] = (uint8_t)(m_nSSRC >> 24);
    m_pRtpBuff[9] = (uint8_t)((m_nSSRC >> 16) & 0xff);
    m_pRtpBuff[10] = (uint8_t)((m_nSSRC >> 8) & 0xff);
    m_pRtpBuff[11] = (uint8_t)(m_nSSRC & 0xff);
    m_pRtpBuff[11] = (uint8_t)(m_nSSRC & 0xff);
    m_pRtpBuff[13] = (uint8_t)(0x0a);

    memcpy(&m_pRtpBuff[13], data, size);
    if (m_pRtpPacketCallbaclk != nullptr)
    {
        m_pRtpPacketCallbaclk(m_pRtpBuff, 13U + size);
    }

    return 0;
}

int32_t MJPEGRTPpacketizer::PacketAsFUAStart(uint8_t* data, uint32_t size, uint8_t type, uint32_t time)
{
    m_pRtpBuff[0] = 0x80;
    m_pRtpBuff[1] = (uint8_t)(0x7f & m_nPayloadType);
    m_pRtpBuff[2] = (uint8_t)(m_nSeqNum >> 8);
    m_pRtpBuff[3] = (uint8_t)(m_nSeqNum & 0xff);
    m_nSeqNum++;

    m_pRtpBuff[4] = (uint8_t)(time >> 24);
    m_pRtpBuff[5] = (uint8_t)((time >> 16) & 0xff);
    m_pRtpBuff[6] = (uint8_t)((time >> 8) & 0xff);
    m_pRtpBuff[7] = (uint8_t)(time & 0xff);

    m_pRtpBuff[8] = (uint8_t)(m_nSSRC >> 24);
    m_pRtpBuff[9] = (uint8_t)((m_nSSRC >> 16) & 0xff);
    m_pRtpBuff[10] = (uint8_t)((m_nSSRC >> 8) & 0xff);
    m_pRtpBuff[11] = (uint8_t)(m_nSSRC & 0xff);

    uint8_t NRI = type & 0x60;
    uint8_t naluType = type & 0x1f;
    m_pRtpBuff[12] = NRI | 0x1c;
    m_pRtpBuff[13] = 0x80 | naluType;

    memcpy(&m_pRtpBuff[14], data, size);
    if (m_pRtpPacketCallbaclk != nullptr)
    {
        m_pRtpPacketCallbaclk(m_pRtpBuff, 14U + size);
    }

    return 0;
}

int32_t MJPEGRTPpacketizer::PacketAsFUAMiddle(uint8_t* data, uint32_t size, uint8_t type, uint32_t time)
{
    m_pRtpBuff[0] = 0x80;
    m_pRtpBuff[1] = (uint8_t)(0x7f & m_nPayloadType);
    m_pRtpBuff[2] = (uint8_t)(m_nSeqNum >> 8);
    m_pRtpBuff[3] = (uint8_t)(m_nSeqNum & 0xff);
    m_nSeqNum++;

    m_pRtpBuff[4] = (uint8_t)(time >> 24);
    m_pRtpBuff[5] = (uint8_t)((time >> 16) & 0xff);
    m_pRtpBuff[6] = (uint8_t)((time >> 8) & 0xff);
    m_pRtpBuff[7] = (uint8_t)(time & 0xff);

    m_pRtpBuff[8] = (uint8_t)(m_nSSRC >> 24);
    m_pRtpBuff[9] = (uint8_t)((m_nSSRC >> 16) & 0xff);
    m_pRtpBuff[10] = (uint8_t)((m_nSSRC >> 8) & 0xff);
    m_pRtpBuff[11] = (uint8_t)(m_nSSRC & 0xff);

    uint8_t NRI = type & 0x60;
    uint8_t naluType = type & 0x1f;
    m_pRtpBuff[12] = NRI | 0x1c;
    m_pRtpBuff[13] = naluType;

    memcpy(&m_pRtpBuff[14], data, size);
    if (m_pRtpPacketCallbaclk != nullptr)
    {
        m_pRtpPacketCallbaclk(m_pRtpBuff, 14U + size);
    }

    return 0;
}

int32_t MJPEGRTPpacketizer::PacketAsFUAEnd(uint8_t* data, uint32_t size, uint8_t type, uint32_t time)
{
    m_pRtpBuff[0] = 0x80;
    m_pRtpBuff[1] = (uint8_t)(0x80 | m_nPayloadType);
    m_pRtpBuff[2] = (uint8_t)(m_nSeqNum >> 8);
    m_pRtpBuff[3] = (uint8_t)(m_nSeqNum & 0xff);
    m_nSeqNum++;

    m_pRtpBuff[4] = (uint8_t)(time >> 24);
    m_pRtpBuff[5] = (uint8_t)((time >> 16) & 0xff);
    m_pRtpBuff[6] = (uint8_t)((time >> 8) & 0xff);
    m_pRtpBuff[7] = (uint8_t)(time & 0xff);

    m_pRtpBuff[8] = (uint8_t)(m_nSSRC >> 24);
    m_pRtpBuff[9] = (uint8_t)((m_nSSRC >> 16) & 0xff);
    m_pRtpBuff[10] = (uint8_t)((m_nSSRC >> 8) & 0xff);
    m_pRtpBuff[11] = (uint8_t)(m_nSSRC & 0xff);

    uint8_t NRI = type & 0x60;
    uint8_t naluType = type & 0x1f;
    m_pRtpBuff[12] = NRI | 0x1c;
    m_pRtpBuff[13] = 0x40 | naluType;

    memcpy(&m_pRtpBuff[14], data, size);
    if (m_pRtpPacketCallbaclk != nullptr)
    {
        m_pRtpPacketCallbaclk(m_pRtpBuff, 14U + size);
    }

    return 0;
}
