#pragma once

extern "C" {
#include "libavcodec/codec.h"
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avio.h"
#include "libswscale/swscale.h"
}


#include <mutex>
#include <memory>
#include <functional>
#include "Common.h"

bool FindSPS(uint8_t* data, uint32_t size, uint8_t*& sps, uint32_t& spsSize);
bool FindPPS(uint8_t* data, uint32_t size, uint8_t*& pps, uint32_t& ppsSize);

class VideoEncoder
{
public:
    VideoEncoder();
    ~VideoEncoder();

    typedef struct EncodParam
    {
        uint32_t m_nWidth = 0;
        uint32_t m_nHeight = 0;
        uint32_t m_nBitRate = 0;
        uint32_t m_nCodecID = AV_CODEC_ID_H264;
    }EncodParam;
    typedef std::function<void(std::shared_ptr<VideoPacket>& pVideo)> VideoPacketCallbaclk;

    int32_t OpenEncoder(const EncodParam& param);
    int32_t CloseEncoder();
    inline const VideoInfo* GetVideoInfo() { return m_pVideoInfo; };
    int32_t EncodeFrame(std::shared_ptr<VideoFrame> pVideoPacket);
    int32_t EncodeFrame(const AVFrame* pFrame);
    int32_t SetVideoPacketCallback(VideoPacketCallbaclk pCallback);

    const uint8_t* GetSPS(uint32_t& len);
    const uint8_t* GetPPS(uint32_t& len);

private:
    int32_t ReleaseAll();
    int32_t OutputVideoPacket();
    int32_t ResampleIfNeed(std::shared_ptr<VideoFrame>& pVideoPacket);

private:
    std::mutex m_EncoderLock;
    VideoInfo* m_pVideoInfo;

    const AVCodec* m_pAVCodec;
    AVCodecContext* m_pAVContext;
    AVFrame* m_pFrame;
    AVPacket* m_pPacket;

    int m_nResampleSrcWidth;
    int m_nResampleSrcHight;
    SwsContext* m_pResampleContext;
    AVFrame* m_pResampleFrame;

    VideoPacketCallbaclk m_pVideoPacketCallback;
};