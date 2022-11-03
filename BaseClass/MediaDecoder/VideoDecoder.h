#pragma once
extern "C" {
#include "libavutil/cpu.h"
#include "libavcodec/codec.h"
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
}

#include <functional>
#include <memory>
#include <mutex>
#include "Common.h"

class VideoDecoder
{
public:
    VideoDecoder();
    ~VideoDecoder();

    typedef std::function<void(std::shared_ptr<VideoFrame>& pVideo)> VideoFrameCallbaclk;

    int32_t AddVideoStream(const VideoInfo& info);
    int32_t SetVideoFrameCallBack(VideoFrameCallbaclk callback);
    int32_t RecvVideoPacket(std::shared_ptr<VideoPacket>& packet);
    int32_t FlushDecoder();

private:
    int32_t DestroyDecoder();
    int32_t DecodePacket(const AVPacket* pPacket);
    int32_t OutputVideoFrame(const AVFrame* pFrame);
    int32_t Resample(const AVFrame* pFrame);

private:
    VideoInfo* m_pVideoInfo;
    std::mutex m_DecoderLock;

    const AVCodec* m_pAVCodec;
    AVCodecContext* m_pAVContext;
    AVCodecParserContext* m_pAVParserContext;
    AVFrame* m_pFrame;
    AVPacket* m_pPacket;

    bool m_bEnableLowDlay;
    VideoFrameCallbaclk m_pVideoFrameCallbaclk;

    AVFrame* m_pResampleFrame;
    SwsContext*  m_pResampleContext;
    int m_nResampleWidth;
    int m_nResampleHight;
    AVPixelFormat m_eResampFormat;
};