#include <thread>
#include "XiheClient.h"
#include "Log/Log.h"

int count = 0;
void OnVideo(std::shared_ptr<VideoFrame>& video)
{
    //Debug("On Video Packet time:%llu", video->m_lPTS);
    if (count % 20 == 0)
    {
        //std::string path = "/usr/Video1/" + std::to_string(count) + ".yuv";
        //FILE* fp = fopen(path.c_str(), "wb+");
        //fwrite(video->m_pData, 1, video->m_nLength, fp);
        //fclose(fp);
        Trace("Rend video:%d", count);
    }
    count++;
}

int main(void)
{
    InitLog("/usr/XiheClient.txt");
    SetLogLevel(DEBUG);
    //XIheClient* pXIheClient = new XIheClient("192.168.12.1", 7777);
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