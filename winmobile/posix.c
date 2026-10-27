// POSIX functions the core uses that the MSVC runtime lacks.

#include <dirent.h>
#include <sys/time.h>

#include <stdlib.h>
#include <string.h>
#include <windows.h>

int gettimeofday(struct timeval *tv, void *tz) {
	static LARGE_INTEGER frequency;
	LARGE_INTEGER now;

	if (!frequency.QuadPart) {
		QueryPerformanceFrequency(&frequency);
	}
	QueryPerformanceCounter(&now);
	tv->tv_sec = (long) (now.QuadPart / frequency.QuadPart);
	tv->tv_usec = (long) (now.QuadPart % frequency.QuadPart * 1000000 / frequency.QuadPart);
	return 0;
}

void usleep(unsigned int usec) {
	Sleep(usec / 1000);
}

unsigned int sleep(unsigned int seconds) {
	Sleep(seconds * 1000);
	return 0;
}

char *strcasestr(const char *haystack, const char *needle) {
	size_t n = strlen(needle);

	for (; *haystack; ++haystack) {
		if (_strnicmp(haystack, needle, n) == 0) {
			return (char *) haystack;
		}
	}
	return n ? NULL : (char *) haystack;
}

struct DIR {
	HANDLE find;
	WIN32_FIND_DATAW data;
	int have_entry;
	struct dirent entry;
};

DIR *opendir(const char *path) {
	wchar_t pattern[MAX_PATH];
	DIR *dir;

	if (MultiByteToWideChar(CP_UTF8, 0, path, -1, pattern, MAX_PATH - 2) == 0) {
		return NULL;
	}
	wcscat_s(pattern, MAX_PATH, L"\\*");
	dir = calloc(1, sizeof(DIR));
	dir->find = FindFirstFileExW(pattern, FindExInfoBasic, &dir->data, FindExSearchNameMatch, NULL,
								 0);
	if (dir->find == INVALID_HANDLE_VALUE) {
		free(dir);
		return NULL;
	}
	dir->have_entry = 1;
	return dir;
}

struct dirent *readdir(DIR *dir) {
	if (!dir->have_entry) {
		return NULL;
	}
	WideCharToMultiByte(CP_UTF8, 0, dir->data.cFileName, -1, dir->entry.d_name,
						sizeof(dir->entry.d_name), NULL, NULL);
	dir->have_entry = FindNextFileW(dir->find, &dir->data);
	return &dir->entry;
}

int closedir(DIR *dir) {
	FindClose(dir->find);
	free(dir);
	return 0;
}
