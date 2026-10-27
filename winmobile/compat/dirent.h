// Minimal opendir/readdir on top of FindFirstFileEx, implemented in posix.c.
#ifndef WM_DIRENT_H
#define WM_DIRENT_H

#ifdef __cplusplus
extern "C" {
#endif

struct dirent {
	char d_name[260];
};

typedef struct DIR DIR;

DIR *opendir(const char *path);
struct dirent *readdir(DIR *dir);
int closedir(DIR *dir);

#ifdef __cplusplus
}
#endif

#endif
