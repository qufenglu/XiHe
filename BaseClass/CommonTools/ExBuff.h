#pragma once
#include <cstdint>

class ExBuff
{
public:
    ExBuff();
    ~ExBuff();

    int32_t Append(uint8_t* data, uint32_t size);
    int32_t Read(uint8_t*& data, uint32_t size);
    int32_t GetRawData(uint8_t*& data, uint32_t& size);
    inline uint32_t GetDataSize() { return m_nWritePos - m_nReadPos; };
    int32_t ClearBuff(uint32_t max = 16 * 1024);

private:
    int32_t ReleaseAll();

private:
    uint8_t* m_pBuff;
    uint32_t m_nBuffSIze;
    uint32_t m_nReadPos;
    uint32_t m_nWritePos;
};