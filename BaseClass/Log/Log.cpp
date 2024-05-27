#include <stdio.h>
#include <cstdarg>
#include <ctime>
#include <chrono>
#include <ratio>
#include <iostream>
#include <mutex>
#include "Log.h"

FILE* g_pLogFile = nullptr;
LogLevel g_eLogLevel = DEBUG;
std::mutex g_cLogLock;

int32_t InitLog(std::string path)
{
    std::lock_guard<std::mutex> lock(g_cLogLock);
    if (g_pLogFile != nullptr)
    {
        return -1;
    }

    g_pLogFile = fopen(path.c_str(), "ab+");
    if (g_pLogFile == nullptr)
    {
        return -2;
    }

    return 0;
}

int32_t UnInitLog()
{
    std::lock_guard<std::mutex> lock(g_cLogLock);
    if (g_pLogFile == nullptr)
    {
        return 0;
    }

    freopen("/dev/tty", "w", stdout);
    fclose(g_pLogFile);
    g_pLogFile = nullptr;

    return 0;
}

int32_t GetTimeStr(char* buff, size_t size)
{
    auto now = std::chrono::system_clock::now();
    uint64_t disMillseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()
        - std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count() * 1000;
    time_t tt = std::chrono::system_clock::to_time_t(now);
    auto time_tm = localtime(&tt);

    snprintf(buff, size, "%d-%02d-%02d %02d:%02d:%02d.%03d", time_tm->tm_year + 1900,
        time_tm->tm_mon + 1, time_tm->tm_mday, time_tm->tm_hour,
        time_tm->tm_min, time_tm->tm_sec, (int)disMillseconds);

    return 0;
}

int32_t SetLogLevel(LogLevel level)
{
    g_eLogLevel = level;
    return 0;
}

void Error(const char* format, ...)
{
    if (g_eLogLevel > ERROR)
    {
        return;
    }

    {
        char buff[64];
        GetTimeStr(buff, 64);

        std::lock_guard<std::mutex> lock(g_cLogLock);
        g_pLogFile == nullptr ? printf("%s%s", buff, " [Error] ") : fprintf(g_pLogFile, "%s%s", buff, " [Error] ");

        va_list args;
        va_start(args, format);
        g_pLogFile == nullptr ? vprintf(format, args) : vfprintf(g_pLogFile, format, args);
        va_end(args);

        g_pLogFile == nullptr ? printf("\r\n") : fprintf(g_pLogFile, "\r\n");
    }
}

void Warn(const char* format, ...)
{
    if (g_eLogLevel > WARN)
    {
        return;
    }

    {
        char buff[64];
        GetTimeStr(buff, 64);

        std::lock_guard<std::mutex> lock(g_cLogLock);
        g_pLogFile == nullptr ? printf("%s%s", buff, " [Warn] ") : fprintf(g_pLogFile, "%s%s", buff, " [Warn] ");

        va_list args;
        va_start(args, format);
        g_pLogFile == nullptr ? vprintf(format, args) : vfprintf(g_pLogFile, format, args);
        va_end(args);

        g_pLogFile == nullptr ? printf("\r\n") : fprintf(g_pLogFile, "\r\n");
    }
}

void Trace(const char* format, ...)
{
    if (g_eLogLevel > TRACE)
    {
        return;
    }

    {
        char buff[64];
        GetTimeStr(buff, 64);

        std::lock_guard<std::mutex> lock(g_cLogLock);
        g_pLogFile == nullptr ? printf("%s%s", buff, " [Trace] ") : fprintf(g_pLogFile, "%s%s", buff, " [Trace] ");

        va_list args;
        va_start(args, format);
        g_pLogFile == nullptr ? vprintf(format, args) : vfprintf(g_pLogFile, format, args);
        va_end(args);

        g_pLogFile == nullptr ? printf("\r\n") : fprintf(g_pLogFile, "\r\n");
    }
}

void Debug(const char* format, ...)
{
    if (g_eLogLevel > DEBUG)
    {
        return;
    }

    {
        char buff[64];
        GetTimeStr(buff, 64);

        std::lock_guard<std::mutex> lock(g_cLogLock);
        g_pLogFile == nullptr ? printf("%s%s", buff, " [Debug] ") : fprintf(g_pLogFile, "%s%s", buff, " [Debug] ");

        va_list args;
        va_start(args, format);
        g_pLogFile == nullptr ? vprintf(format, args) : vfprintf(g_pLogFile, format, args);
        va_end(args);

        g_pLogFile == nullptr ? printf("\r\n") : fprintf(g_pLogFile, "\r\n");
    }
}

void Panic(const char* format, ...)
{
    if (g_eLogLevel > PANIC)
    {
        return;
    }

    {
        char buff[64];
        GetTimeStr(buff, 64);

        std::lock_guard<std::mutex> lock(g_cLogLock);
        g_pLogFile == nullptr ? printf("%s%s", buff, " [Panic] ") : fprintf(g_pLogFile, "%s%s", buff, " [Panic] ");

        va_list args;
        va_start(args, format);
        g_pLogFile == nullptr ? vprintf(format, args) : vfprintf(g_pLogFile, format, args);
        va_end(args);

        g_pLogFile == nullptr ? printf("\r\n") : fprintf(g_pLogFile, "\r\n");
    }
}