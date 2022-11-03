#pragma once
#include <chrono>

class TimeCounter
{
public:
    TimeCounter();
    ~TimeCounter();

    void MakeTimePoint();
    double GetDuration();

private:
    std::chrono::steady_clock::time_point m_LastTimePoint;
};