// Force-included into every core C file (/FI): bits the NDK provided that MSVC lacks.
#ifndef WM_COMPAT_H
#define WM_COMPAT_H

#define _USE_MATH_DEFINES
#include <stdint.h>
#include <string.h>

typedef int64_t __int64_t;

#ifndef PATH_MAX
#define PATH_MAX 260
#endif

#define strcasecmp _stricmp
#define strncasecmp _strnicmp

#ifndef __cplusplus
#define inline __inline
#endif

void usleep(unsigned int usec);
char *strcasestr(const char *haystack, const char *needle);

#endif
