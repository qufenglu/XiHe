#include <thread>
#include "XiheClient.h"

void OnVideo(std::shared_ptr<VideoFrame>& video)
{
    int i = 0;
}

int main(void)
{
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