// migrate.cpp - see migrate.h for the contract.

#include "migrate.h"
#include "config.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <vector>

namespace {

// The ONLY value names this plugin ever reads, copies, backs up or removes.
// Deliberately wider than any one client needs: the plugin is for clients we
// cannot inspect, so entries are not to be trimmed on the grounds that one
// particular client does not use them.
const wchar_t* const kAllowlist[] = {
    L"CTRLBUF", L"GRAPHICCTRL", L"SOUNDCTRL", L"WINDOW_MODE", L"VSync",
    L"BitDepth", L"WORD_WRAP", L"FONT_JPN", L"FOCUS_SOUND",
};

// Login and server values. Never read, copied, backed up or removed. This
// list is not used to filter anything (nothing is ever enumerated); it exists
// so the startup self-check can prove the allowlist never overlaps it.
const wchar_t* const kNeverTouch[] = {
    L"ACCOUNT", L"PASSWORD", L"ACCOUNT_CTRL", L"ACCOUNT_CHECK",
    L"OFFICIAL_SITE", L"BILLING_SITE", L"CLIENT_CODE", L"INSTALL",
};

const wchar_t kRecordSubkey[] = L"psobb-regkey";
constexpr DWORD kMaxValueBytes = 64 * 1024;

struct Value {
    std::wstring      name;
    DWORD             type = 0;
    std::vector<BYTE> data;
    bool              written = false;   // copied AND read back identical
};

std::wstring Widen(const std::string& s)
{
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::string Ascii(const std::wstring& w)   // log messages only
{
    std::string s;
    for (wchar_t c : w) s.push_back((c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '?');
    return s;
}

std::wstring SubkeyPath(const std::string& vendor, const std::wstring& child)
{
    return L"Software\\" + Widen(vendor) + L"\\" + child;
}

bool SameValue(const Value& a, const Value& b)
{
    return a.type == b.type && a.data == b.data;
}

// Read one named value. ERROR_SUCCESS, ERROR_FILE_NOT_FOUND, or another error.
LONG ReadValue(HKEY h, const wchar_t* name, Value& v)
{
    v.name = name;
    v.data.clear();
    DWORD type = 0, size = 0;
    LONG rc = RegQueryValueExW(h, name, nullptr, &type, nullptr, &size);
    if (rc != ERROR_SUCCESS) return rc;
    if (size > kMaxValueBytes) return ERROR_MORE_DATA;
    v.type = type;
    if (size == 0) return ERROR_SUCCESS;
    v.data.resize(size);
    DWORD got = size;
    rc = RegQueryValueExW(h, name, nullptr, &type, v.data.data(), &got);
    if (rc != ERROR_SUCCESS) return rc;
    v.type = type;
    v.data.resize(got);
    return ERROR_SUCCESS;
}

// 0 = the key has none of the allowlisted values (or does not exist),
// 1 = it has at least one, -1 = it could not be checked.
int AnyAllowlisted(const std::wstring& path)
{
    HKEY h = nullptr;
    LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_QUERY_VALUE, &h);
    if (rc == ERROR_FILE_NOT_FOUND) return 0;
    if (rc != ERROR_SUCCESS) return -1;
    int result = 0;
    for (const wchar_t* name : kAllowlist) {
        DWORD size = 0;
        LONG q = RegQueryValueExW(h, name, nullptr, nullptr, nullptr, &size);
        if (q == ERROR_SUCCESS) { result = 1; break; }
        if (q != ERROR_FILE_NOT_FOUND) { result = -1; break; }
    }
    RegCloseKey(h);
    return result;
}

// The completion record: HKCU\Software\<vendor>\psobb-regkey\Migrated (DWORD).
// Returns true and fills `migrated` (0 if absent). False = could not read.
bool ReadRecord(const std::string& vendor, DWORD& migrated, std::wstring& on)
{
    migrated = 0;
    on.clear();
    HKEY h = nullptr;
    LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, SubkeyPath(vendor, kRecordSubkey).c_str(), 0,
                            KEY_QUERY_VALUE, &h);
    if (rc == ERROR_FILE_NOT_FOUND) return true;
    if (rc != ERROR_SUCCESS) return false;
    Value v;
    rc = ReadValue(h, L"Migrated", v);
    if (rc == ERROR_SUCCESS && v.type == REG_DWORD && v.data.size() == 4) {
        migrated = *reinterpret_cast<const DWORD*>(v.data.data());
        Value d;
        if (ReadValue(h, L"MigratedOn", d) == ERROR_SUCCESS && d.type == REG_SZ && !d.data.empty())
            on.assign(reinterpret_cast<const wchar_t*>(d.data.data()), d.data.size() / sizeof(wchar_t));
        while (!on.empty() && on.back() == L'\0') on.pop_back();
    } else if (rc != ERROR_FILE_NOT_FOUND && rc != ERROR_SUCCESS) {
        RegCloseKey(h);
        return false;
    }
    RegCloseKey(h);
    return true;
}

std::wstring TodayIso()
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t b[16];
    swprintf(b, 16, L"%04u-%02u-%02u", t.wYear, t.wMonth, t.wDay);
    return b;
}

bool SetSz(HKEY h, const wchar_t* name, const std::wstring& s)
{
    return RegSetValueExW(h, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(s.c_str()),
                          static_cast<DWORD>((s.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

bool WriteRecord(const std::string& vendor, int mode, const std::string& source,
                 const std::wstring& note)
{
    HKEY h = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, SubkeyPath(vendor, kRecordSubkey).c_str(), 0, nullptr,
                        0, KEY_SET_VALUE, nullptr, &h, nullptr) != ERROR_SUCCESS)
        return false;
    DWORD m = static_cast<DWORD>(mode);
    bool ok = RegSetValueExW(h, L"Migrated", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&m),
                             sizeof(m)) == ERROR_SUCCESS;
    ok = SetSz(h, L"MigratedOn", TodayIso()) && ok;
    ok = SetSz(h, L"MigratedFrom", Widen(source)) && ok;
    ok = SetSz(h, L"MigratedNote", note) && ok;
    RegCloseKey(h);
    return ok;
}

// ---------------------------------------------------------------------------
// The .reg backup. UTF-16LE with BOM, the format regedit writes and restores
// on double-click. Every value is written as the exact bytes and type read
// from the registry, so a restore is byte-identical.
// ---------------------------------------------------------------------------
std::wstring ChooseBackupPath(const std::wstring& gameDir)
{
    const std::wstring stem = gameDir + L"\\psobb-settings-backup-" + TodayIso();
    for (int i = 1; i < 100; ++i) {
        std::wstring p = stem;
        if (i > 1) p += L"-" + std::to_wstring(i);
        p += L".reg";
        if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES &&
            GetLastError() == ERROR_FILE_NOT_FOUND)
            return p;   // never overwrite an existing backup
    }
    return std::wstring();
}

std::wstring EscapeRegName(const std::wstring& n)
{
    std::wstring o;
    for (wchar_t c : n) {
        if (c == L'\\' || c == L'"') o.push_back(L'\\');
        o.push_back(c);
    }
    return o;
}

std::wstring RegLine(const Value& v)
{
    std::wstring line = L"\"" + EscapeRegName(v.name) + L"\"=";
    wchar_t b[24];
    if (v.type == REG_DWORD && v.data.size() == 4) {
        DWORD d = *reinterpret_cast<const DWORD*>(v.data.data());
        swprintf(b, 24, L"dword:%08x", static_cast<unsigned>(d));
        return line + b;
    }
    if (v.type == REG_BINARY) {
        line += L"hex:";
    } else {
        swprintf(b, 24, L"hex(%x):", static_cast<unsigned>(v.type));
        line += b;
    }
    for (size_t i = 0; i < v.data.size(); ++i) {
        swprintf(b, 24, L"%02x", static_cast<unsigned>(v.data[i]));
        line += b;
        if (i + 1 < v.data.size()) {
            line += L",";
            if ((i + 1) % 24 == 0) line += L"\\\r\n  ";   // regedit-style continuation
        }
    }
    return line;
}

bool WriteRegBackup(const std::wstring& path, const std::string& source,
                    const std::string& product, const std::vector<Value>& vals)
{
    std::wstring text = L"Windows Registry Editor Version 5.00\r\n\r\n";
    text += L"; psobb-regkey: settings backup, written before they were moved.\r\n";
    text += L"; Double-click this file to put them back. Login values are not in it\r\n";
    text += L"; because the plugin never touches them.\r\n\r\n";
    text += L"[HKEY_CURRENT_USER\\Software\\" + Widen(source) + L"\\" + Widen(product) + L"]\r\n";
    for (const auto& v : vals) text += RegLine(v) + L"\r\n";
    text += L"\r\n";

    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const BYTE bom[2] = { 0xFF, 0xFE };
    DWORD w1 = 0, w2 = 0;
    const DWORD bytes = static_cast<DWORD>(text.size() * sizeof(wchar_t));
    bool ok = WriteFile(h, bom, 2, &w1, nullptr) && w1 == 2 &&
              WriteFile(h, text.data(), bytes, &w2, nullptr) && w2 == bytes &&
              FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok) return false;

    // Confirm it is on disk at the size written, rather than trusting the calls.
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) return false;
    return fad.nFileSizeHigh == 0 && fad.nFileSizeLow == bytes + 2;
}

// ---------------------------------------------------------------------------
// The one-time prompt (design 8.8). Default button is No. Anything other
// than an explicit Yes - including no interactive desktop - is a No.
// ---------------------------------------------------------------------------
bool PlayerSaysYes(const std::string& vendor, const std::wstring& backupPath)
{
    DWORD sid = 0;
    if (ProcessIdToSessionId(GetCurrentProcessId(), &sid) && sid == 0) {
        plog::Line("no interactive desktop (session 0): treating the prompt as No");
        return false;
    }
    std::wstring title = Widen(vendor) + L" \x2014 one-time settings move";
    std::wstring body =
        L"This copy of the game keeps its settings in its own place in the Windows\n"
        L"registry, rather than in the shared location that stock PSOBB clients use.\n\n"
        L"Your existing settings are still in that shared location: your controls and\n"
        L"key bindings, and your graphics, sound and window options. This will move\n"
        L"them across so they carry over.\n\n"
        L"Your saved login is not touched. It stays where it is, and you'll be asked\n"
        L"to sign in once.\n\n"
        L"Before anything is removed, a backup is saved to:\n\n"
        L"    " + backupPath + L"\n\n"
        L"Double-clicking that file puts the old settings back.\n\n"
        L"One thing worth knowing: if you also play on another PSOBB server from a\n"
        L"different folder, that client may be using some of these same shared\n"
        L"settings, and moving them could reset its controls. If you're not sure,\n"
        L"choose No \x2014 your settings will be copied instead, nothing will be removed,\n"
        L"and both will keep working.\n\n"
        L"Move the settings now?\n\n"
        L"    Yes  -  move them (a backup is saved first)\n"
        L"    No   -  copy them instead, and leave the originals alone\n\n"
        L"You'll only be asked this once.";
    int r = MessageBoxW(nullptr, body.c_str(), title.c_str(),
                        MB_YESNO | MB_ICONQUESTION | MB_TOPMOST | MB_SETFOREGROUND | MB_DEFBUTTON2);
    if (r == 0) {
        plog::Line("the prompt could not be shown (error %lu): treating as No", GetLastError());
        return false;
    }
    return r == IDYES;
}

// Nothing to migrate: record completion anyway (so the shared location is not
// re-scanned every launch), reset the mode, and stop. No prompt, no backup.
void CompleteWithNothing(const MigrationContext& ctx, const char* why)
{
    plog::Line("nothing to migrate: %s", why);
    std::wstring note = L"nothing found at source: " + Widen(why);
    if (!WriteRecord(ctx.vendor, ctx.mode, ctx.source, note)) {
        plog::Line("could not write the completion record; the check will run again next launch");
        return;
    }
    plog::Line("completion recorded (Migrated=%d, nothing found)", ctx.mode);
    if (!ResetModeToZero(ctx.iniPath))
        plog::Line("could not write mode = 0 back to the INI; the registry record prevents a repeat");
}

} // namespace

bool MigrationListsAreSane()
{
    for (const wchar_t* a : kAllowlist)
        for (const wchar_t* n : kNeverTouch)
            if (lstrcmpiW(a, n) == 0) return false;
    return true;
}

void RunMigration(const MigrationContext& ctx)
{
    plog::Line("migration: mode %d requested; from HKCU\\Software\\%s\\%s to HKCU\\Software\\%s\\%s",
               ctx.mode, ctx.source.c_str(), ctx.product.c_str(), ctx.vendor.c_str(),
               ctx.product.c_str());

    if (ctx.mode != 1 && ctx.mode != 2) return;
    if (ctx.source.empty() || ctx.product.empty()) {
        plog::Line("migration refused: source or product name unavailable");
        return;
    }
    if (EqualsNoCase(ctx.source, ctx.vendor)) {
        plog::Line("migration refused: source and vendor are the same key");
        return;
    }

    // 1. Already done? The registry record decides, not the INI.
    DWORD done = 0;
    std::wstring doneOn;
    if (!ReadRecord(ctx.vendor, done, doneOn)) {
        plog::Line("migration refused: could not read the completion record");
        return;
    }
    if (done != 0) {
        plog::Line("mode=%d requested but migration already recorded (Migrated=%lu on %s); ignoring",
                   ctx.mode, done, Ascii(doneOn).c_str());
        if (!ResetModeToZero(ctx.iniPath))
            plog::Line("could not write mode = 0 back to the INI; the registry record prevents a repeat");
        return;
    }

    const std::wstring srcPath = SubkeyPath(ctx.source, Widen(ctx.product));
    const std::wstring dstPath = SubkeyPath(ctx.vendor, Widen(ctx.product));

    // Refuse if the destination already holds any allowlisted value: someone
    // has been using this client already, and their live settings are not to
    // be written over.
    int dst = AnyAllowlisted(dstPath);
    if (dst != 0) {
        plog::Line(dst > 0
            ? "migration refused: the destination already holds settings; they are not overwritten"
            : "migration refused: the destination could not be checked");
        return;
    }

    // 2. Read the allowlisted values from the source, read-only.
    HKEY hs = nullptr;
    LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, srcPath.c_str(), 0, KEY_READ, &hs);
    if (rc == ERROR_FILE_NOT_FOUND) { CompleteWithNothing(ctx, "source key absent"); return; }
    if (rc != ERROR_SUCCESS) {
        plog::Line("migration abandoned: could not open the source key (error %ld)", rc);
        return;
    }
    std::vector<Value> vals;
    for (const wchar_t* name : kAllowlist) {
        Value v;
        LONG q = ReadValue(hs, name, v);
        if (q == ERROR_SUCCESS) vals.push_back(v);
        else if (q != ERROR_FILE_NOT_FOUND) {
            plog::Line("migration abandoned: could not read %s (error %ld)", Ascii(name).c_str(), q);
            RegCloseKey(hs);
            return;
        }
    }
    RegCloseKey(hs);
    if (vals.empty()) {
        CompleteWithNothing(ctx, "source key present, no migratable values");
        return;
    }
    for (const auto& v : vals)
        plog::Line("found at source: %s (type %lu, %u bytes)", Ascii(v.name).c_str(), v.type,
                   static_cast<unsigned>(v.data.size()));

    // 3-4. Move only: ask, then back up. Any failure degrades to copy.
    int effective = ctx.mode;
    std::wstring backup;
    if (effective == 1) {
        backup = ChooseBackupPath(ctx.gameDir);
        if (backup.empty()) {
            plog::Line("NO BACKUP PATH AVAILABLE: completing as a copy; nothing will be removed");
            effective = 2;
        } else if (!PlayerSaysYes(ctx.vendor, backup)) {
            plog::Line("player chose No (or the prompt was unavailable): completing as a copy");
            effective = 2;
        } else if (!WriteRegBackup(backup, ctx.source, ctx.product, vals)) {
            plog::Line("BACKUP COULD NOT BE WRITTEN to %s: completing as a copy; nothing will be removed",
                       Ascii(backup).c_str());
            effective = 2;
        } else {
            plog::Line("backup written: %s", Ascii(backup).c_str());
        }
    }

    // 5. Write to the destination, reading each value back to confirm it.
    HKEY hd = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, dstPath.c_str(), 0, nullptr, 0,
                        KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr, &hd, nullptr) != ERROR_SUCCESS) {
        plog::Line("migration abandoned: could not create the destination key");
        return;
    }
    unsigned written = 0;
    for (auto& v : vals) {
        LONG w = RegSetValueExW(hd, v.name.c_str(), 0, v.type,
                                v.data.empty() ? nullptr : v.data.data(),
                                static_cast<DWORD>(v.data.size()));
        Value back;
        v.written = (w == ERROR_SUCCESS) &&
                    ReadValue(hd, v.name.c_str(), back) == ERROR_SUCCESS && SameValue(back, v);
        if (v.written) ++written;
        else plog::Line("could not copy %s", Ascii(v.name).c_str());
    }
    RegCloseKey(hd);
    if (written != vals.size()) {
        plog::Line("migration abandoned: copied %u of %u values. Nothing recorded, nothing removed. "
                   "The destination now holds some settings, so the plugin will not retry on its own.",
                   written, static_cast<unsigned>(vals.size()));
        return;
    }

    // 6. Record completion BEFORE anything is removed. If the process dies
    //    during removal, the next launch sees the record and does nothing:
    //    some values are left behind (untidy), nothing is lost.
    std::wstring note = effective == 1 ? L"moved" : L"copied";
    if (!WriteRecord(ctx.vendor, effective, ctx.source, note)) {
        plog::Line("COULD NOT WRITE THE COMPLETION RECORD: nothing will be removed");
        return;
    }
    plog::Line("completion recorded (Migrated=%d)", effective);

    // 7. Move only: remove from the source, value by value, only what was
    //    verifiably copied, and only if it still matches what was copied.
    if (effective == 1) {
        HKEY hw = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, srcPath.c_str(), 0, KEY_QUERY_VALUE | KEY_SET_VALUE,
                          &hw) != ERROR_SUCCESS) {
            plog::Line("could not open the source for removal: all values left in place "
                       "(untidy, nothing lost)");
        } else {
            for (const auto& v : vals) {
                if (!v.written) continue;
                Value now;
                if (ReadValue(hw, v.name.c_str(), now) != ERROR_SUCCESS || !SameValue(now, v)) {
                    plog::Line("left in place (changed since it was copied): %s", Ascii(v.name).c_str());
                    continue;
                }
                if (RegDeleteValueW(hw, v.name.c_str()) == ERROR_SUCCESS)
                    plog::Line("removed from source: %s", Ascii(v.name).c_str());
                else
                    plog::Line("could not remove from source: %s", Ascii(v.name).c_str());
            }
            RegCloseKey(hw);
        }
    }

    // 8. Tidy the INI. Not the mechanism: the registry record is.
    if (!ResetModeToZero(ctx.iniPath))
        plog::Line("could not write mode = 0 back to the INI; the registry record prevents a repeat");

    plog::Line("migration finished: %s %u value(s)", effective == 1 ? "moved" : "copied", written);
}
