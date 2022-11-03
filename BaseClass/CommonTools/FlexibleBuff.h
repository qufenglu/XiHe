#pragma once
#include <cstdint>

class FlexibleBuff
{
public:
    FlexibleBuff();
    ~FlexibleBuff();

    int32_t Append(uint8_t* data, uint32_t size);
    int32_t ClearBuff(uint32_t max = 512 * 1024);
    int32_t GetRawData(uint8_t*& data, uint32_t& size);
    inline uint32_t GetDataSize() { return m_nDataSize; };

private:
    int32_t ReleaseAll();

private:
    uint8_t* m_pBuff;
    uint32_t m_nBuffSIze;
    uint32_t m_nDataSize;
};
