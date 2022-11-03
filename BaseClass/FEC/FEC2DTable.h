#pragma once
#include <memory>
#include <cstdint>
#include <functional>
#include <unordered_set>
#include "Common.h"
#define MAX_FEC_LINE 32		//FEC最大行/列数

typedef struct NackItem
{
    uint16_t PID;		//packet id
    uint16_t BLP;		//bitmask of following lost packets

    NackItem()
    {
        PID = 0;
        BLP = 0;
    }
}NackItem;

class FEC2DTable
{
public:
    typedef std::function<void(const std::shared_ptr<Packet>&)> FECPacketCallback;
    typedef std::function<void(const std::shared_ptr<Packet>&)> RTPPacketCallback;
    typedef struct Range
    {
        int32_t min = INT32_MAX;
        int32_t max = INT32_MIN;
    }Range;

public:
    FEC2DTable(uint8_t pt);
    ~FEC2DTable();

    int32_t Init(uint8_t row, uint8_t column);
    bool SetFECPacketCallback(FECPacketCallback callback);
    bool SetRTPPacketCallback(RTPPacketCallback callback);
    void ClearTable();
    int32_t RecvPacketAndMakeRepair(const std::shared_ptr<Packet>& packet);
    int32_t RecvPacketAndTryRepair(const std::shared_ptr<Packet>& packet);
    bool IsCanRecvPacket(const std::shared_ptr<Packet>& packet);
    void GetMinimalRepairSet(std::unordered_set<uint16_t>& repair);

private:
    int32_t ReleaseAll();
    bool IsSeqInRange(uint16_t seq);
    void UpdataRange(uint16_t seq);
    void CalculateRowAndColumn(uint16_t seq, uint32_t& row, uint32_t& col);
    std::shared_ptr<Packet>* CreateRepairPacketByRow(uint32_t row);
    std::shared_ptr<Packet>* CreateRepairPacketByColumn(uint32_t col);
    std::shared_ptr<Packet>* CreateRepairPacket(uint8_t** data, uint16_t* size, uint32_t nPackNum, uint32_t nMaxLen);
    void OutputFECPacket(const std::shared_ptr<Packet>& packet);
    void OutputRTPPacket(const std::shared_ptr<Packet>& packet);
    std::shared_ptr<Packet>* TryRepairByRow(uint32_t row);
    std::shared_ptr<Packet>* TryRepairByColumn(uint32_t col);
    std::shared_ptr<Packet>* Repair(uint8_t** data, uint16_t* size, uint32_t nPackNum, const std::shared_ptr<Packet>& repair);

private:
    class FEC2DBitTable
    {
    public:
        FEC2DBitTable();
        ~FEC2DBitTable();
        void GetMinimalRepairSet(std::unordered_set<uint16_t>& repair);
        bool FillBitTable(const FEC2DTable& table);

    private:
        int32_t ReleaseAll();
        void AddBit2Table(uint16_t seq);

    private:
        int32_t m_nBaseSeq;
        uint8_t m_nRowNum;
        uint8_t m_nColumnNum;
        bool** m_pFecBitTable;
        bool* m_pRowRepairBit;
        bool* m_pColumnRepairBit;
        uint8_t* m_pRowCounter;
        uint8_t* m_pColumnCounter;
    };

private:
    uint8_t m_nPayloadType;
    int32_t m_nBaseSeq;						//2D Table首包seq
    uint8_t m_nRowNum;						//2D Table行数
    uint8_t m_nColumnNum;				//2D Table列数
    uint8_t* m_pRowCounter;				//2D Table行包数
    uint8_t* m_pColumnCounter;			//2D Table列包数

    FEC2DBitTable m_FEC2DBitTable;			//计算最小修复集使用

    std::shared_ptr<Packet>** m_pFecTable;
    std::shared_ptr<Packet>* m_pRowRepairPacket;
    std::shared_ptr<Packet>* m_pColumnRepairPacket;

    Range m_Range1;
    Range m_Range2;

    FECPacketCallback m_pFECPacketCallback;
    RTPPacketCallback m_pRTPPacketCallback;
};