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
    delete m_pFEC2DTable;
    m_pFEC2DTable = nullptr;

    {
        std::lock_guard<std::mutex> lock(m_cCachePacketLock);
        m_cCacheList.clear();
        m_cCacheMap.clear();
    }

    return 0;
}

int32_t RFC8627FECEncoder::Init(uint8_t row, uint8_t col)
{
    Trace("[%p][RFC8627FECEncoder::Init] Init col:%d row:%d", this, row, col);

    int32_t nFailRet = 0;
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
        nFailRet = -2;
        goto fail;
    }

    m_pFEC2DTable->SetFECPacketCallback(std::bind(&RFC8627FECEncoder::OnFECPacket, this, std::placeholders::_1));
    m_pFEC2DTable->SetRTPPacketCallback(std::bind(&RFC8627FECEncoder::OnRTPPacket, this, std::placeholders::_1));

    m_cCacheMap.reserve(MAX_CACHE_NAM);

    return 0;
fail:
    ReleaseAll();
    return ret;
}

bool RFC8627FECEncoder::SetFECEncoderPacketCallback(FECEncoderPacketCallback callback)
{
    m_pEncoderPacketCallback = callback;
    /*if (m_pFEC2DTable == nullptr)
    {
        Error("[%p][RFC8627FECEncoder::SetFECPacketCallback] FEC2DTable is  null", this);
        return false;
    }

    return m_pFEC2DTable->SetFECPacketCallback(callback);*/
    return true;
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

    {
        std::lock_guard<std::mutex> lock(m_cCachePacketLock);
        for (auto& seq : lost)
        {
            if (m_cCacheMap.find(seq) != m_cCacheMap.end())
            {
                OnRTPPacket(m_cCacheMap.at(seq));
            }
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
    {
        std::lock_guard<std::mutex> lock(m_cCachePacketLock);
        if (m_cCacheMap.find(seq) == m_cCacheMap.end())
        {
            m_cCacheMap.insert({ seq ,packet });
            m_cCacheList.push_back(seq);

            while (m_cCacheList.size() > MAX_CACHE_NAM)
            {
                uint16_t nRemoveSeq = m_cCacheList.front();
                m_cCacheList.pop_front();
                m_cCacheMap.erase(nRemoveSeq);
            }
        }
    }
}
