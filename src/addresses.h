// addresses.h - every fixed address and byte pattern the plugin uses.
//
// ONE PLACE FOR ALL OF THEM. Nothing else in the source may carry a literal
// address. docs/ADDRESSES.md gives the derivation of each one.
//
// Provenance: read from the client binaries by static analysis (disassembly,
// hex dump, PE headers). Re-read on 2026-09-29 from two stock builds and one
// vendor-patched build; every value below matched on all three except the
// vendor string itself, which the patched build has changed (that is what
// gate check 3 exists to detect). No value here has been observed in a
// running process yet - see README "Status".
//
// The client image has no relocation directory and DYNAMICBASE off, so it
// always loads at its preferred base and these absolute addresses are stable.

#pragma once
#include <cstdint>
#include <cstddef>

namespace addr {

// --- gate check 1 and 2: the image itself --------------------------------
constexpr uintptr_t kImageBase       = 0x00400000;  // preferred = actual base
constexpr uint32_t  kTextRva         = 0x00001000;  // ".text" VirtualAddress
constexpr uint32_t  kTextVirtualSize = 0x004CB000;  // ".text" VirtualSize

// --- the vendor name and the pointer that carries it ---------------------
// Stock builds keep the bare token "SONICTEAM" here (12-byte slot).
constexpr uintptr_t kVendorString    = 0x0097D0D8;
constexpr char      kStockVendor[]   = "SONICTEAM";  // compared with its NUL

// The pointer global pushed as the vendor argument. Holds kVendorString
// until the game's name initialiser copies it. THIS is what the swap writes.
constexpr uintptr_t kVendorPtr       = 0x009FF8CC;

// Its two neighbours, read for the report and for the migration path only.
constexpr uintptr_t kDataPtr         = 0x009FF8D0;  // -> "data"
constexpr uintptr_t kProductPtr      = 0x009FF8D4;  // -> "PSOBB"

// --- gate check 5: the instruction that consumes the pointer -------------
// push dword ptr [0x009FF8CC]
constexpr uintptr_t kPushVendorInsn  = 0x007A609B;
constexpr uint8_t   kPushVendorBytes[6] = { 0xFF, 0x35, 0xCC, 0xF8, 0x9F, 0x00 };

// --- the destination buffer the vendor name is copied into ---------------
// Known builds: 64 bytes, so a replacement name may be at most 63 characters.
// The copy into it is unbounded; exceeding this is a buffer overflow.
constexpr size_t    kVendorBufferSize = 64;

// --- build identity, for the log only (never used to gate) ----------------
// A "<Brand>Ver%d%d%d" string at this FILE offset distinguishes distributions.
constexpr uint32_t  kBrandFileOffset = 0x0056CE20;

// --- slow path: the structural pattern (design section 5.2) ---------------
//   FF 35 ?? ?? ?? ??   push [A]      ; -> "data"
//   FF 35 ?? ?? ?? ??   push [B]      ; -> "PSOBB"
//   FF 35 ?? ?? ?? ??   push [C]      ; -> "SONICTEAM"   <- vendor slot
//   E8 ?? ?? ?? ??      call StoreNames
//   83 C4 0C            add esp, 0Ch
constexpr size_t    kPatternLen      = 26;

// If the destination buffer size cannot be recovered on an unfamiliar build,
// the safe cap is the length of the string being replaced: it already fit.
constexpr size_t    kSafeFallbackCap = 9;   // strlen("SONICTEAM")

} // namespace addr
