#include <thread>
#include "XiheServer.h"
#include "Log/Log.h"

int main(void)
{
    InitLog("/usr/XiheServer.txt");
    SetLogLevel(TRACE);
    //此操作第一次耗时，因此提前
    setlocale(LC_CTYPE, "zh_CN.GB2312");
    mbstowcs(nullptr, "测试", 0);

    XiheServer* pXiheServer = new XiheServer();
    pXiheServer->OpenRTSPServer(7777);

    pXiheServer->OpenDigitalTransport();
    int baud = 57600;
    pXiheServer->InitControllerTransport("uart", &baud);
    UDPDataChannel::UDPDataChannelInitParam param;
    param.ip = "0.0.0.0"; param.port = 8888; param.mode = UDPDataChannel::WorkMode::WORK_AS_SERVER;
    pXiheServer->InitRemoteTransport("udp", &param);
    pXiheServer->StartTransport();

    while (true)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    return 0;
}