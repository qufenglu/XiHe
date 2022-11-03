#pragma once
#include <mutex>
#include <condition_variable>

class SignalObject
{
public:
    SignalObject();
    ~SignalObject();

    bool Wait(int64_t milliseconds);
    void Signal();

private:
    std::mutex m_Lock;
    std::condition_variable m_ConditionVariable;
};