#include "codedefs.h"
#include "mth.h"
#include "slout.h"
#include "pstr.h"
#include "File.h"

#include <algorithm>
#include <cmath>
#include <codecvt>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <locale>
#include <string>
#include <vector>

void mthCalcQuantDelta(realtype r_range_min, realtype r_range_max, int i_number_of_levels,
                       realtype *rp_delta)
{
    realtype rough_delta = (r_range_max - r_range_min) / (realtype)(i_number_of_levels - 1);
    realtype log_rough_delta = (realtype)log10((double)rough_delta);
    int i_ten_power = (int)log_rough_delta;
    realtype remainder = log_rough_delta - i_ten_power;
    realtype delta_val;

    if (log_rough_delta < 0.0f) {
        i_ten_power = (int)log_rough_delta - 1;
        remainder += 1.0f;
    }

    if (remainder == 0.0f) {
        delta_val = rough_delta;
    } else {
        delta_val = 10.0f;
        if ((realtype)log10(5.0) > remainder) delta_val = 5.0f;
        if ((realtype)log10(2.5) > remainder) delta_val = 2.5f;
        if ((realtype)log10(2.0) > remainder) delta_val = 2.0f;
        if (i_ten_power > 0)
            for (int i = 0; i < i_ten_power; ++i) delta_val *= 10.0f;
        if (i_ten_power < 0)
            for (int i = 0; i < -i_ten_power; ++i) delta_val *= 0.1f;
    }
    *rp_delta = delta_val;
}

realtype mthCalcRoundedValue(realtype input, realtype delta, realtype out_min, realtype out_max)
{
    realtype temp = input / delta;
    temp += temp >= 0.0f ? 0.5f : -0.5f;
    long rounded = (long)temp;
    temp = (realtype)(rounded * delta);
    if (temp < out_min) temp = out_min;
    if (temp > out_max) temp = out_max;
    return temp;
}

realtype mthCalcClosestNiceValue(realtype input, realtype num_places)
{
    realtype remainder = num_places - (int)num_places;
    if (input <= 0.0f || remainder <= 0.0f) return input;

    int decimal_places = (int)log10(input) + 1;
    realtype log_round = decimal_places - (int)num_places + (realtype)log10(remainder);
    realtype round_val = (realtype)pow(10.0, (double)log_round);
    remainder = (realtype)fmod(input, round_val);
    int whole = (int)(input / round_val);
    realtype result = (realtype)(whole * round_val);
    if (remainder >= round_val * 0.5f) result += round_val;
    return result;
}

int mthIsLong_Wide(wchar_t* text, int* is_long)
{
    if (!is_long) return NOT_OKAY;
    *is_long = IS_FALSE;
    if (!text) return NOT_OKAY;

    const int length = (int)wcslen(text);
    if (length <= 0) return OKAY;

    for (int i = 0; i < length; ++i) {
        if (text[i] < L'0' || text[i] > L'9') {
            if (i != 0 || text[i] != L'-') return OKAY;
        }
    }

    *is_long = IS_TRUE;
    return OKAY;
}

int pstrCalcLocationOfStrInStr_Wide(wchar_t* haystack, wchar_t* needle,
                                    int start, int* location, int* found)
{
    if (!haystack || !needle || !location || !found || start < 0) return NOT_OKAY;
    *found = IS_FALSE;
    *location = -1;

    if ((size_t)start > wcslen(haystack)) return OKAY;

    wchar_t* pos = wcsstr(haystack + start, needle);
    if (pos) {
        *location = (int)(pos - haystack);
        *found = IS_TRUE;
    }
    return OKAY;
}

namespace {
std::string wide_to_utf8(const wchar_t* text)
{
    if (!text) return {};
    try {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> convert;
        return convert.to_bytes(text);
    } catch (...) {
        return {};
    }
}

std::wstring utf8_to_wide(const char* text)
{
    if (!text) return {};
    try {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> convert;
        return convert.from_bytes(text);
    } catch (...) {
        return {};
    }
}

std::string linux_path(const wchar_t* text)
{
    std::string result = wide_to_utf8(text);
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
}
}

int pstrCovertWideCharStringToUTF8String_WithAlloc(wchar_t* input, char** output, int* bytes)
{
    if (!input || !output || !bytes) return NOT_OKAY;
    std::string converted = wide_to_utf8(input);
    *bytes = (int)converted.size() + 1;
    *output = (char*)calloc((size_t)*bytes, 1);
    if (!*output) return NOT_OKAY;
    memcpy(*output, converted.c_str(), converted.size() + 1);
    return OKAY;
}

int pstrCovertUTF8StringToWideCharString_WithAlloc(char* input, wchar_t** output, int* chars)
{
    if (!input || !output || !chars) return NOT_OKAY;
    std::wstring converted = utf8_to_wide(input);
    *chars = (int)converted.size() + 1;
    *output = (wchar_t*)calloc((size_t)*chars, sizeof(wchar_t));
    if (!*output) return NOT_OKAY;
    wmemcpy(*output, converted.c_str(), converted.size() + 1);
    return OKAY;
}

FILE* fileOpen_Wide(wchar_t* path, wchar_t* mode, CSlout*)
{
    if (!path || !mode) return NULL;
    std::string p = linux_path(path);
    std::string m = wide_to_utf8(mode);
    return fopen(p.c_str(), m.c_str());
}

int fileExist_Wide(wchar_t* path, int* exists)
{
    if (!path || !exists) return NOT_OKAY;
    std::error_code ec;
    *exists = std::filesystem::exists(linux_path(path), ec) ? IS_TRUE : IS_FALSE;
    return OKAY;
}


wchar_t* fxsound_fgetws(wchar_t* dst, int count, FILE* stream)
{
    if (!dst || count <= 1 || !stream)
        return nullptr;

    // Keep the underlying FILE byte-oriented. FxSound's legacy preset parser
    // mixes fgets() and fgetws(), which MSVCRT permits but glibc does not.
    std::vector<char> bytes(static_cast<size_t>(count) * 4u + 4u, 0);
    if (!fgets(bytes.data(), static_cast<int>(bytes.size()), stream))
        return nullptr;

    std::wstring wide = utf8_to_wide(bytes.data());

    const size_t take = std::min<size_t>(wide.size(), static_cast<size_t>(count - 1));
    if (take > 0)
        std::wmemcpy(dst, wide.data(), take);
    dst[take] = L'\0';

    return dst;
}


int fxsound_fwprintf(FILE* stream, const wchar_t* format, ...)
{
    if (!stream || !format)
        return -1;

    const std::wstring translated = fxsound_translate_wprintf_format(format);
    std::vector<wchar_t> buffer(1024);

    va_list args;
    va_start(args, format);

    int rc = -1;
    for (;;) {
        va_list copy;
        va_copy(copy, args);
        rc = ::vswprintf(buffer.data(), buffer.size(), translated.c_str(), copy);
        va_end(copy);

        if (rc >= 0 && static_cast<size_t>(rc) < buffer.size())
            break;

        if (buffer.size() >= (1u << 20)) {
            va_end(args);
            return -1;
        }
        buffer.resize(buffer.size() * 2);
    }
    va_end(args);

    const std::string utf8 = wide_to_utf8(buffer.data());
    if (utf8.empty() && rc != 0)
        return -1;

    const size_t written = fwrite(utf8.data(), 1, utf8.size(), stream);
    return written == utf8.size() ? rc : -1;
}
