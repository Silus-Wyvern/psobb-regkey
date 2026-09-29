// config.cpp

#include "config.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

// A default no real INI value can equal, so "absent" and "empty" can be told apart.
const wchar_t kAbsent[] = L"\x01<absent>\x01";

bool ReadIni(const std::wstring& ini, const wchar_t* section, const wchar_t* key,
             std::wstring& out)
{
    wchar_t buf[1024];
    GetPrivateProfileStringW(section, key, kAbsent, buf,
                             static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])), ini.c_str());
    out = buf;
    return out != kAbsent;   // false = key absent
}

std::wstring TrimW(const std::wstring& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == L' ' || s[a] == L'\t')) ++a;
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t')) --b;
    return s.substr(a, b - a);
}

// Wide -> system code page (what the client's ANSI registry calls use).
// Fails if any character cannot be represented exactly.
bool ToAnsi(const std::wstring& w, std::string& out)
{
    out.clear();
    if (w.empty()) return true;
    BOOL lossy = FALSE;
    int n = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, w.c_str(),
                                static_cast<int>(w.size()), nullptr, 0, nullptr, &lossy);
    if (n <= 0 || lossy) return false;
    out.resize(static_cast<size_t>(n));
    WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, w.c_str(), static_cast<int>(w.size()),
                        &out[0], n, nullptr, &lossy);
    return !lossy;
}

std::string Narrow(const std::wstring& w)   // for log messages only
{
    std::string s;
    for (wchar_t c : w) s.push_back((c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '?');
    return s;
}

} // namespace

bool EqualsNoCase(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

bool ValidateKeyName(const std::string& name, size_t cap, const std::string& capReason,
                     const char* what)
{
    if (name.empty()) {
        plog::Line("Refusing: %s is empty", what);
        return false;
    }
    if (name.size() > cap) {
        // Never truncate: a shortened name is a different key, and two
        // names sharing a prefix would collide.
        plog::Line("Refusing: %s is %u characters; the limit on this build is %u (%s). "
                   "It was not shortened - shorten it in the INI.",
                   what, static_cast<unsigned>(name.size()), static_cast<unsigned>(cap),
                   capReason.c_str());
        return false;
    }
    for (char c : name) {
        if (c == '\\' || c == '/') {
            // A backslash would quietly create nested keys - the worst kind
            // of failure, because it appears to work.
            plog::Line("Refusing: %s contains '%c'; key names may not contain \\ or /", what, c);
            return false;
        }
        if (static_cast<unsigned char>(c) < 0x20) {
            plog::Line("Refusing: %s contains a control character", what);
            return false;
        }
    }
    bool unusual = false;
    for (char c : name) {
        bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                     (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_' || c == '.';
        if (!plain) { unusual = true; break; }
    }
    if (unusual)
        plog::Line("note: %s \"%s\" has characters other than letters, digits, space, "
                   "- _ . - allowed, but check it is what you meant", what, name.c_str());
    return true;
}

Config ReadConfig(const std::wstring& iniPath)
{
    Config c;
    c.iniPath = iniPath;
    DWORD attrs = GetFileAttributesW(iniPath.c_str());
    c.iniFound = (attrs != INVALID_FILE_ATTRIBUTES) && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
    if (!c.iniFound) return c;

    // --- [registry] vendor ----------------------------------------------
    std::wstring w;
    if (ReadIni(iniPath, L"registry", L"vendor", w)) {
        w = TrimW(w);
        if (!w.empty()) {
            if (ToAnsi(w, c.vendor)) {
                c.vendorSet = true;
            } else {
                plog::Line("Refusing: vendor \"%s\" has characters the system code page "
                           "cannot represent exactly", Narrow(w).c_str());
                c.vendorInvalid = true;   // not report mode: refuse outright
            }
        }
    }

    // --- [migration] mode -----------------------------------------------
    // Read as a string and validated explicitly. Numbers only; words such as
    // "move" or "copy" are refused, and a refused value is never rewritten.
    if (!ReadIni(iniPath, L"migration", L"mode", w)) {
        c.mode = 0;   // normal state of a settled install; nothing to say
    } else {
        std::wstring t = TrimW(w);
        bool digits = !t.empty();
        for (wchar_t ch : t) if (ch < L'0' || ch > L'9') { digits = false; break; }
        if (t.empty()) {
            plog::Line("mode is empty; treating as 0");
            c.mode = 0;
        } else if (!digits) {
            plog::Line("mode \"%s\" is not recognised; treating as 0. Valid values are 0, 1, 2",
                       Narrow(t).c_str());
            c.mode = 0;
        } else {
            size_t k = 0;
            while (k + 1 < t.size() && t[k] == L'0') ++k;   // leading zeros
            std::wstring d = t.substr(k);
            if (d == L"0") c.mode = 0;
            else if (d == L"1") { c.mode = 1; c.modeLiteralActive = true; }
            else if (d == L"2") { c.mode = 2; c.modeLiteralActive = true; }
            else {
                plog::Line("mode \"%s\" is not recognised; treating as 0. Valid values are 0, 1, 2",
                           Narrow(t).c_str());
                c.mode = 0;
            }
        }
    }

    // --- [migration] source ---------------------------------------------
    if (ReadIni(iniPath, L"migration", L"source", w)) {
        w = TrimW(w);
        if (!w.empty()) {
            std::string s;
            if (ToAnsi(w, s)) c.source = s;
            else {
                plog::Line("source has characters the system code page cannot represent; "
                           "migration disabled");
                c.source.clear();
            }
        }
    }
    return c;
}

bool ResetModeToZero(const std::wstring& iniPath)
{
    return WritePrivateProfileStringW(L"migration", L"mode", L"0", iniPath.c_str()) != FALSE;
}
