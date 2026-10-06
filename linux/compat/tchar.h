#pragma once
#include <wchar.h>
#ifdef UNICODE
#define TCHAR wchar_t
#else
#define TCHAR char
#endif
