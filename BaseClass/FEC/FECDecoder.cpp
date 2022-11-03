#include "FECDecoder.h"
#include "Log/Log.h"
#include "CommonTools/TimeCounter.h"

#define MAX_CACHE_NUM 50
#define MAX_WAIT_TIME 40.0				//丢包最长等待时间
#define MAX_SKIP_NUM 10					//丢包最大连续跳跃包数
#define NACK_TICK 5.0							//发送NACK周期
#define MAX_TOLERATED_JUMP 100		//最大容忍包跳跃数


RFC8627FECDecoder::RFC8627FECDecoder()
{
    m_nPayloadType = 99;
    m_nSSRC = 0x33445566;
    m_pDecoderPacketCallback = nullptr;
    m_pRTPSortArray = nullptr;
    m_bRecvFtrstPacket = false;
    m_nLastOutSeq = 0;
    m_nLastRecvSeq = 0;
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

    for (auto& pFEC2DTable : m_pTableList)
    {
        if (pFEC2DTable != nullptr)
        {
            delete pFEC2DTable;
        }
    }
    m_pTableList.clear();

    for (auto& packet : m_pCachePacketList)
    {
        packet = nullptr;
    }
    m_pCachePacketList.clear();

    {
        std::lock_guard<std::mutex> lock(m_pRTPSortArrayLock);
        if (m_pRTPSortArray != nullptr)
        {
            for (int i = 0; i < 65536; i++)
            {
                m_pRTPSortArray[i] = nullptr;
            }
            delete[] m_pRTPSortArray;
            m_pRTPSortArray = nullptr;
        }

        m_bRecvFtrstPacket = false;
        m_nLastOutSeq = 0;
        m_nLastRecvSeq = 0;
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
        int32_t ret1 = pFEC2DTable->Init(row, col);
        if (ret1 != 0)
        {
            Error("[%p][RFC8627FECDecoder::Init] Init FEC2DTable err,return:%d", this, ret1);
            ret = -2;
            goto fail;
        }
        FEC2DTable::RTPPacketCallback calbbback = std::bind(&RFC8627FECDecoder::OnRTPPacket, this, std::placeholders::_1);
        pFEC2DTable->SetRTPPacketCallback(calbbback);
    }

    {
        std::lock_guard<std::mutex> lock(m_pRTPSortArrayLock);
        m_pRTPSortArray = new std::shared_ptr<Packet>[65536]();
        if (m_pRTPSortArray == nullptr)
        {
            Error("[%p][RFC8627FECDecoder::Init] malloc rtp sort array fail", this);
            ret = -3;
            goto fail;
        }
    }

    //为了简化后续操作，限定初始化前必须设置DecoderPacketCallback和NackPacketCallback
    if (m_pDecoderPacketCallback == nullptr)
    {
        Error("[%p][RFC8627FECDecoder::Init] not set decoder packet callback", this);
        ret = -4;
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

void RFC8627FECDecoder::OnRTPPacket(const std::shared_ptr<Packet>& packet)
{
    uint16_t seq = (packet->m_pData[2] << 8) | packet->m_pData[3];
    {
        std::lock_guard<std::mutex> lock(m_pRTPSortArrayLock);
        m_pRTPSortArray[seq] = nullptr;
        m_pRTPSortArray[seq] = packet;
        m_nLastRecvSeq = seq;

        if (m_bRecvFtrstPacket)
        {
            //seq跳变
            if (abs(seq - m_nLastOutSeq) > MAX_TOLERATED_JUMP)
            {
                m_nLastOutSeq = seq;
            }
        }
        else
        {
            m_bRecvFtrstPacket = true;
            m_nLastOutSeq = seq;
        }
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

int32_t RFC8627FECDecoder::RecvPacket(const std::shared_ptr<Packet>& packet)
{
    uint8_t pt = packet->m_pData[1] & 0x7f;
    bool bIsRepair = pt == m_nPayloadType ? true : false;
    if (!bIsRepair)
    {
        OnRTPPacket(packet);
    }

    //LRU替换算法
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
        m_pTableList.push_front(pFEC2DTable);
    }
    else
    {
        if (pt == m_nPayloadType)
        {
            pFEC2DTable = m_pTableList.back();
            m_pTableList.pop_back();
            m_pTableList.push_front(pFEC2DTable);

            if (pFEC2DTable != nullptr)
            {
                pFEC2DTable->ClearTable();
            }
            pFEC2DTable->RecvPacketAndTryRepair(packet);

            for (auto it = m_pCachePacketList.begin(); it != m_pCachePacketList.cend();)
            {
                if (pFEC2DTable->IsCanRecvPacket(*it))
                {
                    pFEC2DTable->RecvPacketAndMakeRepair(*it);
                    it = m_pCachePacketList.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }
        else
        {
            if (m_pCachePacketList.size() > MAX_CACHE_NUM)
            {
                while (m_pCachePacketList.size() > 0)
                {
                    std::shared_ptr<Packet> temp = m_pCachePacketList.back();
                    m_pCachePacketList.pop_back();
                    temp = nullptr;
                }
                Trace("[%p][RFC8627FECDecoder::RecvPacket] cache packet list size > MAX_CACHE_NUM clear", this);
            }

            m_pCachePacketList.push_front(packet);
        }
    }

    return 0;
}

void RFC8627FECDecoder::OutPacketThread()
{
    //等待第一个包
    while (!m_bStopOutPacket)
    {
        if (m_bRecvFtrstPacket)
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
            if (m_pRTPSortArray[m_nLastOutSeq] != nullptr)
            {
                pOutPacket = m_pRTPSortArray[m_nLastOutSeq];
                m_pRTPSortArray[m_nLastOutSeq] = nullptr;
                m_nLastOutSeq++;
            }
        }

        //找到下一个要输出的包，出包
        if (pOutPacket != nullptr)
        {
            lostWaitTimer.MakeTimePoint();
            m_pDecoderPacketCallback(pOutPacket);
            pOutPacket = nullptr;
            continue;
        }

        //未找到下一个要输出的包，当到达最大等待时间跳过等待
        if (lostWaitTimer.GetDuration() > MAX_WAIT_TIME)
        {
            lostWaitTimer.MakeTimePoint();
            Trace("[%p][RFC8627FECDecoder::OutPacketThread] packet:%d lost,skip", this, m_nLastOutSeq);
            SkipPackets();
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

        if (bNeedWait)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void RFC8627FECDecoder::SkipPackets()
{
    std::shared_ptr<Packet> pOutPacket = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_pRTPSortArrayLock);
        for (int i = 0; i < MAX_SKIP_NUM; i++)
        {
            //最多跳到收到的最后一个包
            if (m_nLastRecvSeq == m_nLastOutSeq)
            {
                break;
            }

            m_nLastOutSeq++;
            if (m_pRTPSortArray[m_nLastOutSeq] != nullptr)
            {
                pOutPacket = m_pRTPSortArray[m_nLastOutSeq];
                m_pRTPSortArray[m_nLastOutSeq] = nullptr;
                m_nLastOutSeq++;
                break;
            }
        }
    }

    if (pOutPacket != nullptr)
    {
        m_pDecoderPacketCallback(pOutPacket);
        pOutPacket = nullptr;
    }
}

int32_t RFC8627FECDecoder::OutNackPacketIfNeed()
{
    std::shared_ptr<Packet>* nack = MakeNackPacket();
    if (nack != nullptr)
    {
        m_pNackPacketCallback(*nack);
    }
    (*nack) = nullptr;
    delete nack;

    return 0;
}

std::shared_ptr<Packet>* RFC8627FECDecoder::MakeNackPacket()
{
    std::list<NackItem> nackItems;
    std::shared_ptr<Packet>* nack = nullptr;

    {
        std::lock_guard<std::mutex> lock(m_pRTPSortArrayLock);
        uint16_t seq = m_nLastOutSeq;

        while (seq != m_nLastRecvSeq)
        {
            seq++;
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

        nack = new std::shared_ptr<Packet>();
        (*nack)->m_pData = nackData;
        (*nack)->m_nLength = nNackPacketSize;
    }

    return nack;
}
