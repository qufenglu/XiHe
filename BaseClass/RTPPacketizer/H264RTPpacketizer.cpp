#include <cstring>
#include "H264RTPpacketizer.h"
#include "Log/Log.h"

#define ADD_START_CODE(p) p[0]=0,p[1]=0,p[2]=0,p[3]=1

extern bool FindSPS(uint8_t* data, uint32_t size, uint8_t*& sps, uint32_t& spsSize);
extern bool FindPPS(uint8_t* data, uint32_t size, uint8_t*& pps, uint32_t& ppsSize);

H264RTPpacketizer::H264RTPpacketizer()
{
    m_nPayloadType = 96;
    m_nSSRC = 0x12345678;
    m_nSeqNum = rand() % 65535;
    m_pRtpBuff = nullptr;

    m_pSPS = nullptr;
    m_nSPSLen = 0;
    m_pPPS = nullptr;
    m_nPPSLen = 0;

    m_bHasSendSPSBeforeIFrame = false;
    m_bHasSendPPSBeforeIFrame = false;

    m_pRtpPacketCallbaclk = nullptr;
}

H264RTPpacketizer::~H264RTPpacketizer()
{
    ReleaseAll();
}

int32_t H264RTPpacketizer::Init()
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
                Error("[%p][H264RTPpacketizer::Init] mallco RtpBuff  fail", this);
                goto FAIL;
            }
        }

    }
    return ret;

FAIL:
    ReleaseAll();
    return ret;
}

int32_t H264RTPpacketizer::ReleaseAll()
{
    if (m_pRtpBuff != nullptr)
    {
        delete m_pRtpBuff;
        m_pRtpBuff = nullptr;
    }

    free(m_pSPS);
    m_pSPS = nullptr;
    m_nSPSLen = 0;

    free(m_pPPS);
    m_pPPS = nullptr;
    m_nPPSLen = 0;

    m_bHasSendSPSBeforeIFrame = false;
    m_bHasSendPPSBeforeIFrame = false;
    m_pRtpPacketCallbaclk = nullptr;

    return 0;
}

bool H264RTPpacketizer::SetPaylodaType(uint8_t type)
{
    Debug("[%p][H264RTPpacketizer::SetPaylodaType] SetPaylodaType:%d", this, type);

    if (type > 0x7f)
    {
        Error("[%p][H264RTPpacketizer::SetPaylodaType] SetPaylodaType:%d", this, type);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_PacketizerLock);
        m_nPayloadType = type;
    }

    return true;
}

bool H264RTPpacketizer::SetRtpPacketCallbaclk(RTPPacketizer::RtpPacketCallbaclk callback)
{
    std::lock_guard<std::mutex> lock(m_PacketizerLock);
    m_pRtpPacketCallbaclk = callback;

    return true;
}

int32_t H264RTPpacketizer::RecvPacket(std::shared_ptr<MediaPacket> packet)
{
    uint8_t* data = packet->m_pData;
    uint32_t size = packet->m_nLength;
    uint32_t time = packet->m_lDTS;

    if (data == nullptr || size == 0)
    {
        Error("[%p][H264RTPpacketizer::RecvPacket] data:%p or size:%d error", this, data, size);
        return -1;
    }

    int32_t ret = 0;
    uint8_t nNaluType = data[0] & 0x1f;
    {
        if (nNaluType == 5)
        {
            if (!m_bHasSendSPSBeforeIFrame)
            {
                std::lock_guard<std::mutex> lock(m_PacketizerLock);
                PacketSPS(time);
                m_bHasSendSPSBeforeIFrame = true;
            }
            if (!m_bHasSendPPSBeforeIFrame)
            {
                std::lock_guard<std::mutex> lock(m_PacketizerLock);
                PacketPPS(time);
                m_bHasSendPPSBeforeIFrame = true;
            }
        }
        else if (nNaluType == 7)
        {
            if (m_pSPS == nullptr)
            {
                m_bHasSendSPSBeforeIFrame = true;
                uint8_t* pTempBuff = (uint8_t*)malloc(size + 4);
                if (pTempBuff != nullptr)
                {
                    ADD_START_CODE(pTempBuff);
                    memcpy(pTempBuff + 4, data, size);
                    UpdateSPSAndPPS(pTempBuff, size + 4);
                    free(pTempBuff);
                }
            }
            //data = m_pSPS;
            //size = m_nSPSLen;
        }
        else if (nNaluType == 8)
        {
            if (m_pPPS == nullptr)
            {
                m_bHasSendPPSBeforeIFrame = true;
                uint8_t* pTempBuff = (uint8_t*)malloc(size + 4);
                if (pTempBuff != nullptr)
                {
                    ADD_START_CODE(pTempBuff);
                    memcpy(pTempBuff + 4, data, size);
                    UpdateSPSAndPPS(pTempBuff, size + 4);
                    free(pTempBuff);
                }
            }
            //data = m_pPPS;
            //size = m_nPPSLen;
        }
        else
        {
            m_bHasSendSPSBeforeIFrame = false;
            m_bHasSendPPSBeforeIFrame = false;
        }

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
    }

    return ret;
}

int32_t H264RTPpacketizer::PacketAsFUANalu(uint8_t* data, uint32_t size, uint32_t time)
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

int32_t H264RTPpacketizer::PacketSPS(uint32_t time)
{
    if (m_pSPS == nullptr || m_nSPSLen == 0)
    {
        Warn("[%p][H264RTPpacketizer::PacketSPS] sps:%p or sps len:%d error", this, m_pSPS, m_nSPSLen);
        return 0;
    }

    int32_t ret = 0;
    if (m_nSPSLen <= MAX_RTP_LEN)
    {
        ret = PacketAsSingleNalu(m_pSPS, m_nSPSLen, time);
    }
    else
    {
        ret = PacketAsFUANalu(m_pSPS, m_nSPSLen, time);
    }

    return ret;
}

int32_t H264RTPpacketizer::PacketPPS(uint32_t time)
{
    if (m_pPPS == nullptr || m_nPPSLen == 0)
    {
        Warn("[%p][H264RTPpacketizer::PacketPPS] pps:%p or pps len:%d error", this, m_pPPS, m_nPPSLen);
        return 0;
    }

    int32_t ret = 0;
    if (m_nPPSLen <= MAX_RTP_LEN)
    {
        ret = PacketAsSingleNalu(m_pPPS, m_nPPSLen, time);
    }
    else
    {
        ret = PacketAsFUANalu(m_pPPS, m_nPPSLen, time);
    }

    return ret;
}

bool H264RTPpacketizer::SetSSRC(uint32_t ssrc)
{
    std::lock_guard<std::mutex> lock(m_PacketizerLock);
    m_nSSRC = ssrc;
    return true;
}

int32_t H264RTPpacketizer::PacketAsSingleNalu(uint8_t* data, uint32_t size, uint32_t time)
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

    memcpy(&m_pRtpBuff[12], data, size);
    if (m_pRtpPacketCallbaclk != nullptr)
    {
        m_pRtpPacketCallbaclk(m_pRtpBuff, 12U + size);
    }

    return 0;
}

int32_t H264RTPpacketizer::PacketAsFUAStart(uint8_t* data, uint32_t size, uint8_t type, uint32_t time)
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

int32_t H264RTPpacketizer::PacketAsFUAMiddle(uint8_t* data, uint32_t size, uint8_t type, uint32_t time)
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

int32_t H264RTPpacketizer::PacketAsFUAEnd(uint8_t* data, uint32_t size, uint8_t type, uint32_t time)
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

int32_t H264RTPpacketizer::SetSPS(const uint8_t* sps, uint32_t size)
{
    if (sps == nullptr || size == 0)
    {
        Error("[%p][H264RTPpacketizer::SetSPS] sps:%p or size:%d error", this, sps, size);
        return -1;
    }

    uint8_t* pNewSPS = (uint8_t*)malloc(size);
    if (pNewSPS == nullptr)
    {
        Error("[%p][H264RTPpacketizer::SetSPS] malloc sps fail", this);
        return -2;
    }
    memcpy(pNewSPS, sps, size);

    {
        std::lock_guard<std::mutex> lock(m_PacketizerLock);
        free(m_pSPS);
        m_pSPS = pNewSPS;
        m_nSPSLen = size;
    }

    return 0;
}

int32_t H264RTPpacketizer::SetPPS(const uint8_t* pps, uint32_t size)
{
    if (pps == nullptr || size == 0)
    {
        Error("[%p][H264RTPpacketizer::SetPPS] pps:%p or size:%d error", this, pps, size);
        return -1;
    }

    uint8_t* pNewPPS = (uint8_t*)malloc(size);
    if (pNewPPS == nullptr)
    {
        Error("[%p][H264RTPpacketizer::SetPPS] malloc pps fail", this);
        return -2;
    }
    memcpy(pNewPPS, pps, size);

    {
        std::lock_guard<std::mutex> lock(m_PacketizerLock);
        free(m_pPPS);
        m_pPPS = pNewPPS;
        m_nPPSLen = size;
    }

    return 0;
}

int32_t H264RTPpacketizer::UpdateSPSAndPPS(uint8_t* data, uint32_t size)
{
    uint8_t* sps;
    uint32_t spsSize;
    if (FindSPS(data, size, sps, spsSize))
    {
        SetSPS(sps, spsSize);
    }

    uint8_t* pps;
    uint32_t ppsSize;
    if (FindPPS(data, size, pps, ppsSize))
    {
        SetPPS(pps, ppsSize);
    }

    return 0;
}