#pragma once
#include <list>
#include <mutex>
#include <unordered_map>
#include "Common.h"
#include "FEC2DTable.h"

class RFC8627FECEncoder
{
public:
    typedef std::function<void(const std::shared_ptr<Packet>&)> FECEncoderPacketCallback;

public:
    RFC8627FECEncoder();
    ~RFC8627FECEncoder();
    int32_t Init(uint8_t row, uint8_t col);
    int32_t RecvRTPPacket(const std::shared_ptr<Packet>& packet);
    int32_t RecvNackPacket(const std::shared_ptr<Packet>& packet);
    bool SetFECEncoderPacketCallback(FECEncoderPacketCallback callback);
    bool SetPayloadType(uint8_t pt);
    bool SetSSRC(uint32_t ssrc);

private:
    int32_t ReleaseAll();
    void OnFECPacket(const std::shared_ptr<Packet>& packet);
    void OnRTPPacket(const std::shared_ptr<Packet>& packet);
    void CacheRTPPacket(const std::shared_ptr<Packet>& packet);

private:
    uint8_t m_nPayloadType;
    uint16_t m_nSeq;
    uint32_t m_nSSRC;
    FEC2DTable* m_pFEC2DTable;
    FECEncoderPacketCallback m_pEncoderPacketCallback;

    std::list<uint16_t> m_cCacheList;
    std::unordered_map<uint16_t, std::shared_ptr<Packet>> m_cCacheMap;
    std::mutex m_cCachePacketLock;
};
