#pragma once
#ifdef _WIN32
#error "FxSound Linux compatibility header must not be used on Windows"
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <wchar.h>
#include <wctype.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

typedef int BOOL;
typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef int32_t LONG;
typedef uint32_t UINT;
typedef unsigned long ULONG;
typedef uintptr_t ULONG_PTR;
typedef void* HANDLE;
typedef void* HWND;
typedef void* HLOCAL;
typedef void* HINSTANCE;
typedef void* HMODULE;
typedef void* HKEY;
typedef void* LPVOID;
typedef const void* LPCVOID;
typedef char* LPSTR;
typedef const char* LPCSTR;
typedef wchar_t* LPWSTR;
typedef const wchar_t* LPCWSTR;
typedef char* HPSTR;
typedef unsigned char* LPBYTE;
typedef DWORD* LPDWORD;
typedef LONG* LPLONG;
typedef int errno_t;
typedef wchar_t WCHAR;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef const wchar_t* LPCTSTR;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef NULL
#define NULL 0
#endif

#define WINAPI
#define CALLBACK
#define APIENTRY
#define MAX_PATH 4096
#define MB_OK 0x00000000U
#define MB_ICONERROR 0x00000010U
#define MB_ICONWARNING 0x00000030U
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define CP_UTF8 65001
#define _MAX_PATH MAX_PATH

#ifndef __declspec
#define __declspec(x)
#endif
#ifndef __int64
#define __int64 long long
#endif

typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME;

typedef struct _SECURITY_ATTRIBUTES {
    DWORD nLength;
    LPVOID lpSecurityDescriptor;
    BOOL bInheritHandle;
} SECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;

static inline int MessageBoxA(HWND, const char* text, const char* title, UINT) {
    fprintf(stderr, "FxSound: %s%s%s\n", title ? title : "", title ? ": " : "", text ? text : "");
    return 0;
}
static inline int MessageBoxW(HWND, const wchar_t* text, const wchar_t* title, UINT) {
    fwprintf(stderr, L"FxSound: %ls%ls%ls\n", title ? title : L"", title ? L": " : L"", text ? text : L"");
    return 0;
}
#ifdef __cplusplus
static inline int MessageBox(HWND h, const wchar_t* text, const wchar_t* title, UINT t) { return MessageBoxW(h, text, title, t); }
static inline int MessageBox(HWND h, const char* text, const char* title, UINT t) { return MessageBoxA(h, text, title, t); }
#endif

static inline void Sleep(DWORD ms) { usleep((useconds_t)ms * 1000U); }
static inline HLOCAL LocalFree(HLOCAL p) { free(p); return NULL; }
static inline BOOL IsDebuggerPresent(void) { return FALSE; }
static inline void DebugBreak(void) { __builtin_trap(); }
static inline DWORD GetTickCount(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (DWORD)((uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL);
}

#ifndef _stricmp
#define _stricmp strcasecmp
#endif
#ifndef _wcsicmp
#define _wcsicmp wcscasecmp
#endif
#ifndef _wtoi
#define _wtoi(s) ((int)wcstol((s), NULL, 10))
#endif
#ifndef _wtol
#define _wtol(s) (wcstol((s), NULL, 10))
#endif
#ifndef _wtof
#define _wtof(s) (wcstod((s), NULL))
#endif

static inline int fopen_s(FILE** f, const char* name, const char* mode) {
    if (!f) return EINVAL;
    *f = fopen(name, mode);
    return *f ? 0 : errno;
}
static inline int _wfopen_s(FILE** f, const wchar_t* name, const wchar_t* mode) {
    if (!f) return EINVAL;
    char n[MAX_PATH];
    char m[32];
    wcstombs(n, name, sizeof(n) - 1); n[sizeof(n) - 1] = 0;
    wcstombs(m, mode, sizeof(m) - 1); m[sizeof(m) - 1] = 0;
    *f = fopen(n, m);
    return *f ? 0 : errno;
}
static inline FILE* _wfopen(const wchar_t* name, const wchar_t* mode) {
    FILE* f = NULL;
    _wfopen_s(&f, name, mode);
    return f;
}

static inline int strcpy_s(char* dst, size_t n, const char* src) {
    if (!dst || !src || n == 0) return EINVAL;
    snprintf(dst, n, "%s", src);
    return 0;
}
static inline int wcscpy_s(wchar_t* dst, size_t n, const wchar_t* src) {
    if (!dst || !src || n == 0) return EINVAL;
    wcsncpy(dst, src, n - 1);
    dst[n - 1] = 0;
    return 0;
}
static inline int strcat_s(char* dst, size_t n, const char* src) {
    if (!dst || !src || n == 0) return EINVAL;
    size_t used = strlen(dst);
    if (used >= n) return ERANGE;
    snprintf(dst + used, n - used, "%s", src);
    return 0;
}
static inline int wcscat_s(wchar_t* dst, size_t n, const wchar_t* src) {
    if (!dst || !src || n == 0) return EINVAL;
    size_t used = wcslen(dst);
    if (used >= n) return ERANGE;
    wcsncat(dst, src, n - used - 1);
    return 0;
}

#ifdef __cplusplus
#include <cstdarg>
#include <string>

static inline std::wstring fxsound_translate_wprintf_format(const wchar_t* format) {
    if (!format) return {};
    std::wstring translated(format);
    size_t pos = 0;
    while ((pos = translated.find(L"%s", pos)) != std::wstring::npos) {
        translated.replace(pos, 2, L"%ls");
        pos += 3;
    }
    return translated;
}

static inline int fxsound_vswprintf(wchar_t* dst, size_t cap,
                                    const wchar_t* format, va_list args) {
    if (!dst || !format || cap == 0) return -1;
    const std::wstring translated = fxsound_translate_wprintf_format(format);
    const int rc = ::vswprintf(dst, cap, translated.c_str(), args);
    if (rc < 0) dst[cap - 1] = 0;
    return rc;
}

template <size_t N>
static inline int fxsound_swprintf(wchar_t (&dst)[N], const wchar_t* format, ...) {
    va_list args;
    va_start(args, format);
    const int rc = fxsound_vswprintf(dst, N, format, args);
    va_end(args);
    return rc;
}

static inline int fxsound_swprintf(wchar_t* dst, size_t cap,
                                   const wchar_t* format, ...) {
    va_list args;
    va_start(args, format);
    const int rc = fxsound_vswprintf(dst, cap, format, args);
    va_end(args);
    return rc;
}

#define swprintf fxsound_swprintf
#endif

#ifdef __cplusplus
extern "C" {
#endif
#include <dlfcn.h>
static inline HMODULE LoadLibraryW(const wchar_t*) { return NULL; }
static inline HMODULE LoadLibraryA(const char* path) { return path ? dlopen(path, RTLD_NOW) : NULL; }
static inline BOOL FreeLibrary(HMODULE h) { if (h) dlclose(h); return TRUE; }
static inline void* GetProcAddress(HMODULE h, const char* name) { return (h && name) ? dlsym(h, name) : NULL; }
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
wchar_t* fxsound_fgetws(wchar_t* dst, int count, FILE* stream);
#define fgetws fxsound_fgetws
#endif
