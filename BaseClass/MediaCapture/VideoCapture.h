#pragma once
#include <list>
#include <functional>
#include <memory>
#include <thread>
#include "Common.h"

class VideoCapture
{
public:
    VideoCapture();
    ~VideoCapture();

public:
    typedef std::function<void(std::shared_ptr<VideoFrame>&)> CaptureVideoCallbaclk;
    typedef struct VideoCaptureCapability
    {
        uint32_t m_nWidth;
        uint32_t m_nHeight;
        uint32_t m_nFPS;
        bool m_bInterlaced;
        uint32_t m_nVideoType;

        VideoCaptureCapability()
        {
            m_nWidth = 0;
            m_nHeight = 0;
            m_nFPS = 0;
            m_bInterlaced = false;
            m_nVideoType = 0;
        }

        VideoCaptureCapability(const VideoCaptureCapability* capability)
        {
            if (capability != nullptr)
            {
                this->m_nWidth = capability->m_nWidth;
                this->m_nHeight = capability->m_nHeight;
                this->m_nFPS = capability->m_nFPS;
                this->m_bInterlaced = capability->m_bInterlaced;
                this->m_nVideoType = capability->m_nVideoType;
            }
        }
    }VideoCaptureCapability;

    static void ListDevices(std::list<std::string>& devicesList);
    static std::string GetDeviceName(std::string& device);
    static std::list<VideoCaptureCapability*>* GetDeviceCapabilities(std::string& device);

    void SetCaptureVideoCallbaclk(CaptureVideoCallbaclk callbsck);
    int32_t StartCapture(std::string& device, VideoCaptureCapability capability);
    int32_t StopCapture();

private:
    void VideoCaptureThread();
    bool AllocateVideoBuffers();
    bool DeAllocateVideoBuffers();

private:
    VideoCaptureCapability* m_pVideoCaptureCapability;
    bool m_bStopCaptureVideo;
    std::thread* m_pVideoCaptureThread;
    int m_nCameraFd;
    CaptureVideoCallbaclk m_pCaptureVideoCallbaclk;
    uint32_t m_nVideoBuffNum;
    uint64_t m_nFrameTime;
    uint64_t m_nTimePerFrame;

    typedef struct Buffer
    {
        void* start;
        size_t length;
    }Buffer;
    Buffer* m_pBuffers;
};