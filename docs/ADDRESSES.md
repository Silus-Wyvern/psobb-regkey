# Addresses and patterns — what each constant is and how it was found

Every constant the plugin uses lives in `src/addresses.h`. This file says where
each one came from. **All of it is static analysis** — read from the client
executable on disk (disassembly, hex dump, PE headers). None of it has yet been
observed in a running process; see the README's *Status* section.

## Builds checked

Re-read on 2026-09-29. Builds are identified by MD5 only.

| label | MD5 of `psobb.exe` | vendor string | result |
|---|---|---|---|
| stock build A | `076c4822be159e499ca0e4df662709b3` | `SONICTEAM` | every constant below matches |
| stock build B | `0e218806b89db0af655e1c176ada2c92` | `SONICTEAM` | every constant below matches |
| vendor-patched build | `d47c34b6ff273db979d6db7b1eef92a5` | changed on disk | all match **except** the vendor string — correctly refused by check 3 |

Stock builds A and B differ only inside the build-identity string; every address
here is the same in both.

## The image

The executable is a 32-bit x86 PE, image base `0x00400000`, with **no relocation
directory and DYNAMICBASE off**, so it always loads at that base and absolute
addresses are stable. `.text` is at RVA `0x00001000` with VirtualSize
`0x004CB000` (gate check 2).

The entry point is a small stub in an appended section that loads `patch.dll`
and calls its `patch` export — that is what loads ASI plugins — before jumping to
the normal C runtime start-up. So an ASI's `load()` runs before any of the game's
own start-up code.

## How the registry key is built

The client never holds a `Software\SonicTeam\PSOBB` string. At start-up a small
initialiser pushes three pointer globals and calls a routine that copies each
string into a fixed 64-byte buffer:

```
0x007A608F  FF 35 D0 F8 9F 00   push [0x009FF8D0]   ; -> "data"
0x007A6095  FF 35 D4 F8 9F 00   push [0x009FF8D4]   ; -> "PSOBB"
0x007A609B  FF 35 CC F8 9F 00   push [0x009FF8CC]   ; -> "SONICTEAM"   (vendor)
0x007A60A1  E8 ...              call 0x0082D650     ; copy the names
0x007A60A6  83 C4 0C            add esp, 0Ch
```

The copy routine puts the vendor into a buffer at `0x00A216C0` and the product
into one at `0x00A21700`. The registry is then opened in three chained steps:
`HKCU` → `Software` → the vendor buffer → the product buffer. The copy routine is
called from exactly one place, once, well after ASI plugins load.

**So pointing `[0x009FF8CC]` at a different string before that call changes the
vendor part of the key, and nothing else.**

## The constants

| constant | value | what it is | how it was confirmed |
|---|---|---|---|
| `kImageBase` | `0x00400000` | fixed image base | PE optional header |
| `kTextRva`, `kTextVirtualSize` | `0x00001000`, `0x004CB000` | `.text` section | PE section table |
| `kVendorString` | `0x0097D0D8` | `SONICTEAM\0` in a 12-byte slot | hex dump |
| `kVendorPtr` | `0x009FF8CC` | pointer global, initialised to `0x0097D0D8` | initialised data |
| `kDataPtr`, `kProductPtr` | `0x009FF8D0`, `0x009FF8D4` | pointers to `data` and `PSOBB` | initialised data |
| `kPushVendorInsn` | `0x007A609B` = `FF 35 CC F8 9F 00` | `push [0x009FF8CC]` | disassembly |
| `kVendorBufferSize` | 64 | vendor buffer `0x00A216C0`–`0x00A216FF` | the copy routine loads the two buffer addresses with `mov esi, imm32` (`BE C0 16 A2 00`, then `BE 00 17 A2 00`); they are 64 bytes apart |
| `kBrandFileOffset` | `0x0056CE20` (file offset) | `<Brand>Ver%d%d%d` build identity — logged, never gates | hex dump |

## The unfamiliar-build path

Used only when the fixed addresses above do not match. It looks for the 26-byte
shape of the initialiser (three `push [imm32]`, a `call`, `add esp, 0Ch`).

**The shape alone is not unique.** On all three builds above it matches **twice**:
once at `0x007A608F` (the initialiser) and once elsewhere, where the three pointers
resolve to empty data. So the plugin requires **exactly one match whose three
pointers resolve to `data`, `PSOBB` and `SONICTEAM`**, and refuses on zero or on
two or more. It never chooses between candidates.

The destination buffer's size is recovered from the first two `mov esi, imm32` in
the called routine. If that fails, the name limit falls back to 9 characters — the
length of `SONICTEAM` — which always fits, because the original did.

**Not yet shown:** that this path recognises a build whose addresses really do
differ. No such build has been examined.

## Adding a build

Launch it with `vendor` blank and read `psobb_regkey.log`. The report names the
path taken, the addresses used and the strings found. A new build that passes the
fast path needs nothing added. Please report one that passes only through the
unfamiliar-build path, or one that is refused.
