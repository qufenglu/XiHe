#pragma once
#include <cstdint>
#include <stdlib.h>
#include <cstring>
#include <string>
#include <vector>

typedef struct VideoFrame
{
    uint32_t m_nWidth;
    uint32_t m_nHeight;
    uint8_t* m_pData;
    uint32_t m_nLength;
    uint32_t m_nFrameType;
    uint64_t m_lPTS;

    VideoFrame()
    {
        m_nWidth = 0;
        m_nHeight = 0;
        m_pData = nullptr;
        m_nLength = 0;
        m_nFrameType = 0;
        m_lPTS = 0;
    }

    ~VideoFrame()
    {
        free(m_pData);
    }
}VideoFrame;

typedef struct VideoInfo
{
    uint32_t m_nWidth;
    uint32_t m_nHight;
    int32_t m_nCodecID;
    uint8_t* m_pExtraData;
    uint32_t m_uiExtraDataLen;

    VideoInfo()
    {
        m_nWidth = 0;
        m_nHight = 0;
        m_nCodecID = 0;
        m_pExtraData = nullptr;
        m_uiExtraDataLen = 0;
    }

    VideoInfo(const VideoInfo& info)
    {
        this->m_nWidth = info.m_nWidth;
        this->m_nHight = info.m_nHight;
        this->m_nCodecID = info.m_nCodecID;
        if (info.m_uiExtraDataLen > 0)
        {
            this->m_pExtraData = (uint8_t*)malloc(info.m_uiExtraDataLen);
            if (this->m_pExtraData != nullptr)
            {
                memcpy(this->m_pExtraData, info.m_pExtraData, info.m_uiExtraDataLen);
                this->m_uiExtraDataLen = info.m_uiExtraDataLen;
            }
            else
            {
                this->m_pExtraData = nullptr;
                this->m_uiExtraDataLen = 0;
            }
        }
        else
        {
            this->m_uiExtraDataLen = 0;
            this->m_pExtraData = nullptr;
        }
    }

    ~VideoInfo()
    {
        free(m_pExtraData);
    }
}VideoInfo;

typedef struct VideoPacket
{
    uint8_t* m_pData;
    uint32_t m_nLength;
    uint32_t m_nFrameType;
    uint64_t m_lPTS;
    uint64_t m_lDTS;

    VideoPacket()
    {
        m_pData = nullptr;
        m_nLength = 0;
        m_nFrameType = 0;
        m_lPTS = 0;
        m_lDTS = 0;
    }

    ~VideoPacket()
    {
        free(m_pData);
    }
}VideoPacket, MediaPacket;

typedef struct Packet
{
    uint8_t* m_pData;
    uint32_t m_nLength;

    Packet()
    {
        m_pData = nullptr;
        m_nLength = 0;
    }

    ~Packet()
    {
        free(m_pData);
    }
}Packet;