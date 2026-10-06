#pragma once
#include "windows.h"
#define CSIDL_COMMON_APPDATA 0
#define CSIDL_APPDATA 1
static inline int SHGetSpecialFolderPathW(HWND, wchar_t* path, int, BOOL) { if (path) *path=0; return 0; }
