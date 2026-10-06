#include "codedefs.h"
#include "reg.h"
#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_map>

namespace {
std::mutex g_mutex;
std::unordered_map<std::wstring, std::wstring> g_strings;
std::unordered_map<std::wstring, unsigned long> g_dwords;

std::wstring key(int root, const wchar_t* path, const wchar_t* name = L"") {
    return std::to_wstring(root) + L":" + (path ? path : L"") + L":" + (name ? name : L"");
}

void copy_wide(wchar_t* out, unsigned long cap, const std::wstring& value) {
    if (!out || cap == 0) return;
    const size_t n = std::min<size_t>(cap - 1, value.size());
    std::wmemcpy(out, value.data(), n);
    out[n] = 0;
}
}

int regReadTopDir_Wide(wchar_t* out, int length, int, int, CSlout*) {
    if (!out || length <= 0) return NOT_OKAY;
    copy_wide(out, (unsigned long)length, L".");
    return OKAY;
}
int regReadRegisteredOwner(char* out, int length) {
    if (!out || length <= 0) return NOT_OKAY;
    snprintf(out, (size_t)length, "%s", "FxSound Linux");
    return OKAY;
}
int regReadRegisteredOwner_Wide(wchar_t* out, int length) {
    if (!out || length <= 0) return NOT_OKAY;
    copy_wide(out, (unsigned long)length, L"FxSound Linux");
    return OKAY;
}
int regRemoveKey(int root, char* path) {
    if (!path) return NOT_OKAY;
    std::wstring w;
    while (*path) w.push_back((unsigned char)*path++);
    return regRemoveKey_Wide(root, w.data());
}
int regRemoveKey_Wide(int root, wchar_t* path) {
    if (!path) return NOT_OKAY;
    std::lock_guard<std::mutex> lock(g_mutex);
    const std::wstring prefix = std::to_wstring(root) + L":" + path;
    for (auto it = g_strings.begin(); it != g_strings.end(); )
        it = it->first.rfind(prefix, 0) == 0 ? g_strings.erase(it) : std::next(it);
    for (auto it = g_dwords.begin(); it != g_dwords.end(); )
        it = it->first.rfind(prefix, 0) == 0 ? g_dwords.erase(it) : std::next(it);
    return OKAY;
}
int regCreateKey(int root, char* path, char* data) {
    if (!path || !data) return NOT_OKAY;
    std::wstring wp, wd;
    while (*path) wp.push_back((unsigned char)*path++);
    while (*data) wd.push_back((unsigned char)*data++);
    return regCreateKey_Wide(root, wp.data(), wd.data());
}
int regCreateKey_Wide(int root, wchar_t* path, wchar_t* data) {
    if (!path || !data) return NOT_OKAY;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_strings[key(root, path)] = data;
    return OKAY;
}
int regReadKey(int root, char* path, int* exists, char* data, unsigned long cap) {
    if (!path || !exists || !data || cap == 0) return NOT_OKAY;
    std::wstring wp;
    while (*path) wp.push_back((unsigned char)*path++);
    wchar_t temp[2048] = {};
    int rc = regReadKey_Wide(root, wp.data(), exists, temp, 2048);
    if (rc != OKAY || !*exists) return rc;
    wcstombs(data, temp, cap - 1);
    data[cap - 1] = 0;
    return OKAY;
}
int regReadKey_Wide(int root, wchar_t* path, int* exists, wchar_t* data, unsigned long cap) {
    if (!path || !exists || !data || cap == 0) return NOT_OKAY;
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_strings.find(key(root, path));
    if (it == g_strings.end()) {
        *exists = IS_FALSE;
        data[0] = 0;
        return OKAY;
    }
    *exists = IS_TRUE;
    copy_wide(data, cap, it->second);
    return OKAY;
}
int regCreateKeyTest_Wide(int root, wchar_t* path, wchar_t* data, int* success) {
    if (!success) return NOT_OKAY;
    const int rc = regCreateKey_Wide(root, path, data);
    *success = rc == OKAY ? IS_TRUE : IS_FALSE;
    return OKAY;
}
int regCreateKeyWithKeyname_Dword_Wide(int root, wchar_t* path, wchar_t* name, unsigned long value) {
    if (!path || !name) return NOT_OKAY;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_dwords[key(root, path, name)] = value;
    return OKAY;
}
int regCreateKeyWithKeyname_String_Wide(int root, wchar_t* path, wchar_t* name, wchar_t* data) {
    if (!path || !name || !data) return NOT_OKAY;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_strings[key(root, path, name)] = data;
    return OKAY;
}
int regReadKeyWithKeyname_String_Wide(int root, wchar_t* path, wchar_t* name, int* exists,
                                      wchar_t* data, unsigned long cap) {
    if (!path || !name || !exists || !data || cap == 0) return NOT_OKAY;
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_strings.find(key(root, path, name));
    if (it == g_strings.end()) {
        *exists = IS_FALSE;
        data[0] = 0;
        return OKAY;
    }
    *exists = IS_TRUE;
    copy_wide(data, cap, it->second);
    return OKAY;
}
int regReadKeyWithKeyname_Dword_Wide(int root, wchar_t* path, wchar_t* name, int* exists,
                                     unsigned long* value) {
    if (!path || !name || !exists || !value) return NOT_OKAY;
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_dwords.find(key(root, path, name));
    if (it == g_dwords.end()) {
        *exists = IS_FALSE;
        *value = 0;
        return OKAY;
    }
    *exists = IS_TRUE;
    *value = it->second;
    return OKAY;
}
int regRecursiveDeleteFolder_Wide(int root, wchar_t* path) {
    return regRemoveKey_Wide(root, path);
}
