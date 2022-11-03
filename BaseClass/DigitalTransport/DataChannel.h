#pragma once
#include <string>
#include <functional>
#include "Common.h"

class DataChannel
{
public:
    typedef std::function<void(std::shared_ptr<Packet>)> MsgCallback;

public:
    virtual ~DataChannel() {};
    virtual int32_t SendMsg(std::shared_ptr<Packet> packet) = 0;
    virtual int32_t SetMsgCallback(MsgCallback callback) = 0;
    virtual int32_t GetProtocol(std::string& protocol) = 0;
    virtual int32_t CloseChannel() = 0;
};