#include "TimeCounter.h"

TimeCounter::TimeCounter()
{
    m_LastTimePoint = std::chrono::steady_clock::now();
}

TimeCounter::~TimeCounter()
{
}

void TimeCounter::MakeTimePoint()
{
    m_LastTimePoint = std::chrono::steady_clock::now();
}

double TimeCounter::GetDuration()
{
    std::chrono::steady_clock::time_point nowTime = std::chrono::steady_clock::now();
    std::chrono::duration<double> timeInterval = std::chrono::duration_cast<std::chrono::duration<double>>(nowTime - m_LastTimePoint);
    return timeInterval.count() * 1000;
}