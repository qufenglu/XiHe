#include "FEC2DTable.h"
#include "Log/Log.h"

FEC2DTable::FEC2DTable(uint8_t pt)
{
    m_nPayloadType = pt;
    m_nBaseSeq = -1;
    m_nRowNum = 0;
    m_nColumnNum = 0;
    m_pRowCounter = nullptr;
    m_pColumnCounter = nullptr;
    m_pFecTable = nullptr;
    m_pRowRepairPacket = nullptr;
    m_pColumnRepairPacket = nullptr;
    m_pFECPacketCallback = nullptr;
    m_pRTPPacketCallback = nullptr;
}

FEC2DTable::~FEC2DTable()
{
    ReleaseAll();
}

int32_t FEC2DTable::ReleaseAll()
{
    if (m_pFecTable != nullptr)
    {
        for (uint8_t row = 0; row < m_nRowNum; row++)
        {
            if (m_pFecTable[row] != nullptr)
            {
                for (uint8_t col = 0; col < m_nColumnNum; col++)
                {
                    if (m_pFecTable[row, col] != nullptr)
                    {
                        m_pFecTable[row, col] = nullptr;
                    }
                }

                delete[] m_pFecTable[row];
                m_pFecTable[row] = nullptr;
            }
        }

        free(m_pFecTable);
        m_pFecTable = nullptr;
    }

    free(m_pRowCounter);
    m_pRowCounter = nullptr;
    free(m_pColumnCounter);
    m_pColumnCounter = nullptr;

    if (m_pRowRepairPacket != nullptr)
    {
        for (uint8_t row = 0; row < m_nRowNum; row++)
        {
            if (m_pRowRepairPacket[row] != nullptr)
            {
                m_pRowRepairPacket[row] = nullptr;
            }
        }
        delete[] m_pRowRepairPacket;
        m_pRowRepairPacket = nullptr;
    }
    if (m_pColumnRepairPacket != nullptr)
    {
        for (uint8_t col = 0; col < m_nColumnNum; col++)
        {
            if (m_pColumnRepairPacket[col] != nullptr)
            {
                m_pColumnRepairPacket[col] = nullptr;
            }
        }
        delete[] m_pColumnRepairPacket;
        m_pColumnRepairPacket = nullptr;
    }

    m_nRowNum = 0;
    m_nColumnNum = 0;
    m_nBaseSeq = -1;

    return 0;
}

int32_t FEC2DTable::Init(uint8_t row, uint8_t column)
{
    if (row == 0 || column == 0 || row > MAX_FEC_LINE || column > MAX_FEC_LINE)
    {
        Error("[%p][FEC2DTable::Init] param row:%d or column:%d err", this, row, column);
        return -1;
    }

    m_nRowNum = row;
    m_nColumnNum = column;

    int32_t ret = -1;
    size_t nMallocSize = sizeof(uint8_t) * m_nRowNum;
    m_pRowCounter = (uint8_t*)malloc(nMallocSize);
    if (m_pRowCounter == nullptr)
    {
        Error("[%p][FEC2DTable::Init] malloc row counter  fali", this);
        ret = -2;
        goto fail;
    }
    memset(m_pRowCounter, 0, nMallocSize);

    nMallocSize = sizeof(uint8_t) * m_nColumnNum;
    m_pColumnCounter = (uint8_t*)malloc(nMallocSize);
    if (m_pColumnCounter == nullptr)
    {
        Error("[%p][FEC2DTable::Init] malloc column counter  fali", this);
        ret = -3;
        goto fail;
    }
    memset(m_pColumnCounter, 0, nMallocSize);

    nMallocSize = sizeof(std::shared_ptr<Packet>*) * m_nRowNum;
    m_pFecTable = (std::shared_ptr<Packet>**)malloc(nMallocSize);
    if (m_pFecTable == nullptr)
    {
        Error("[%p][FEC2DTable::Init] malloc fec table  fali", this);
        ret = -4;
        goto fail;
    }
    memset(m_pFecTable, 0, nMallocSize);
    for (uint32_t i = 0; i < m_nRowNum; i++)
    {
        m_pFecTable[i] = new std::shared_ptr<Packet>[m_nColumnNum]();
        if (m_pFecTable[i] == nullptr)
        {
            Error("[%p][FEC2DTable::Init] malloc fec table row:%d  fali", this, i);
            ret = -5;
            goto fail;
        }
    }

    m_pRowRepairPacket = new std::shared_ptr<Packet>[m_nRowNum]();
    if (m_pRowRepairPacket == nullptr)
    {
        Error("[%p][FEC2DTable::Init] malloc row repair packets  fali", this);
        ret = -6;
        goto fail;
    }

    m_pColumnRepairPacket = new std::shared_ptr<Packet>[m_nColumnNum]();
    if (m_pColumnRepairPacket == nullptr)
    {
        Error("[%p][FEC2DTable::Init] malloc column repair packets  fali", this);
        ret = -7;
        goto fail;
    }

    return 0;
fail:
    ReleaseAll();
    return ret;
}

void FEC2DTable::ClearTable()
{
    if (m_pFecTable != nullptr)
    {
        for (uint8_t row = 0; row < m_nRowNum; row++)
        {
            if (m_pFecTable[row] != nullptr)
            {
                for (uint8_t col = 0; col < m_nColumnNum; col++)
                {
                    m_pFecTable[row, col] = nullptr;
                }
            }
        }
    }

    if (m_pRowCounter != nullptr)
    {
        memset(m_pRowCounter, 0, sizeof(uint32_t) * m_nRowNum);
    }
    if (m_pColumnCounter != nullptr)
    {
        memset(m_pColumnCounter, 0, sizeof(uint32_t) * m_nColumnNum);
    }

    if (m_pRowRepairPacket != nullptr)
    {
        for (uint8_t row = 0; row < m_nRowNum; row++)
        {
            m_pRowRepairPacket[row] = nullptr;
        }
    }
    if (m_pColumnRepairPacket != nullptr)
    {
        for (uint8_t col = 0; col < m_nColumnNum; col++)
        {
            m_pColumnRepairPacket[col] = nullptr;
        }
    }

    m_nBaseSeq = -1;
    m_Range1.max = INT32_MIN;
    m_Range1.min = INT32_MAX;
    m_Range2.max = INT32_MIN;
    m_Range2.min = INT32_MAX;
}

bool FEC2DTable::SetFECPacketCallback(FECPacketCallback callback)
{
    m_pFECPacketCallback = callback;
    return true;
}

bool FEC2DTable::SetRTPPacketCallback(RTPPacketCallback callback)
{
    m_pRTPPacketCallback = callback;
    return true;
}

bool FEC2DTable::IsSeqInRange(uint16_t seq)
{
    bool ret1 = seq < m_Range1.min ? false :
        seq > m_Range1.max ? false : true;

    bool ret2 = seq < m_Range2.min ? false :
        seq>m_Range2.max ? false : true;

    return ret1 || ret2;
}

void FEC2DTable::UpdataRange(uint16_t seq)
{
    m_nBaseSeq = seq;
    int32_t nMaxSeq = m_nBaseSeq + (m_nRowNum * m_nColumnNum) - 1;
    m_Range1.min = seq;
    if (nMaxSeq < 65536)
    {
        m_Range1.max = nMaxSeq;
    }
    else
    {
        m_Range1.max = 65535;
        m_Range2.min = 0;
        m_Range2.max = nMaxSeq - 65536;
    }
}

void FEC2DTable::CalculateRowAndColumn(uint16_t seq, uint32_t& row, uint32_t& col)
{
    int32_t nSeq = seq;
    if (nSeq < m_nBaseSeq)
    {
        nSeq += 65536;
    }
    uint32_t n = nSeq - m_nBaseSeq + 1;
    row = n / m_nColumnNum;
    col = n % m_nColumnNum - 1;
}

int32_t FEC2DTable::RecvPacketAndMakeRepair(const std::shared_ptr<Packet>& packet)
{
    uint16_t seq = (packet->m_pData[2] << 8) | packet->m_pData[3];
    if (m_nBaseSeq == -1)
    {
        UpdataRange(seq);
    }

    if (IsSeqInRange(seq))
    {
        uint32_t row = 0;
        uint32_t col = 0;
        CalculateRowAndColumn(seq, row, col);
        if (m_pFecTable[row][col] == nullptr)
        {
            m_pFecTable[row][col] = packet;
            m_pRowCounter[row]++;
            m_pColumnCounter[col]++;

            if (m_pRowCounter[row] == m_nColumnNum)
            {
                std::shared_ptr<Packet>* pRepairPacket = CreateRepairPacketByRow(row);
                if (pRepairPacket == nullptr)
                {
                    Error("[%p][FEC2DTable::RecvPacketAndMakeRepair] create repair packet by row:%d fail", this, row);
                }
                else
                {
                    m_pRowRepairPacket[row] = *pRepairPacket;
                    OutputFECPacket(*pRepairPacket);

                    *pRepairPacket = nullptr;
                    delete pRepairPacket;
                }
            }
            if (m_pColumnCounter[col] == m_nRowNum)
            {
                std::shared_ptr<Packet>* pRepairPacket = CreateRepairPacketByColumn(col);
                if (pRepairPacket == nullptr)
                {
                    Error("[%p][FEC2DTable::RecvPacketAndMakeRepair] create repair packet by col:%d fail", this, col);
                }
                else
                {
                    m_pColumnRepairPacket[col] = *pRepairPacket;
                    OutputFECPacket(*pRepairPacket);

                    *pRepairPacket = nullptr;
                    delete pRepairPacket;
                }
            }
        }
    }
    else
    {
        ClearTable();
        return RecvPacketAndMakeRepair(packet);
    }

    return 0;
}

std::shared_ptr<Packet>* FEC2DTable::CreateRepairPacket(uint8_t** data, uint16_t* size, uint32_t nPackNum, uint32_t nMaxLen)
{
    size_t nRepairPacketSize = nMaxLen + 12;
    uint8_t* pRepairPacketData = (uint8_t*)malloc(nRepairPacketSize);
    if (pRepairPacketData == nullptr)
    {
        Error("[%p][FEC2DTable::CreateRepairPacketByRow] malloc repair packet fail", this);
        return nullptr;
    }
    memset(pRepairPacketData, 0, nRepairPacketSize);

    //RTP Head
    pRepairPacketData[0] = 0x80;
    //pRepairPacketData[1]
    //pRepairPacketData[2]
    //pRepairPacketData[3]
    pRepairPacketData[4] = data[nPackNum - 1][4];
    pRepairPacketData[5] = data[nPackNum - 1][5];
    pRepairPacketData[6] = data[nPackNum - 1][6];
    pRepairPacketData[7] = data[nPackNum - 1][7];
    //pRepairPacketData[8]
    //pRepairPacketData[9] 
    //pRepairPacketData[10]
    //pRepairPacketData[11]

    //FEC Head
    uint8_t nTempByte = 0;
    for (uint32_t i = 0; i < nPackNum; i++)
    {
        nTempByte ^= data[i][0];
        pRepairPacketData[13] ^= data[i][1];
        pRepairPacketData[16] ^= data[i][4];
        pRepairPacketData[17] ^= data[i][5];
        pRepairPacketData[18] ^= data[i][6];
        pRepairPacketData[19] ^= data[i][7];
    }
    pRepairPacketData[12] = 0x40 | (nTempByte & 0x3f);
    uint16_t nTempSize = 0;
    for (uint32_t i = 0; i < nPackNum; i++)
    {
        nTempSize ^= size[i];
    }
    pRepairPacketData[14] = nTempSize >> 8;
    pRepairPacketData[15] = nTempSize & 0xff;
    pRepairPacketData[20] = m_nBaseSeq >> 8;
    pRepairPacketData[21] = m_nBaseSeq & 0xff;
    // L(columns) and D(rows) be filled in outside
    //pRepairPacketData[22];
    //pRepairPacketData[23];

    //FEC Repair Payload
    for (uint32_t i = 0; i < nPackNum; i++)
    {
        for (uint32_t j = 12; j < nMaxLen; j++)
        {
            if (j >= size[i])
            {
                continue;
            }

            pRepairPacketData[j + 12] ^= data[i][j];
        }
    }

    std::shared_ptr<Packet>* pRepairPacket = new std::shared_ptr<Packet>();
    (*pRepairPacket)->m_nLength = nRepairPacketSize;
    (*pRepairPacket)->m_pData = pRepairPacketData;

    return pRepairPacket;
}

std::shared_ptr<Packet>* FEC2DTable::CreateRepairPacketByRow(uint32_t row)
{
    if (m_pRowRepairPacket[row] != nullptr)
    {
        return nullptr;
    }

    uint32_t nMaxLen = 0;
    uint32_t nPackNum = 0;
    uint8_t* data[MAX_FEC_LINE] = { 0 };
    uint16_t size[MAX_FEC_LINE] = { 0 };
    for (uint8_t col = 0; col < m_nColumnNum; col++)
    {
        if (m_pFecTable[row][col] != nullptr)
        {
            if (nMaxLen < m_pFecTable[row][col]->m_nLength)
            {
                nMaxLen = m_pFecTable[row][col]->m_nLength;
            }
            data[nPackNum] = m_pFecTable[row][col]->m_pData;
            size[nPackNum] = m_pFecTable[row][col]->m_nLength;
            nPackNum++;
        }
        else
        {
            break;
        }
    }
    if (nPackNum < m_nColumnNum)
    {
        return nullptr;
    }

    std::shared_ptr<Packet>* pRepairPacket = CreateRepairPacket(data, size, nPackNum, nMaxLen);
    if (pRepairPacket != nullptr)
    {
        (*pRepairPacket)->m_pData[22] = m_nColumnNum + 1;
        (*pRepairPacket)->m_pData[23] = row + 1;
    }

    return pRepairPacket;
}

std::shared_ptr<Packet>* FEC2DTable::CreateRepairPacketByColumn(uint32_t col)
{
    if (m_pColumnRepairPacket[col] != nullptr)
    {
        return nullptr;
    }

    uint32_t nMaxLen = 0;
    uint32_t nPackNum = 0;
    uint8_t* data[MAX_FEC_LINE] = { 0 };
    uint16_t size[MAX_FEC_LINE] = { 0 };
    for (uint8_t row = 0; row < m_nRowNum; row++)
    {
        if (m_pFecTable[row][col] != nullptr)
        {
            if (nMaxLen < m_pFecTable[row][col]->m_nLength)
            {
                nMaxLen = m_pFecTable[row][col]->m_nLength;
            }
            data[nPackNum] = m_pFecTable[row][col]->m_pData;
            size[nPackNum] = m_pFecTable[row][col]->m_nLength;
            nPackNum++;
        }
        else
        {
            break;
        }
    }
    if (nPackNum < m_nRowNum)
    {
        return nullptr;
    }

    std::shared_ptr<Packet>* pRepairPacket = CreateRepairPacket(data, size, nPackNum, nMaxLen);
    if (pRepairPacket != nullptr)
    {
        (*pRepairPacket)->m_pData[22] = col + 1;
        (*pRepairPacket)->m_pData[23] = m_nRowNum + 1;
    }

    return pRepairPacket;
}

void FEC2DTable::OutputFECPacket(const std::shared_ptr<Packet>& packet)
{
    if (m_pFECPacketCallback != nullptr)
    {
        m_pFECPacketCallback(packet);
    }
}

void FEC2DTable::OutputRTPPacket(const std::shared_ptr<Packet>& packet)
{
    if (m_pRTPPacketCallback != nullptr)
    {
        m_pRTPPacketCallback(packet);
    }
}

int32_t FEC2DTable::RecvPacketAndTryRepair(const std::shared_ptr<Packet>& packet)
{
    if (!IsCanRecvPacket(packet))
    {
        Error("[%p][FEC2DTable::RecvPacketAndTryRepair]  table can not recv this packet", this);
        return -1;
    }

    uint8_t pt = packet->m_pData[1] & 0x7f;
    uint32_t row = 0;
    uint32_t col = 0;
    bool bIsRepairPacket = pt == m_nPayloadType ? true : false;
    if (bIsRepairPacket)
    {
        if (m_nBaseSeq == -1)
        {
            uint16_t seq = (packet->m_pData[12 + 8] << 8) | (packet->m_pData[12 + 9]);
            UpdataRange(seq);
        }

        col = packet->m_pData[22] - 1;
        row = packet->m_pData[23] - 1;
        if (col == m_nColumnNum)
        {
            if (row >= m_nRowNum)
            {
                Error("[%p][FEC2DTable::RecvPacketAndTryRepair] col:%d  row:%d is illage,table col:%d row:%d",
                    this, col, row, m_nColumnNum, m_nRowNum);
                return -2;
            }
            if (m_pRowRepairPacket[row] != nullptr)
            {
                m_pRowRepairPacket[row] = packet;
            }
        }
        else if (row == m_nRowNum)
        {
            if (col >= m_nColumnNum)
            {
                Error("[%p][FEC2DTable::RecvPacketAndTryRepair] col:%d  row:%d is illage,table col:%d row:%d",
                    this, col, row, m_nColumnNum, m_nRowNum);
                return -3;
            }
            if (m_pColumnRepairPacket[col] != nullptr)
            {
                m_pColumnRepairPacket[col] = packet;
            }
        }
        else
        {
            Error("[%p][FEC2DTable::RecvPacketAndTryRepair] col:%d  row:%d is illage,table col:%d row:%d",
                this, col, row, m_nColumnNum, m_nRowNum);
            return -4;
        }
    }
    else
    {
        uint16_t seq = (packet->m_pData[2] << 8) | packet->m_pData[3];
        CalculateRowAndColumn(seq, row, col);
        if (m_pFecTable[row][col] == nullptr)
        {
            m_pFecTable[row][col] = packet;
            m_pRowCounter[row]++;
            m_pColumnCounter[col]++;
        }
    }

    std::shared_ptr<Packet>* pFixedPacket = nullptr;
    pFixedPacket = TryRepairByColumn(col);
    if (pFixedPacket != nullptr)
    {
        OutputRTPPacket(*pFixedPacket);
        RecvPacketAndTryRepair(*pFixedPacket);
        (*pFixedPacket) = nullptr;
        delete pFixedPacket;
    }

    pFixedPacket = TryRepairByRow(row);
    if (pFixedPacket != nullptr)
    {
        OutputRTPPacket(*pFixedPacket);
        RecvPacketAndTryRepair(*pFixedPacket);
        (*pFixedPacket) = nullptr;
        delete pFixedPacket;
    }

    return 0;
}

bool FEC2DTable::IsCanRecvPacket(const std::shared_ptr<Packet>& packet)
{
    if (m_pFecTable == nullptr)
    {
        return false;
    }

    uint8_t pt = packet->m_pData[1] & 0x7f;
    if (pt == m_nPayloadType)
    {
        if (m_nBaseSeq == -1)
        {
            return true;
        }

        uint16_t baseSeq = (packet->m_pData[12 + 8] << 8) | (packet->m_pData[12 + 9]);
        if (m_nBaseSeq == baseSeq)
        {
            return true;
        }
        else
        {
            return false;
        }
    }
    else
    {
        uint16_t seq = (packet->m_pData[2] << 8) | packet->m_pData[3];
        if (!IsSeqInRange(seq))
        {
            return false;
        }
    }

    return true;
}

std::shared_ptr<Packet>* FEC2DTable::TryRepairByRow(uint32_t row)
{
    std::shared_ptr<Packet>* packet = nullptr;
    if (m_pRowCounter[row] == (m_nRowNum - 1) && m_pRowRepairPacket[row] != nullptr)
    {
        uint32_t nPackNum = 0;
        uint8_t* data[MAX_FEC_LINE] = { 0 };
        uint16_t size[MAX_FEC_LINE] = { 0 };
        uint16_t seq = 0;

        for (uint8_t col = 0; col < m_nColumnNum; col++)
        {
            if (m_pFecTable[row][col] != nullptr)
            {
                data[nPackNum] = m_pFecTable[row][col]->m_pData;
                size[nPackNum] = m_pFecTable[row][col]->m_nLength;
                nPackNum++;
            }
            else
            {
                seq = m_nBaseSeq + row * m_nColumnNum + col;
            }
        }

        if (nPackNum == (m_nColumnNum - 1))
        {
            packet = Repair(data, size, nPackNum, m_pRowRepairPacket[row]);
            if (packet != nullptr)
            {
                (*packet)->m_pData[2] = seq >> 8;
                (*packet)->m_pData[3] = seq & 0xff;
            }
        }
    }

    return packet;
}

std::shared_ptr<Packet>* FEC2DTable::TryRepairByColumn(uint32_t col)
{
    std::shared_ptr<Packet>* packet = nullptr;
    if (m_pColumnCounter[col] == m_nColumnNum && m_pColumnRepairPacket[col] != nullptr)
    {
        uint32_t nPackNum = 0;
        uint8_t* data[MAX_FEC_LINE] = { 0 };
        uint16_t size[MAX_FEC_LINE] = { 0 };
        uint16_t seq = 0;

        for (uint8_t row = 0; row < m_nRowNum; row++)
        {
            if (m_pFecTable[row][col] != nullptr)
            {
                data[nPackNum] = m_pFecTable[row][col]->m_pData;
                size[nPackNum] = m_pFecTable[row][col]->m_nLength;
                nPackNum++;
            }
            else
            {
                seq = m_nBaseSeq + row * m_nColumnNum + col;
                if (packet != nullptr)
                {
                    (*packet)->m_pData[2] = seq >> 8;
                    (*packet)->m_pData[3] = seq & 0xff;
                }
            }
        }

        if (nPackNum == (m_nRowNum - 1))
        {
            packet = Repair(data, size, nPackNum, m_pColumnRepairPacket[col]);
        }
    }

    return packet;
}

std::shared_ptr<Packet>* FEC2DTable::Repair(uint8_t** data, uint16_t* size, uint32_t nPackNum, const std::shared_ptr<Packet>& repair)
{
    uint16_t nPacketSize = 0;
    for (uint32_t i = 0; i < nPackNum; i++)
    {
        nPacketSize ^= size[i];
    }
    uint16_t nRepairSize = (repair->m_pData[14] << 8) | repair->m_pData[15];
    nPacketSize ^= nRepairSize;
    if (nPacketSize + 12 > repair->m_nLength)
    {
        Error("[%p][FEC2DTable::Repair] repair size:%d > repair packet size:%d - 12", this, nRepairSize, repair->m_nLength);
        return nullptr;
    }

    uint8_t* pPacketData = (uint8_t*)malloc(nRepairSize);
    if (pPacketData == nullptr)
    {
        Error("[%p][FEC2DTable::Repair] malloc packet data err,size:%d", this, nRepairSize);
        return nullptr;
    }
    memset(pPacketData, 0, nRepairSize);

    uint8_t nP_X_CC_Byte = 0;		//–ﬁ∏¥P°¢X°¢CC
    uint8_t nM_PT_Byte;				//–ﬁ∏¥M°¢PT
    uint8_t nTSBytes[4];					//–ﬁ∏¥TS
    for (uint32_t i = 0; i < nPackNum; i++)
    {
        nP_X_CC_Byte ^= data[i][0];
        nM_PT_Byte ^= data[i][1];
        nTSBytes[0] ^= data[i][4];
        nTSBytes[1] ^= data[i][5];
        nTSBytes[2] ^= data[i][6];
        nTSBytes[3] ^= data[i][7];
    }
    nP_X_CC_Byte ^= repair->m_pData[12];
    nM_PT_Byte ^= repair->m_pData[13];
    nTSBytes[0] ^= repair->m_pData[16];
    nTSBytes[1] ^= repair->m_pData[17];
    nTSBytes[2] ^= repair->m_pData[18];
    nTSBytes[3] ^= repair->m_pData[19];

    pPacketData[0] = 0x80 | (nP_X_CC_Byte & 0x3f);
    pPacketData[1] = nM_PT_Byte;
    //seq”…Õ‚≤øÃÓ≥‰
    //pPacketData[2]
    //pPacketData[3]
    pPacketData[4] = nTSBytes[0];
    pPacketData[5] = nTSBytes[1];
    pPacketData[6] = nTSBytes[2];
    pPacketData[7] = nTSBytes[3];
    pPacketData[8] = data[0][8];
    pPacketData[9] = data[0][9];
    pPacketData[10] = data[0][10];
    pPacketData[11] = data[0][11];

    for (uint32_t i = 0; i < nPackNum; i++)
    {
        for (uint16_t j = 12; j < nRepairSize; j++)
        {
            if (j >= size[i])
            {
                continue;
            }

            pPacketData[j] ^= data[i][j];
        }
    }

    for (int i = 12; i < nRepairSize; i++)
    {
        pPacketData[i] ^= repair->m_pData[i + 12];
    }

    std::shared_ptr<Packet>* packet = new std::shared_ptr<Packet>();
    (*packet)->m_pData = pPacketData;
    (*packet)->m_nLength = nRepairSize;

    return packet;
}

void FEC2DTable::GetMinimalRepairSet(std::unordered_set<uint16_t>& repair)
{
    repair.clear();
    if (m_FEC2DBitTable.FillBitTable(*this))
    {
        m_FEC2DBitTable.GetMinimalRepairSet(repair);
    }
}

FEC2DTable::FEC2DBitTable::FEC2DBitTable()
{
    m_nBaseSeq = -1;
    m_nRowNum = 0;
    m_nColumnNum = 0;
    m_pFecBitTable = nullptr;
    m_pRowRepairBit = nullptr;
    m_pColumnRepairBit = nullptr;
    m_pRowCounter = nullptr;
    m_pColumnCounter = nullptr;
}

FEC2DTable::FEC2DBitTable::~FEC2DBitTable()
{
    ReleaseAll();
}

int32_t FEC2DTable::FEC2DBitTable::ReleaseAll()
{
    if (m_pFecBitTable != nullptr)
    {
        for (int i = 0; i < m_nRowNum; i++)
        {
            free(m_pFecBitTable[i]);
        }

        free(m_pFecBitTable);
        m_pFecBitTable = nullptr;
    }

    free(m_pRowRepairBit);
    m_pRowRepairBit = nullptr;
    free(m_pColumnRepairBit);
    m_pColumnRepairBit = nullptr;
    free(m_pRowCounter);
    m_pRowCounter = nullptr;
    free(m_pColumnCounter);
    m_pColumnCounter = nullptr;

    m_nBaseSeq = -1;
    m_nRowNum = 0;
    m_nColumnNum = 0;

    return 0;
}

bool FEC2DTable::FEC2DBitTable::FillBitTable(const FEC2DTable& table)
{
    bool isTableInited = table.m_nBaseSeq == -1 ? false :
        table.m_nColumnNum == 0 ? false :
        table.m_nRowNum == 0 ? false :
        table.m_pFecTable == nullptr ? false :
        table.m_pColumnRepairPacket == nullptr ? false :
        table.m_pRowRepairPacket == nullptr ? false : true;

    if (!isTableInited)
    {
        return false;
    }

    if (m_nColumnNum != table.m_nColumnNum || m_nRowNum != table.m_nRowNum)
    {
        ReleaseAll();
        m_nBaseSeq = table.m_nBaseSeq;
        m_nRowNum = table.m_nRowNum;
        m_nColumnNum = table.m_nColumnNum;

        m_pFecBitTable = (bool**)malloc(sizeof(bool*) * m_nRowNum);
        if (m_pFecBitTable == nullptr)
        {
            Error("[%p][FEC2DBitTable::FillBitTable] malloc fec bit table fail", this);
            goto fail;
        }
        memset(m_pFecBitTable, 0, sizeof(bool*) * m_nRowNum);
        for (int i = 0; i < m_nRowNum; i++)
        {
            m_pFecBitTable[i] = (bool*)malloc(sizeof(bool) * m_nColumnNum);
            if (m_pFecBitTable[i] == nullptr)
            {
                Error("[%p][FEC2DBitTable::FillBitTable] malloc fec bit table fail", this);
                goto fail;
            }
        }
        m_pRowRepairBit = (bool*)malloc(sizeof(bool) * m_nRowNum);
        if (m_pRowRepairBit == nullptr)
        {
            Error("[%p][FEC2DBitTable::FillBitTable] malloc row repair bit fail", this);
            goto fail;
        }
        m_pColumnRepairBit = (bool*)malloc(sizeof(bool) * m_nColumnNum);
        if (m_pColumnRepairBit == nullptr)
        {
            Error("[%p][FEC2DBitTable::FillBitTable] malloc column repair bit fail", this);
            goto fail;
        }
        m_pRowCounter = (uint8_t*)malloc(sizeof(uint8_t) * m_nRowNum);
        if (m_pRowCounter == nullptr)
        {
            Error("[%p][FEC2DBitTable::FillBitTable] malloc row counter fail", this);
            goto fail;
        }
        m_pColumnCounter = (uint8_t*)malloc(sizeof(uint8_t) * m_nColumnNum);
        if (m_pColumnCounter == nullptr)
        {
            Error("[%p][FEC2DBitTable::FillBitTable] malloc column counter fail", this);
            goto fail;
        }
    }

    memset(m_pColumnCounter, 0, m_nColumnNum);
    memset(m_pRowCounter, 0, m_nRowNum);

    for (int i = 0; i < m_nRowNum; i++)
    {
        for (int j = 0; j < m_nColumnNum; j++)
        {
            if (table.m_pFecTable[i][j] != nullptr)
            {
                m_pFecBitTable[i][j] = true;
                m_pRowCounter[i]++;
                m_pColumnCounter[j]++;
            }
            else
            {
                m_pFecBitTable[i][j] = false;
            }
        }
    }
    for (int i = 0; i < m_nRowNum; i++)
    {
        if (table.m_pRowRepairPacket[i] != nullptr)
        {
            m_pRowRepairBit[i] = true;
        }
        else
        {
            m_pRowRepairBit[i] = false;
        }
    }
    for (int i = 0; i < m_nColumnNum; i++)
    {
        if (table.m_pColumnRepairPacket[i] != nullptr)
        {
            m_pColumnRepairBit[i] = true;
        }
        else
        {
            m_pColumnRepairBit[i] = false;
        }
    }

    return true;
fail:
    ReleaseAll();
    return false;
}

void  FEC2DTable::FEC2DBitTable::AddBit2Table(uint16_t seq)
{
    int32_t nSeq = seq;
    if (nSeq < m_nBaseSeq)
    {
        nSeq += 65536;
    }
    uint32_t n = nSeq - m_nBaseSeq + 1;
    uint8_t row = n / m_nColumnNum;
    uint8_t col = n % m_nColumnNum - 1;

    if (m_pFecBitTable[row][col])
    {
        return;
    }

    m_pFecBitTable[row][col] = true;
    m_pRowCounter[row]++;
    m_pColumnCounter[col]++;

    if (m_pRowRepairBit[row] && m_pRowCounter[row] == m_nRowNum - 1)
    {
        for (uint8_t c = 0; c < m_nColumnNum; c++)
        {
            if (!m_pFecBitTable[row][c])
            {
                uint16_t n = m_nBaseSeq + row * m_nColumnNum + c;
                AddBit2Table(n);
            }
        }
    }
    if (m_pColumnRepairBit[col] && m_pColumnCounter[col] == m_nColumnNum - 1)
    {
        for (uint8_t r = 0; r < m_nRowNum; r++)
        {
            if (!m_pFecBitTable[r][col])
            {
                uint16_t n = m_nBaseSeq + r * m_nColumnNum + col;
                AddBit2Table(n);
            }
        }
    }
}

void FEC2DTable::FEC2DBitTable::GetMinimalRepairSet(std::unordered_set<uint16_t>& repair)
{
    std::unordered_set<uint16_t> lost;
    for (uint8_t row = 0; row < m_nRowNum; row++)
    {
        for (uint8_t col = 0; col < m_nColumnNum; col++)
        {
            if (!m_pFecBitTable[row][col])
            {
                uint16_t seq = m_nBaseSeq + row * m_nColumnNum + col;
                lost.insert(seq);
                AddBit2Table(seq);
            }
        }
    }
}