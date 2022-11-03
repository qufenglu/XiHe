#include <stdlib.h>
#include <string.h>
#include "FlexibleBuff.h"

FlexibleBuff::FlexibleBuff()
{
    m_pBuff = nullptr;
    m_nBuffSIze = 0;
    m_nDataSize = 0;
}

FlexibleBuff::~FlexibleBuff()
{
    ReleaseAll();
}

int32_t FlexibleBuff::ReleaseAll()
{
    free(m_pBuff);
    m_pBuff = nullptr;
    m_nBuffSIze = 0;
    m_nDataSize = 0;

    return 0;
}

int32_t FlexibleBuff::Append(uint8_t* data, uint32_t size)
{
    if (m_pBuff == nullptr)
    {
        uint32_t mallocsize = size < 4 * 1024 ? 4 * 1024 : size << 1;
        m_pBuff = (uint8_t*)malloc(mallocsize);
        if (m_pBuff == nullptr)
        {
            return -1;
        }

        m_nBuffSIze = mallocsize;
        memcpy(m_pBuff, data, size);
        m_nDataSize = size;
    }
    else
    {
        if (m_nDataSize + size <= m_nBuffSIze)
        {
            memcpy(m_pBuff + m_nDataSize, data, size);
            m_nDataSize += size;
        }
        else
        {
            uint32_t mallocsize = (m_nBuffSIze << 1) > (m_nDataSize + size) ? m_nBuffSIze << 1 : m_nDataSize + size;
            uint8_t* pNewBuff = (uint8_t*)malloc(mallocsize);
            if (pNewBuff == nullptr)
            {
                return -2;
            }
            m_nBuffSIze = mallocsize;

            memcpy(pNewBuff, m_pBuff, m_nDataSize);
            memcpy(pNewBuff + m_nDataSize, data, size);
            free(m_pBuff);
            m_pBuff = pNewBuff;
            m_nDataSize += size;
        }
    }

    return 0;
}

int32_t FlexibleBuff::ClearBuff(uint32_t max)
{
    if (m_nBuffSIze > max)
    {
        ReleaseAll();
    }
    else
    {
        m_nDataSize = 0;
    }

    return 0;
}

int32_t FlexibleBuff::GetRawData(uint8_t*& data, uint32_t& size)
{
    data = m_pBuff;
    size = m_nDataSize;
    return 0;
}