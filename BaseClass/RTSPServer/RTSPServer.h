#pragma once
#include <cstdint>
#include <set>
#include <mutex>
#include <thread>
#include "RTSPServerSession.h"

class RTSPServer
{
public:
    RTSPServer();
    ~RTSPServer();

    int32_t OpenServer(uint16_t port);
    int32_t CloseServer();
    int32_t EnableOSD(bool enable);
    int32_t SetAttitude(float pitch, float roll, float yaw);
    int32_t SetGPS(int32_t lat, int32_t lon, int32_t alt, uint8_t satellites, uint16_t vel);
    int32_t SetSysStatus(uint16_t voltage, int16_t current, int8_t batteryRemaining);

private:
    int32_t ReleaseAll();
    void ServerThread();
    void RemoveFinishedSession();

private:
    int32_t m_nServerSocketfd;
    bool m_bCloseServer;
    std::thread* m_pServerThread;
    bool m_bEnableOSD;

    std::mutex m_RTSPServerSessionSetLock;
    std::set<RTSPServerSession*> m_RTSPServerSessionSet;
};