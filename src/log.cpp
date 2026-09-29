// log.cpp

#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdio>

namespace plog {

namespace {
std::wstring g_path;
bool g_open = false;
constexpr LONGLONG kRotateBytes = 1024 * 1024;
}

void Open(const std::wstring& path)
{
    if (g_open) return;
    g_path = path;
    g_open = true;

    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExW(g_path.c_str(), GetFileExInfoStandard, &fad)) {
        LARGE_INTEGER sz;
        sz.LowPart = fad.nFileSizeLow;
        sz.HighPart = static_cast<LONG>(fad.nFileSizeHigh);
        if (sz.QuadPart > kRotateBytes) {
            std::wstring old = g_path + L".old";
            MoveFileExW(g_path.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
    }
}

const std::wstring& Path() { return g_path; }

void Line(const char* fmt, ...)
{
    if (!g_open) return;

    char body[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    if (n < 0) return;

    SYSTEMTIME t;
    GetLocalTime(&t);
    char line[1100];
    int m = snprintf(line, sizeof(line), "%04u-%02u-%02u %02u:%02u:%02u  %s\r\n",
                     t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, body);
    if (m <= 0) return;
    if (m >= static_cast<int>(sizeof(line))) m = static_cast<int>(sizeof(line)) - 1;

    HANDLE h = CreateFileW(g_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(h, line, static_cast<DWORD>(m), &written, nullptr);
    CloseHandle(h);
}

} // namespace plog
