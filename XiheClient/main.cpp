#include <thread>
#include "XiheClient.h"
#include "Log/Log.h"

int count = 0;
void OnVideo(std::shared_ptr<VideoFrame>& video)
{
    count++;
    if (count % 25 == 0)
    {
        Trace("Rend video:%d", count);
    }
}

int main(void)
{
    InitLog("/usr/XiheClient.txt");
    SetLogLevel(TRACE);
    XIheClient* pXIheClient = new XIheClient("127.0.0.1", 7777);
    VideoDecoder::VideoFrameCallbaclk pVideoCallback = std::bind(&OnVideo, std::placeholders::_1);
    pXIheClient->SetVideoFrameCallback(pVideoCallback);
    pXIheClient->PlayDevice("video0");

    pXIheClient->OpenDigitalTransport();
    UDPDataChannel::UDPDataChannelInitParam param;
    param.ip = "127.0.0.1"; param.port = 8888; param.mode = UDPDataChannel::WorkMode::WORK_AS_CLIENT;
    pXIheClient->InitRemoteTransport("udp", &param);
    pXIheClient->StartTransport();

    while (true)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    return 0;
}