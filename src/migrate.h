// migrate.h - one-time migration of existing settings into the new key.
//
// Mode 2 (copy): allowlisted values are copied; the source is opened
//                read-only and is never modified.
// Mode 1 (move): the player is asked first. On Yes: a .reg backup of the
//                values about to be removed is written, the values are
//                copied, completion is recorded, and only then are the
//                copied values removed from the source. On No, or if the
//                prompt cannot be shown, or if the backup cannot be written,
//                it completes as a copy instead.
//
// Only these nine value names are ever read, copied, backed up or removed:
//   CTRLBUF GRAPHICCTRL SOUNDCTRL WINDOW_MODE VSync BitDepth WORD_WRAP
//   FONT_JPN FOCUS_SOUND
// Login and server values are never read, copied, backed up or removed.
// Nothing is ever enumerated: values are opened by name only.
//
// Runs at most once per vendor name. The record of having run lives in the
// registry (HKCU\Software\<vendor>\psobb-regkey), not in the INI, because an
// INI write can fail silently or be undone by an updater.
//
// Failure is never fatal to the game and never undoes the redirect.

#pragma once
#include <string>

struct Config;

struct MigrationContext {
    std::string  vendor;     // destination vendor (already validated)
    std::string  source;     // source vendor (already validated)
    std::string  product;    // the client's product key name, e.g. "PSOBB"
    int          mode = 0;   // 1 or 2
    std::wstring iniPath;    // for the one-line mode reset
    std::wstring gameDir;    // where the .reg backup is written
};

void RunMigration(const MigrationContext& ctx);

// Startup self-check: the allowlist and the never-touch list must not overlap.
// If they ever do, migration is disabled entirely.
bool MigrationListsAreSane();
