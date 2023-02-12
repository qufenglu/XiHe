#include "FECDecoder.h"
#include "Log/Log.h"
#include "CommonTools/TimeCounter.h"

#define MAX_CACHE_NUM 50
#define MAX_WAIT_TIME 40.0				//丢包最长等待时间
#define MAX_SKIP_NUM 100					//丢包最大连续跳跃包数
#define NACK_TICK 5.0							//发送NACK周期
#define MAX_TOLERATED_JUMP 100		//最大容忍包跳跃数


RFC8627FECDecoder::RFC8627FECDecoder()
{
    m_nPayloadType = 99;
    m_nSSRC = 0x33445566;
    m_bRecvFirstPacket = false;
    m_nExpOutSeq = 0;
    m_nLastRecvSeq = 0;
    m_nLastOutSeq = 0;
    m_bStopOutPacket = true;
    m_pOutPacketThread = nullptr;
}

RFC8627FECDecoder::~RFC8627FECDecoder()
{
    ReleaseAll();
}

int32_t RFC8627FECDecoder::ReleaseAll()
{
    m_bStopOutPacket = true;
    if (m_pOutPacketThread != nullptr)
    {
        if (m_pOutPacketThread->joinable())
        {
            m_pOutPacketThread->join();
        }
        delete m_pOutPacketThread;
        m_pOutPacketThread = nullptr;
    }

    for (auto item : m_pTableList)
    {
        delete item;
    }
    m_pTableList.clear();

    m_pCachePacketList.clear();
    m_bRecvFirstPacket = false;
    m_nExpOutSeq = 0;
    m_nLastRecvSeq = 0;
    m_nLastOutSeq = 0;

    {
        std::lock_guard<std::mutex> lock(m_pRTPSortArrayLock);
        for (auto& item : m_pRTPSortArray)
        {
            item = nullptr;
        }
    }

    return 0;
}

int32_t RFC8627FECDecoder::Init(uint8_t row, uint8_t col, uint8_t tableNum)
{
    Trace("[%p][RFC8627FECDecoder::Init] Init col:%d row:%d num:%d", this, row, col, tableNum);

    int32_t ret = 0;
    if (tableNum == 0)
    {
        Error("[%p][RFC8627FECDecoder::Init] num:%d err", this, tableNum);
        ret = -1;
        goto fail;
    }

    for (uint8_t i = 0; i < tableNum; i++)
    {
        FEC2DTable* pFEC2DTable = new FEC2DTable(m_nPayloadType);
        m_pTableList.push_back(pFEC2DTable);
        ret = pFEC2DTable->Init(row, col);
        if (ret != 0)
        {
            Error("[%p][RFC8627FECDecoder::Init] Init FEC2DTable err,return:%d", this, ret);
            ret = -2;
            goto fail;
        }
        pFEC2DTable->SetRTPPacketCallback(std::bind(&RFC8627FECDecoder::OnRTPPacket, this, std::placeholders::_1, std::placeholders::_2));
    }

    {
        std::lock_guard<std::mutex> lock(m_pRTPSortArrayLock);
        for (int i = 0; i < 65536; i++)
        {
            m_pRTPSortArray.push_back(nullptr);
        }
    }

    if (m_pDecoderPacketCallback == nullptr)
    {
        Error("[%p][RFC8627FECDecoder::Init] not set decoder packet callback", this);
        ret = -3;
        goto fail;
    }
    if (m_pNackPacketCallback == nullptr)
    {
        Error("[%p][RFC8627FECDecoder::Init] not set nack packet callback", this);
        ret = -4;
        goto fail;
    }

    m_bStopOutPacket = false;
    m_pOutPacketThread = new std::thread(&RFC8627FECDecoder::OutPacketThread, this);

    return 0;
fail:
    ReleaseAll();
    return ret;
}

bool RFC8627FECDecoder::IsHasRecvPacket(uint16_t seq)
{
    if (m_nLastRecvSeq >= MAX_TOLERATED_JUMP)
    {
        if (seq <= m_nLastRecvSeq)
        {
            return true;
        }
    }
    else
    {
        if (seq <= m_nLastRecvSeq || seq > (65535 - (MAX_TOLERATED_JUMP - m_nLastRecvSeq)))
        {
            return true;
        }
    }

    return false;
}

void RFC8627FECDecoder::OnRTPPacket(const std::shared_ptr<Packet>& packet, bool isOutByRepair)
{
    uint16_t seq = (packet->m_pData[2] << 8) | packet->m_pData[3];
    {
        std::lock_guard<std::mutex> lock(m_pRTPSortArrayLock);
        if (m_bRecvFirstPacket)
        {
            if (abs(seq - m_nLastRecvSeq) > MAX_TOLERATED_JUMP)
            {
                Warn("[%p][RFC8627FECDecoder::OnRTPPacket] seq jump,%d->%d", this, m_nLastRecvSeq, seq);
                m_nExpOutSeq = seq;
                goto AddToArray;
            }

            if (IsHasRecvPacket(seq))
            {
                if (!isOutByRepair)
                {
                    Warn("[%p][RFC8627FECDecoder::OnRTPPacket] packet:%d has recved,discare", this, seq);
                }
                return;
            }
        }
        else
        {
            m_bRecvFirstPacket = true;
            m_nExpOutSeq = seq;
        }

    AddToArray:
        m_pRTPSortArray[seq] = packet;
        m_nLastRecvSeq = seq;
        Debug("[%p][RFC8627FECDecoder::OnRTPPacket] m_nLastRecvSeq:%d", this, m_nLastRecvSeq);
    }
}

bool RFC8627FECDecoder::SetDecoderPacketCallback(FECDecoderPacketCallback callback)
{
    m_pDecoderPacketCallback = callback;
    return true;
}

bool RFC8627FECDecoder::SetNackPacketCallback(NackPacketCallback callback)
{
    m_pNackPacketCallback = callback;
    return true;
}

bool RFC8627FECDecoder::SetPayloadType(uint8_t pt)
{
    if (pt > 0x7f)
    {
        Error("[%p][RFC8627FECDecoder::SetPayloadType] pt:%d err", this, pt);
        return false;
    }

    m_nPayloadType = pt;
    return true;
}

bool RFC8627FECDecoder::SetSSRC(uint32_t ssrc)
{
    m_nSSRC = ssrc;
    return true;
}

void RFC8627FECDecoder::RecvCachePacket(FEC2DTable* pFEC2DTable)
{
    for (auto it = m_pCachePacketList.begin(); it != m_pCachePacketList.cend();)
    {
        if (pFEC2DTable->IsCanRecvPacket(*it))
        {
            pFEC2DTable->RecvPacketAndTryRepair(*it);
            it = m_pCachePacketList.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

int32_t RFC8627FECDecoder::RecvPacket(const std::shared_ptr<Packet>& packet)
{
    uint8_t pt = packet->m_pData[1] & 0x7f;
    bool bIsRepair = pt == m_nPayloadType ? true : false;

    uint16_t seq = (packet->m_pData[2] << 8) | packet->m_pData[3];
    if (bIsRepair)
    {
        Debug("[%p]recv repair rtp seq:%d pt:%d", this, seq, pt);
    }
    else
    {
        Debug("[%p]recv rtp seq:%d pt:%d", this, seq, pt);
        OnRTPPacket(packet);
    }

    FEC2DTable* pFEC2DTable = nullptr;
    for (auto it = m_pTableList.begin(); it != m_pTableList.cend();)
    {
        if ((*it) != nullptr && (*it)->IsCanRecvPacket(packet))
        {
            pFEC2DTable = (*it);
            it = m_pTableList.erase(it);
            break;
        }
        else
        {
            ++it;
        }
    }

    if (pFEC2DTable != nullptr)
    {
        pFEC2DTable->RecvPacketAndTryRepair(packet);
        RecvCachePacket(pFEC2DTable);
        m_pTableList.push_front(pFEC2DTable);
    }
    else
    {
        if (pt == m_nPayloadType)
        {
            pFEC2DTable = m_pTableList.back();
            m_pTableList.pop_back();
            m_pTableList.push_front(pFEC2DTable);

            pFEC2DTable->ClearTable();
            pFEC2DTable->RecvPacketAndTryRepair(packet);
            RecvCachePacket(pFEC2DTable);
        }
        else
        {
            if (m_pCachePacketList.size() > MAX_CACHE_NUM)
            {
                while (m_pCachePacketList.size() > 0)
                {
                    m_pCachePacketList.pop_front();
                }
                Trace("[%p][RFC8627FECDecoder::RecvPacket] cache packet list size > MAX_CACHE_NUM clear", this);
            }

            m_pCachePacketList.push_back(packet);
        }
    }

    return 0;
}

void RFC8627FECDecoder::OutPacketThread()
{
    //等待第一个包
    while (!m_bStopOutPacket)
    {
        if (m_bRecvFirstPacket)
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    TimeCounter lostWaitTimer;
    lostWaitTimer.MakeTimePoint();
    TimeCounter nackTimer;
    nackTimer.MakeTimePoint();
    std::shared_ptr<Packet> pOutPacket = nullptr;
    bool bNeedWait = false;

    while (!m_bStopOutPacket)
    {
        pOutPacket = nullptr;
        bNeedWait = false;

        {
            std::lock_guard<std::mutex> lock(m_pRTPSortArrayLock);
            if (m_pRTPSortArray[m_nExpOutSeq] != nullptr)
            {
                pOutPacket = m_pRTPSortArray[m_nExpOutSeq];
                m_pRTPSortArray[m_nExpOutSeq] = nullptr;
            }

            //找到下一个要输出的包，出包
            if (pOutPacket != nullptr)
            {
                lostWaitTimer.MakeTimePoint();
                m_pDecoderPacketCallback(pOutPacket);
                m_nLastOutSeq = m_nExpOutSeq;
                m_nExpOutSeq++;
                continue;
            }

            //未找到下一个要输出的包，当到达最大等待时间跳过等待
            if (lostWaitTimer.GetDuration() > MAX_WAIT_TIME)
            {
                lostWaitTimer.MakeTimePoint();
                if (!SkipPackets())
                {
                    bNeedWait = true;
                }
            }
            else
            {
                bNeedWait = true;
            }

            if (nackTimer.GetDuration() > NACK_TICK)
            {
                nackTimer.MakeTimePoint();
                OutNackPacketIfNeed();
            }
        }

        if (bNeedWait)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

bool RFC8627FECDecoder::SkipPackets()
{
    std::shared_ptr<Packet> pOutPacket = nullptr;
    {
        //std::lock_guard<std::mutex> lock(m_pRTPSortArrayLock);
        for (int i = 0; i < MAX_SKIP_NUM; i++)
        {
            //最多跳到收到的最后一个包
            if (m_nLastRecvSeq + 1 == m_nExpOutSeq)
            {
                break;
            }
            m_nExpOutSeq++;

            if (m_pRTPSortArray[m_nExpOutSeq] != nullptr)
            {
                pOutPacket = m_pRTPSortArray[m_nExpOutSeq];
                m_pRTPSortArray[m_nExpOutSeq] = nullptr;
                break;
            }
        }
    }

    if (pOutPacket == nullptr)
    {
        if (m_nExpOutSeq != m_nLastRecvSeq + 1)
        {
            m_nExpOutSeq = m_nLastRecvSeq + 1;
            Warn("[%p][RFC8627FECDecoder::SkipPackets1] skip packet:%d->%d", this, m_nLastOutSeq, m_nLastRecvSeq);
        }
    }
    else
    {
        uint16_t seq = m_nLastOutSeq;
        m_pDecoderPacketCallback(pOutPacket);
        m_nLastOutSeq = m_nExpOutSeq;
        m_nExpOutSeq++;
        Warn("[%p][RFC8627FECDecoder::SkipPackets2] skip packet:%d->%d", this, seq, m_nLastOutSeq - 1);
        pOutPacket = nullptr;
        return true;
    }

    return false;
}

int32_t RFC8627FECDecoder::OutNackPacketIfNeed()
{
    if (m_pNackPacketCallback != nullptr)
    {
        std::shared_ptr<Packet>nack = MakeNackPacket();
        if (nack != nullptr)
        {
            m_pNackPacketCallback(nack);
        }
    }

    return 0;
}

std::shared_ptr<Packet> RFC8627FECDecoder::MakeNackPacket()
{
    std::list<NackItem> nackItems;
    std::shared_ptr<Packet> nack = nullptr;

    {
        //std::lock_guard<std::mutex> lock(m_pRTPSortArrayLock);
        uint16_t seq = m_nLastOutSeq + 1;

        while (seq < m_nLastRecvSeq)
        {
            if (m_pRTPSortArray[seq] == nullptr)
            {
                NackItem item;
                item.PID = seq;

                for (int i = 0; i < 16; i++)
                {
                    seq++;
                    if (seq == m_nLastRecvSeq)
                    {
                        break;
                    }
                    if (m_pRTPSortArray[seq] == nullptr)
                    {
                        item.BLP |= (1 << (15 - i));
                    }
                }
                nackItems.push_back(item);
            }
            seq++;
        }
    }

    size_t itemCount = nackItems.size();
    if (itemCount > 0)
    {
        size_t nNackPacketSize = itemCount * 4 + 12;
        uint8_t* nackData = (uint8_t*)malloc(nNackPacketSize);
        if (nackData == nullptr)
        {
            Error("[%p][RFC8627FECDecoder::MakeNackPacket] malloc nack data fail,size:%d", nNackPacketSize);
            return nullptr;
        }

        nackData[0] = 0x81;
        nackData[1] = 0xcd;
        nackData[2] = (itemCount + 2) >> 8;
        nackData[3] = (itemCount + 2) & 0xff;
        nackData[4] = m_nSSRC >> 24;
        nackData[5] = (m_nSSRC >> 16) & 0xff;
        nackData[6] = (m_nSSRC >> 8) & 0xff;
        nackData[7] = m_nSSRC & 0xff;
        nackData[8] = 0;
        nackData[9] = 0;
        nackData[10] = 0;
        nackData[11] = 0;

        int i = 0;
        for (auto& item : nackItems)
        {
            nackData[12 + (i * 4)] = item.PID >> 8;
            nackData[13 + (i * 4)] = item.PID & 0xff;
            nackData[14 + (i * 4)] = item.BLP >> 8;
            nackData[15 + (i * 4)] = item.BLP & 0xff;
            i++;
        }

        nack = std::make_shared<Packet>();
        nack->m_pData = nackData;
        nack->m_nLength = nNackPacketSize;
    }

    return nack;
}
