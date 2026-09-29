// config.h - the INI next to the plugin.
//
//   [registry]
//   vendor =            ; blank = report mode: check the build, write nothing
//
//   [migration]
//   mode = 0            ; 0 none, 1 move, 2 copy. Anything else is treated as 0.
//   source = SonicTeam  ; where existing settings are migrated from
//
// Values are read as strings and validated here; nothing is guessed.

#pragma once
#include <string>

struct Config {
    bool         iniFound = false;
    std::wstring iniPath;

    // [registry] vendor
    bool         vendorSet = false;     // present and non-empty
    std::string  vendor;                // trimmed, system code page
    bool         vendorInvalid = false; // set but unusable: refuse, do not fall to report mode

    // [migration] mode
    int          mode = 0;              // effective: 0, 1 or 2
    bool         modeLiteralActive = false; // file holds a literal 1 or 2 (after trim/leading zeros)

    // [migration] source
    std::string  source = "SonicTeam";
};

// Reads and validates. Problems are logged; invalid values fail closed
// (mode -> 0). Vendor length is checked later, against the gate's cap.
Config ReadConfig(const std::wstring& iniPath);

// Character and length checks for a registry key name taken from the INI.
// Logs the reason on refusal. `what` names the setting for the message.
bool ValidateKeyName(const std::string& name, size_t cap, const std::string& capReason,
                     const char* what);

// Set [migration] mode back to 0 - one line, in place, via
// WritePrivateProfileStringW. Returns false if the write did not take.
bool ResetModeToZero(const std::wstring& iniPath);

// Case-insensitive ASCII comparison.
bool EqualsNoCase(const std::string& a, const std::string& b);
