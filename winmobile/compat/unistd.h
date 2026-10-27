// POSIX stand-in for MSVC: the core includes unistd.h but uses next to nothing from it.
#ifndef WM_UNISTD_H
#define WM_UNISTD_H

#include <io.h>
#include <direct.h>
#include <process.h>

unsigned int sleep(unsigned int seconds);

#endif
