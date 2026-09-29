// log.h - a plain text log in the game folder.
//
// Appends (never truncates), so the record of a migration - including every
// removed value name - survives later launches. Rotates to <name>.old once
// it passes 1 MB.

#pragma once
#include <string>

namespace plog {

// Open (or create) the log. Safe to call once; later calls are ignored.
void Open(const std::wstring& path);

// printf-style line, prefixed with a timestamp. Never throws; if the file
// cannot be written the line is silently dropped (the game must never be
// affected by the plugin's logging).
void Line(const char* fmt, ...);

// Where the log is, for messages that point a reader at it.
const std::wstring& Path();

} // namespace plog
