#pragma once
#include <list>
#include <mutex>
#include <thread>
#include "Common.h"
#include "FEC2DTable.h"

class RFC8627FECDecoder
{
public:
    typedef std::function<void(const std::shared_ptr<Packet>&)> FECDecoderPacketCallback;
    typedef std::function<void(const std::shared_ptr<Packet>&)> NackPacketCallback;

public:
    RFC8627FECDecoder();
    ~RFC8627FECDecoder();
    int32_t Init(uint8_t row, uint8_t col, uint8_t tableNum);
    bool SetDecoderPacketCallback(FECDecoderPacketCallback callback);
    bool SetNackPacketCallback(NackPacketCallback callback);
    bool SetPayloadType(uint8_t pt);
    bool SetSSRC(uint32_t ssrc);
    int32_t RecvPacket(const std::shared_ptr<Packet>& packet);

private:
    int32_t ReleaseAll();
    int32_t OutNackPacketIfNeed();
    std::shared_ptr<Packet> MakeNackPacket();
    void OnRTPPacket(const std::shared_ptr<Packet>& packet, bool isOutByRepair = false);
    void OutPacketThread();
    bool SkipPackets();
    bool IsHasRecvPacket(uint16_t seq);
    void RecvCachePacket(FEC2DTable* pFEC2DTable);

private:
    uint8_t m_nPayloadType;
    uint32_t m_nSSRC;

    std::list<FEC2DTable*> m_pTableList;
    std::list<std::shared_ptr<Packet>> m_pCachePacketList;
    FECDecoderPacketCallback m_pDecoderPacketCallback;
    NackPacketCallback m_pNackPacketCallback;

    bool m_bRecvFirstPacket;
    uint16_t m_nExpOutSeq;
    uint16_t m_nLastRecvSeq;
    uint16_t m_nLastOutSeq;
    std::mutex m_pRTPSortArrayLock;
    std::vector< std::shared_ptr<Packet>>m_pRTPSortArray;

    bool m_bStopOutPacket;
    std::thread* m_pOutPacketThread;
};
