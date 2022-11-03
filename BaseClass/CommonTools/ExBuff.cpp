#include <stdlib.h>
#include <string.h>
#include "ExBuff.h"

ExBuff::ExBuff()
{
    m_pBuff = nullptr;
    m_nBuffSIze = 0;
    m_nReadPos = 0;
    m_nWritePos = 0;
}

ExBuff::~ExBuff()
{
    ReleaseAll();
}

int32_t ExBuff::ReleaseAll()
{
    if (m_pBuff != nullptr)
    {
        free(m_pBuff);
        m_pBuff = nullptr;
    }
    m_nBuffSIze = 0;
    m_nReadPos = 0;
    m_nWritePos = 0;

    return 0;
}

int32_t ExBuff::Append(uint8_t* data, uint32_t size)
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
        m_nReadPos = 0;
        m_nWritePos = size;
    }
    else
    {
        if (m_nWritePos + size <= m_nBuffSIze)
        {
            memcpy(m_pBuff + m_nWritePos, data, size);
            m_nWritePos += size;
        }
        else
        {
            uint32_t nDataSize = GetDataSize();
            if (nDataSize + size <= m_nBuffSIze)
            {
                memmove(m_pBuff, m_pBuff + m_nReadPos, nDataSize);
                memcpy(m_pBuff + nDataSize, data, size);
                m_nReadPos = 0;
                m_nWritePos = nDataSize + size;
            }
            else
            {
                uint32_t mallocsize = (m_nBuffSIze << 1) > (nDataSize + size) ? m_nBuffSIze << 1 : nDataSize + size;
                uint8_t* pNewBuff = (uint8_t*)malloc(mallocsize);
                if (pNewBuff == nullptr)
                {
                    return -2;
                }

                m_nBuffSIze = mallocsize;
                memcpy(pNewBuff, m_pBuff + m_nReadPos, nDataSize);
                memcpy(pNewBuff + nDataSize, data, size);
                free(m_pBuff);
                m_pBuff = pNewBuff;
                m_nReadPos = 0;
                m_nWritePos = nDataSize + size;
            }
        }
    }

    return 0;
}

int32_t ExBuff::Read(uint8_t*& data, uint32_t size)
{
    uint32_t nDataSize = GetDataSize();
    if (nDataSize == 0)
    {
        data = nullptr;
        return 0;
    }

    uint32_t nRetSize = size <= nDataSize ? size : nDataSize;
    data = m_pBuff + m_nReadPos;
    m_nReadPos += nRetSize;

    return nRetSize;
}

int32_t ExBuff::GetRawData(uint8_t*& data, uint32_t& size)
{
    data = m_pBuff + m_nReadPos;
    size = GetDataSize();

    return 0;
}

int32_t ExBuff::ClearBuff(uint32_t max)
{
    if (max > m_nBuffSIze)
    {
        return ReleaseAll();
    }

    m_nReadPos = 0;
    m_nWritePos = 0;

    return 0;
}
