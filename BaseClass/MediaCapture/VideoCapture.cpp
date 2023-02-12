#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/videodev2.h>
#include <string.h>
#include <sys/mman.h>
#include "VideoCapture.h" 
#include "Log/Log.h"

#define VIDEO_CAPTURN_BUFF (4)
#define VIDEO_CLOCK_RATE (90000)

VideoCapture::VideoCapture()
{
    m_pVideoCaptureCapability = nullptr;
    m_bStopCaptureVideo = true;
    m_pVideoCaptureThread = nullptr;
    m_nCameraFd = -1;
    m_pCaptureVideoCallbaclk = nullptr;
    m_nVideoBuffNum = 0;
    m_nFrameTime = 0;
    m_nTimePerFrame = VIDEO_CLOCK_RATE / 25;
}

VideoCapture::~VideoCapture()
{
    StopCapture();

    if (m_pVideoCaptureCapability != nullptr)
    {
        delete m_pVideoCaptureCapability;
    }
    m_pCaptureVideoCallbaclk = nullptr;
}

void VideoCapture::ListDevices(std::list<std::string>& devicesList)
{
    char device[16];
    int fd = -1;

    for (int i = 0; i < 64; i++)
    {
        sprintf(device, "/dev/video%d", i);
        fd = open(device, O_RDONLY);
        if (fd != -1)
        {
            close(fd);
            devicesList.push_back(device);
        }
    }
}

std::string VideoCapture::GetDeviceName(std::string& device)
{
    std::string deviceName = "";

    int fd = open(device.c_str(), O_RDONLY);
    if (fd == -1)
    {
        Error("[VideoCapture::GetDeviceName] Can not open device");
        return deviceName;
    }

    struct v4l2_capability cap;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0)
    {
        close(fd);
        Error("[VideoCapture::GetDeviceName] Quey device error. device:%s return:%d", device.c_str(), errno);
        return deviceName;
    }

    close(fd);
    deviceName = (char*)cap.card;

    return deviceName;
}

std::list<VideoCapture::VideoCaptureCapability*>* VideoCapture::GetDeviceCapabilities(std::string& device)
{
    int fd = open(device.c_str(), O_RDONLY);
    if (fd == -1)
    {
        Error("[VideoCapture::GetDeviceCapabilities] Can not open device");
        return nullptr;
    }

    std::list<VideoCaptureCapability*>* result = new std::list<VideoCaptureCapability*>();
    struct v4l2_format video_fmt;
    memset(&video_fmt, 0, sizeof(struct v4l2_format));
    video_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    video_fmt.fmt.pix.sizeimage = 0;

    unsigned int videoFormats[] = { V4L2_PIX_FMT_MJPEG,V4L2_PIX_FMT_JPEG,V4L2_PIX_FMT_MPEG,V4L2_PIX_FMT_YUV420 };
    int totalFmts = sizeof(videoFormats) / sizeof(unsigned int);

    unsigned int size[][2] = { {1280, 720},{1920, 1080},{2560,1440},{4096,2160} };
    int sizes = sizeof(size) / sizeof(unsigned int) / 2;

    for (int fmts = 0; fmts < totalFmts; fmts++)
    {
        for (int i = 0; i < sizes; i++)
        {
            video_fmt.fmt.pix.pixelformat = videoFormats[fmts];
            video_fmt.fmt.pix.width = size[i][0];
            video_fmt.fmt.pix.height = size[i][1];

            if (ioctl(fd, VIDIOC_TRY_FMT, &video_fmt) >= 0)
            {
                if ((video_fmt.fmt.pix.width == size[i][0]) && (video_fmt.fmt.pix.height == size[i][1]))
                {
                    VideoCaptureCapability* cap = new VideoCaptureCapability();
                    cap->m_nWidth = video_fmt.fmt.pix.width;
                    cap->m_nHeight = video_fmt.fmt.pix.height;
                    cap->m_nVideoType = videoFormats[fmts];

                    cap->m_nFPS = 25;
                    result->push_back(cap);
                }
            }

        }
    }

    close(fd);

    return result;
}

void VideoCapture::SetCaptureVideoCallbaclk(CaptureVideoCallbaclk callbsck)
{
    m_pCaptureVideoCallbaclk = callbsck;
}

int32_t VideoCapture::StartCapture(std::string& device, VideoCaptureCapability capability)
{
    if (m_pVideoCaptureThread != nullptr)
    {
        StopCapture();
        Warn("[%p][VideoCapture::StartCapture] already capturing video", this);
    }

    int fd = open(device.c_str(), O_RDWR | O_NONBLOCK, 0);
    if (fd == -1)
    {
        Error("[%p][VideoCapture::StartCapture] Can not open device,errno:%d", this, errno);
        return -1;
    }

    struct v4l2_format video_fmt;
    memset(&video_fmt, 0, sizeof(struct v4l2_format));
    video_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    video_fmt.fmt.pix.sizeimage = 0;
    video_fmt.fmt.pix.pixelformat = capability.m_nVideoType;
    video_fmt.fmt.pix.width = capability.m_nWidth;
    video_fmt.fmt.pix.height = capability.m_nHeight;

    if (ioctl(fd, VIDIOC_S_FMT, &video_fmt) < 0)
    {
        Error("[%p][VideoCapture::StartCapture] Set video format Error,return:%d ", this, errno);
        return -2;
    }

    struct v4l2_streamparm streamparms;
    memset(&streamparms, 0, sizeof(streamparms));
    streamparms.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_G_PARM, &streamparms) < 0)
    {
        Warn("[%p][VideoCapture::StartCapture] Error in VIDIOC_G_PARM errno = %d ", this, errno);
    }
    else
    {
        if (streamparms.parm.capture.capability & V4L2_CAP_TIMEPERFRAME)
        {
            memset(&streamparms, 0, sizeof(streamparms));
            streamparms.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            streamparms.parm.capture.timeperframe.numerator = 1;
            streamparms.parm.capture.timeperframe.denominator = capability.m_nFPS;
            if (ioctl(fd, VIDIOC_S_PARM, &streamparms) < 0)
            {
                Warn("[%p][VideoCapture::StartCapture] Failed to set the framerate. errno=%d", this, errno);
            }
            else
            {
                m_nTimePerFrame = VIDEO_CLOCK_RATE / capability.m_nFPS;
            }
        }
    }

    m_nCameraFd = fd;
    if (!AllocateVideoBuffers())
    {
        Error("[%p][VideoCapture::StartCapture] Failed to allocate video capture buffers", this);
        return -3;
    }

    if (m_pVideoCaptureCapability != nullptr)
    {
        delete m_pVideoCaptureCapability;
    }
    m_pVideoCaptureCapability = new VideoCaptureCapability(&capability);

    m_bStopCaptureVideo = false;
    m_pVideoCaptureThread = new std::thread(&VideoCapture::VideoCaptureThread, this);

    return 0;
}

int32_t VideoCapture::StopCapture()
{
    m_bStopCaptureVideo = true;
    if (m_pVideoCaptureThread != nullptr)
    {
        if (m_pVideoCaptureThread->joinable())
        {
            m_pVideoCaptureThread->join();
        }
        delete m_pVideoCaptureThread;
        m_pVideoCaptureThread = nullptr;
    }

    if (m_pVideoCaptureCapability != nullptr)
    {
        delete m_pVideoCaptureCapability;
        m_pVideoCaptureCapability = nullptr;
    }

    if (m_nCameraFd != -1)
    {
        close(m_nCameraFd);
        m_nCameraFd = -1;
    }

    DeAllocateVideoBuffers();

    return 0;
}

void VideoCapture::VideoCaptureThread()
{
    Trace("[%p][VideoCapture::VideoCaptureThread] start VideoCaptureThread", this);

    if (m_pCaptureVideoCallbaclk == nullptr)
    {
        Error("[%p][VideoCapture::VideoCaptureProc] vioed callback is null,can not output frame", this);
        close(m_nCameraFd);
        m_nCameraFd = -1;
        return;
    }

    Trace("[%p][VideoCapture::VideoCaptureThread] start ioctl VIDIOC_STREAMON VIDEO_CAPTURE", this);
    enum v4l2_buf_type type;
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(m_nCameraFd, VIDIOC_STREAMON, &type) == -1)
    {
        Error("[%p][VideoCapture::VideoCaptureProc] Failed to turn on stream  error:%s", this, strerror(errno));
        return;
    }
    Trace("[%p][VideoCapture::VideoCaptureThread] ioctl VIDIOC_STREAMON VIDEO_CAPTURE finish", this);

    struct v4l2_buffer buf;
    while (!m_bStopCaptureVideo)
    {
        memset(&buf, 0, sizeof(struct v4l2_buffer));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        while (ioctl(m_nCameraFd, VIDIOC_DQBUF, &buf) < 0)
        {
            if (errno == EAGAIN)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            else if (errno != EINTR)
            {
                Warn("[%p][VideoCapture::VideoCaptureProc] Could not sync on a buffer on device error:%s ", this, strerror(errno));
                continue;
            }
        }

        std::shared_ptr<VideoFrame> frame = std::make_shared<VideoFrame>();
        frame->m_nWidth = m_pVideoCaptureCapability->m_nWidth;
        frame->m_nHeight = m_pVideoCaptureCapability->m_nHeight;
        frame->m_nFrameType = m_pVideoCaptureCapability->m_nVideoType;
        frame->m_pData = (unsigned char*)malloc(buf.bytesused);
        if (frame->m_pData != nullptr)
        {
            frame->m_nLength = buf.bytesused;
            memcpy(frame->m_pData, m_pBuffers[buf.index].start, buf.bytesused);
        }
        frame->m_lPTS = m_nFrameTime;
        m_nFrameTime += m_nTimePerFrame;

        m_pCaptureVideoCallbaclk(frame);
        frame = nullptr;

        if (ioctl(m_nCameraFd, VIDIOC_QBUF, &buf) == -1)
        {
            Warn("[%p][VideoCapture::VideoCaptureProc] Failed to enqueue capture buffer", this);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    close(m_nCameraFd);
    m_nCameraFd = -1;

    Trace("[%p][VideoCapture::VideoCaptureThread] exit VideoCaptureThread", this);

    return;
}

bool VideoCapture::AllocateVideoBuffers()
{
    struct v4l2_requestbuffers rbuffer;
    memset(&rbuffer, 0, sizeof(v4l2_requestbuffers));

    rbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    rbuffer.memory = V4L2_MEMORY_MMAP;
    rbuffer.count = VIDEO_CAPTURN_BUFF;

    if (ioctl(m_nCameraFd, VIDIOC_REQBUFS, &rbuffer) < 0)
    {
        Error("[%p][VideoCapture::AllocateVideoBuffers] Could not get buffers from device. errno:%d", this, errno);
        return false;
    }

    if (rbuffer.count > VIDEO_CAPTURN_BUFF)
    {
        rbuffer.count = VIDEO_CAPTURN_BUFF;
    }
    m_nVideoBuffNum = rbuffer.count;

    m_pBuffers = new Buffer[rbuffer.count];
    for (unsigned int i = 0; i < rbuffer.count; i++)
    {
        struct v4l2_buffer buffer;
        memset(&buffer, 0, sizeof(v4l2_buffer));
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = i;

        if (ioctl(m_nCameraFd, VIDIOC_QUERYBUF, &buffer) < 0)
        {
            Error("[%p][VideoCapture::AllocateVideoBuffers] VIDIOC_QUERYBUF fail. errno:%d", this, errno);
            return false;
        }

        m_pBuffers[i].start = mmap(NULL, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, m_nCameraFd, buffer.m.offset);

        if (MAP_FAILED == m_pBuffers[i].start)
        {
            for (unsigned int j = 0; j < i; j++)
            {
                munmap(m_pBuffers[j].start, m_pBuffers[j].length);
            }
            Error("[%p][VideoCapture::AllocateVideoBuffers] mmap fail", this);
            return false;
        }

        m_pBuffers[i].length = buffer.length;

        if (ioctl(m_nCameraFd, VIDIOC_QBUF, &buffer) < 0)
        {
            return false;
        }
    }

    return true;
}

bool VideoCapture::DeAllocateVideoBuffers()
{
    if (m_pBuffers == nullptr)
    {
        return true;
    }

    for (uint32_t i = 0; i < m_nVideoBuffNum; i++)
    {
        munmap(m_pBuffers[i].start, m_pBuffers[i].length);
    }

    delete[] m_pBuffers;
    m_pBuffers = nullptr;

    enum v4l2_buf_type type;
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(m_nCameraFd, VIDIOC_STREAMOFF, &type) < 0)
    {
        Error("[%p][VideoCapture::DeAllocateVideoBuffers] VIDIOC_STREAMOFF error. errno:%d ", this, errno);
    }

    return true;
}