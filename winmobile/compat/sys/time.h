// gettimeofday on top of QueryPerformanceCounter, implemented in posix.c.
#ifndef WM_SYS_TIME_H
#define WM_SYS_TIME_H

#include <winsock2.h>	// struct timeval

#ifdef __cplusplus
extern "C" {
#endif

int gettimeofday(struct timeval *tv, void *tz);

#ifdef __cplusplus
}
#endif

#endif
