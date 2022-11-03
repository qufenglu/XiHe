#include <list>
#include "FECEncoder.h"
#include "Log/Log.h"

#define MAX_CACHE_NAM 200

RFC8627FECEncoder::RFC8627FECEncoder()
{
    m_nPayloadType = 99;
    m_nSeq = 0;
    m_nSSRC = 0x55667788;
    m_pFEC2DTable = nullptr;
    m_pEncoderPacketCallback = nullptr;
}

RFC8627FECEncoder::~RFC8627FECEncoder()
{
    ReleaseAll();
}

int32_t RFC8627FECEncoder::ReleaseAll()
{
    if (m_pFEC2DTable != nullptr)
    {
        delete m_pFEC2DTable;
        m_pFEC2DTable = nullptr;
    }

    return 0;
}

int32_t RFC8627FECEncoder::Init(uint8_t row, uint8_t col)
{
    Trace("[%p][RFC8627FECEncoder::Init] Init col:%d row:%d", this, row, col);

    if (m_pFEC2DTable != nullptr)
    {
        Error("[%p][RFC8627FECEncoder::Init] FEC2DTable is not null", this);
        return -1;
    }

    m_pFEC2DTable = new FEC2DTable(m_nPayloadType);
    int32_t ret = m_pFEC2DTable->Init(row, col);
    if (ret != 0)
    {
        Error("[%p][RFC8627FECEncoder::Init] Init FEC2DTable err,return:%d", this, ret);
        ReleaseAll();
        return -2;
    }
    FEC2DTable::FECPacketCallback calbbback1 = std::bind(&RFC8627FECEncoder::OnFECPacket, this, std::placeholders::_1);
    m_pFEC2DTable->SetFECPacketCallback(calbbback1);
    FEC2DTable::RTPPacketCallback calbbback2 = std::bind(&RFC8627FECEncoder::OnRTPPacket, this, std::placeholders::_1);
    m_pFEC2DTable->SetRTPPacketCallback(calbbback2);

    m_CacheMap.reserve(MAX_CACHE_NAM);

    return 0;
}

bool RFC8627FECEncoder::SetFECEncoderPacketCallback(FECEncoderPacketCallback callback)
{
    if (m_pFEC2DTable == nullptr)
    {
        Error("[%p][RFC8627FECEncoder::SetFECPacketCallback] FEC2DTable is not null", this);
        return false;
    }

    return m_pFEC2DTable->SetFECPacketCallback(callback);
}

int32_t RFC8627FECEncoder::RecvRTPPacket(const std::shared_ptr<Packet>& packet)
{
    OnRTPPacket(packet);

    if (m_pFEC2DTable == nullptr)
    {
        Error("[%p][RFC8627FECEncoder::RecvPacket] FEC2DTable is not null", this);
        return -1;
    }

    CacheRTPPacket(packet);

    int32_t ret = m_pFEC2DTable->RecvPacketAndMakeRepair(packet);
    if (ret != 0)
    {
        Error("[%p][RFC8627FECEncoder::RecvPacket] RecvPacketAndMakeRepair fail,return:%d", this, ret);
        return -2;
    }

    return 0;
}

int32_t RFC8627FECEncoder::RecvNackPacket(const std::shared_ptr<Packet>& packet)
{
    uint8_t* nack = packet->m_pData;
    uint16_t itemCount = (nack[2] << 8) | nack[3];
    if (((itemCount + 3) * 4) > packet->m_nLength)
    {
        Error("[%p][RFC8627FECEncoder::RecvNackPacket] nack packet item:%d size:%d error", this, itemCount, packet->m_nLength);
        return -1;
    }

    std::list<uint16_t> lost;
    for (int i = 0; i < itemCount; i++)
    {
        uint16_t PID = (nack[12 + (4 * i)] << 8) | nack[13 + (4 * i)];
        uint16_t BLP = (nack[14 + (4 * i)] << 8) | nack[15 + (4 * i)];

        lost.push_back(PID);
        for (int i = 0; i < 16; i++)
        {
            if ((BLP & 0x80) == 0x80)
            {
                lost.push_back(PID + i + 1);
            }
            BLP <<= 1;
        }
    }

    for (auto& seq : lost)
    {
        if (m_CacheMap.find(seq) != m_CacheMap.end())
        {
            OnRTPPacket(m_CacheMap[seq]);
        }
    }

    return 0;
}

bool RFC8627FECEncoder::SetPayloadType(uint8_t pt)
{
    if (pt > 0x7f)
    {
        Error("[%p][RFC8627FECEncoder::SetPayloadType] pt:%d err", this, pt);
        return false;
    }

    m_nPayloadType = pt;
    return true;
}

bool RFC8627FECEncoder::SetSSRC(uint32_t ssrc)
{
    m_nSSRC = ssrc;
    return true;
}

void RFC8627FECEncoder::OnFECPacket(const std::shared_ptr<Packet>& packet)
{
    uint8_t* pRepairPacketData = packet->m_pData;
    pRepairPacketData[1] = 0x80 | m_nPayloadType;
    pRepairPacketData[2] = m_nSeq >> 8;
    pRepairPacketData[3] = m_nSeq & 0xff;
    pRepairPacketData[8] = m_nSSRC >> 24;
    pRepairPacketData[9] = (m_nSSRC >> 16) & 0xff;
    pRepairPacketData[10] = (m_nSSRC >> 8) & 0xff;
    pRepairPacketData[11] = m_nSSRC & 0xff;
    m_nSeq++;

    if (m_pEncoderPacketCallback != nullptr)
    {
        m_pEncoderPacketCallback(packet);
    }
}

void RFC8627FECEncoder::OnRTPPacket(const std::shared_ptr<Packet>& packet)
{
    if (m_pEncoderPacketCallback != nullptr)
    {
        m_pEncoderPacketCallback(packet);
    }
}

void RFC8627FECEncoder::CacheRTPPacket(const std::shared_ptr<Packet>& packet)
{
    uint16_t seq = (packet->m_pData[2] << 8) | packet->m_pData[3];
    if (m_CacheMap.find(seq) == m_CacheMap.end())
    {
        m_CacheMap[seq] = packet;
        m_CacheList.push_back(seq);

        while (m_CacheList.size() > MAX_CACHE_NAM)
        {
            uint16_t nRemoveSeq = m_CacheList.front();
            m_CacheList.pop_front();
            std::shared_ptr<Packet> removePacket = m_CacheMap[nRemoveSeq];
            m_CacheMap.erase(nRemoveSeq);
            removePacket = nullptr;
        }
    }
}
