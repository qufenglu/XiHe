#pragma once
#include <stdint.h>
#include <string>

enum LogLevel
{
    DEBUG = 0,
    TRACE,
    WARN,
    ERROR,
    PANIC
};

int32_t InitLog(std::string path);

int32_t UnInitLog();

int32_t SetLogLevel(LogLevel level);

void Error(const char* format, ...);

void Warn(const char* format, ...);

void Trace(const char* format, ...);

void Debug(const char* format, ...);

void Panic(const char* format, ...);
