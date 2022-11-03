#include <stdio.h>
#include <cstdarg>
#include <sys/time.h>
#include "Log.h"

void Error(const char* format, ...)
{
    va_list list;
    va_start(list, 1);
    struct timeval time;
    gettimeofday(&time, nullptr);
    printf("[%ld.%ld]Error:", time.tv_sec, time.tv_usec);
    vprintf(format, list);
    printf("\n");
    va_end(list);
}

void Warn(const char* format, ...)
{
    va_list list;
    va_start(list, 1);
    struct timeval time;
    gettimeofday(&time, nullptr);
    printf("[%ld.%ld]Warn:", time.tv_sec, time.tv_usec);
    vprintf(format, list);
    printf("\n");
    va_end(list);
}

void Trace(const char* format, ...)
{
    va_list list;
    va_start(list, 1);
    struct timeval time;
    gettimeofday(&time, nullptr);
    printf("[%ld.%ld]Tarce:", time.tv_sec, time.tv_usec);
    vprintf(format, list);
    printf("\n");
    va_end(list);
}

void Debug(const char* format, ...)
{
    va_list list;
    va_start(list, 1);
    struct timeval time;
    gettimeofday(&time, nullptr);
    printf("[%ld.%ld]Debug:", time.tv_sec, time.tv_usec);
    vprintf(format, list);
    printf("\n");
    va_end(list);
}

void Panic(const char* format, ...)
{
    va_list list;
    va_start(list, 1);
    struct timeval time;
    gettimeofday(&time, nullptr);
    printf("[%ld.%ld]Panic:", time.tv_sec, time.tv_usec);
    vprintf(format, list);
    printf("\n");
    va_end(list);
}