#include "SignalObject.h"

SignalObject::SignalObject()
{
}

SignalObject::~SignalObject()
{
}

void SignalObject::Signal()
{
    m_ConditionVariable.notify_one();
}

bool SignalObject::Wait(int64_t milliseconds)
{
    std::unique_lock<std::mutex> lock(m_Lock);
    if (m_ConditionVariable.wait_for(lock, std::chrono::milliseconds(milliseconds)) == std::cv_status::timeout)
    {
        return false;
    }

    return true;
}