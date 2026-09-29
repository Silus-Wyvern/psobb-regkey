// dllmain.cpp - psobb-regkey: makes a PSOBB Blue Burst client read its
// settings from its own registry key, HKCU\Software\<vendor>\PSOBB, instead
// of the shared default HKCU\Software\SonicTeam\PSOBB. (The game only reads
// its settings; a launcher or settings tool writes them.)
//
// The whole intervention is one guarded 4-byte write: the pointer the game
// uses as its registry "vendor" name is pointed at a name read from
// psobb_regkey.ini, before the game reads it. No hooks, no code patches, and
// the game executable on disk is never modified.
//
// Load order: the client's ASI loader calls load() from the executable's
// entry point, before the game's own start-up code runs, and the game copies
// the vendor name later, during initialisation - so there is no race.
//
// Written from scratch against a static disassembly of the client.

#include "addresses.h"
#include "config.h"
#include "log.h"
#include "migrate.h"
#include "verify.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstring>
#include <string>

#define PSOBB_REGKEY_VERSION "0.1.0 (pre-release, untested)"

namespace {

HMODULE g_self = nullptr;
bool    g_ran  = false;

// The replacement name. Static storage: the game copies from this pointer
// AFTER load() has returned, so it must outlive load() - and the module is
// pinned so it is never unloaded.
char g_vendorName[256] = {};

std::wstring ModulePath(HMODULE m)
{
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(m, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
    if (n == 0 || n >= sizeof(buf) / sizeof(buf[0])) return std::wstring();
    return std::wstring(buf, n);
}

std::wstring DirOf(const std::wstring& p)
{
    size_t s = p.find_last_of(L"\\/");
    return s == std::wstring::npos ? std::wstring(L".") : p.substr(0, s);
}

std::wstring StemOf(const std::wstring& p)   // file name without extension
{
    size_t s = p.find_last_of(L"\\/");
    std::wstring f = (s == std::wstring::npos) ? p : p.substr(s + 1);
    size_t d = f.find_last_of(L'.');
    return d == std::wstring::npos ? f : f.substr(0, d);
}

std::string Ascii(const std::wstring& w)
{
    std::string s;
    for (wchar_t c : w) s.push_back((c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '?');
    return s;
}

// The single write. Re-checks the slot immediately before writing.
bool SwapVendorPointer(const GateResult& g, const std::string& name)
{
    if (name.size() > g.cap || name.size() >= sizeof(g_vendorName)) return false;
    std::memcpy(g_vendorName, name.c_str(), name.size() + 1);

    auto* slot = reinterpret_cast<volatile uint32_t*>(g.vendorSlot);
    if (!IsReadableInImage(g.vendorSlot, 4) || *slot != static_cast<uint32_t>(g.originalVendorString)) {
        plog::Line("Refusing to act: the vendor pointer changed between the check and the write");
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<LPVOID>(g.vendorSlot), 4, PAGE_READWRITE, &old)) {
        plog::Line("Refusing to act: VirtualProtect failed (error %lu)", GetLastError());
        return false;
    }
    *slot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_vendorName));
    DWORD ignored = 0;
    VirtualProtect(reinterpret_cast<LPVOID>(g.vendorSlot), 4, old, &ignored);

    return *slot == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_vendorName));
}

void Run()
{
    // Pin this module: the game reads g_vendorName after load() returns.
    HMODULE pinned = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCWSTR>(&g_vendorName), &pinned);

    const std::wstring self = ModulePath(g_self);
    const std::wstring exe  = ModulePath(nullptr);
    if (self.empty() || exe.empty()) return;   // cannot even place a log: do nothing

    const std::wstring stem    = StemOf(self);            // "psobb_regkey"
    const std::wstring iniPath = DirOf(self) + L"\\" + stem + L".ini";   // next to the plugin
    const std::wstring gameDir = DirOf(exe);
    plog::Open(gameDir + L"\\" + stem + L".log");         // in the game folder

    plog::Line("==== psobb-regkey %s ====", PSOBB_REGKEY_VERSION);
    plog::Line("game: %s", Ascii(exe).c_str());
    plog::Line("config: %s", Ascii(iniPath).c_str());

    Config cfg = ReadConfig(iniPath);
    if (!cfg.iniFound) {
        plog::Line("no config, inactive");
        return;
    }

    // The gate runs on every configured launch, and writes the report.
    GateResult g = RunGate();

    if (!cfg.vendorSet && !cfg.vendorInvalid) {
        plog::Line("report mode: vendor is not set, so nothing was written. "
                   "Set vendor in the INI to redirect.");
        return;
    }
    if (cfg.vendorInvalid) return;       // already logged
    if (!g.passed) return;               // the gate wins; already logged

    if (!ValidateKeyName(cfg.vendor, g.cap, g.capReason, "vendor")) return;
    if (EqualsNoCase(cfg.vendor, "SonicTeam"))
        plog::Line("WARNING: vendor is SonicTeam - that is the shared default location, "
                   "which defeats the purpose of this plugin");

    if (!SwapVendorPointer(g, cfg.vendor)) {
        plog::Line("redirect NOT applied; the client will use its default key. "
                   "No migration was attempted.");
        return;
    }
    plog::Line("redirect applied: settings key is HKCU\\Software\\%s\\%s",
               cfg.vendor.c_str(), g.productFound.empty() ? "<product>" : g.productFound.c_str());

    // Migration is optional, non-fatal, and never undoes the redirect.
    if (cfg.mode == 0) return;
    if (!MigrationListsAreSane()) {
        plog::Line("migration disabled: internal self-check failed (allowlist overlaps the "
                   "never-touch list). This is a bug; please report it.");
        return;
    }
    if (!ValidateKeyName(cfg.source, 255, "registry key name limit", "source")) return;
    if (g.productFound.empty() || !ValidateKeyName(g.productFound, 63, "product buffer", "product")) {
        plog::Line("migration refused: the client's product name could not be read");
        return;
    }

    MigrationContext mc;
    mc.vendor  = cfg.vendor;
    mc.source  = cfg.source;
    mc.product = g.productFound;
    mc.mode    = cfg.mode;
    mc.iniPath = iniPath;
    mc.gameDir = gameDir;
    RunMigration(mc);
}

} // namespace

// The client's ASI loader resolves this by the undecorated name "load".
extern "C" void __stdcall load(void)
{
    if (g_ran) return;
    g_ran = true;
    Run();
}

// void __stdcall load(void) decorates to _load@0 under MSVC; export it under
// the plain name the loader looks up (the same method silus_mod_loader uses).
#if defined(_MSC_VER)
#pragma comment(linker, "/EXPORT:load=_load@0")
#endif

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);
        g_self = hinst;   // everything else happens in load(), off the loader lock
    }
    return TRUE;
}
