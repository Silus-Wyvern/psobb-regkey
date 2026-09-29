// verify.cpp - the gate. Reads memory only. See verify.h and docs/ADDRESSES.md.

#include "verify.h"
#include "addresses.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstring>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// The main image's PE headers, parsed from memory.
// ---------------------------------------------------------------------------
struct Image {
    uintptr_t base = 0;
    const IMAGE_NT_HEADERS32* nt = nullptr;
    const IMAGE_SECTION_HEADER* sections = nullptr;
    WORD nsections = 0;
    bool ok = false;
};

Image LoadImageInfo()
{
    Image im;
    im.base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!im.base) return im;
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(im.base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return im;
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(im.base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return im;
    im.nt = nt;
    im.sections = IMAGE_FIRST_SECTION(nt);
    im.nsections = nt->FileHeader.NumberOfSections;
    im.ok = true;
    return im;
}

const Image& Img()
{
    static Image im = LoadImageInfo();
    return im;
}

uint32_t SectionSpan(const IMAGE_SECTION_HEADER& s)
{
    return s.Misc.VirtualSize ? s.Misc.VirtualSize : s.SizeOfRawData;
}

// The section containing [p, p+len), or nullptr.
const IMAGE_SECTION_HEADER* SectionOf(uintptr_t p, size_t len)
{
    const Image& im = Img();
    if (!im.ok || len == 0) return nullptr;
    for (WORD i = 0; i < im.nsections; ++i) {
        const auto& s = im.sections[i];
        uintptr_t lo = im.base + s.VirtualAddress;
        uintptr_t hi = lo + SectionSpan(s);
        if (p >= lo && p < hi && len <= hi - p) return &s;
    }
    return nullptr;
}

bool PageReadable(DWORD protect)
{
    if (protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;
    const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                           PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                           PAGE_EXECUTE_WRITECOPY;
    return (protect & readable) != 0;
}

// Read a NUL-terminated string of at most maxLen characters, validating every
// byte before it is touched. Returns false if unreadable or unterminated.
bool ReadCString(uintptr_t p, size_t maxLen, std::string& out)
{
    out.clear();
    for (size_t i = 0; i <= maxLen; ++i) {
        if (!IsReadableInImage(p + i, 1)) return false;
        char c = *reinterpret_cast<const char*>(p + i);
        if (c == '\0') return true;
        out.push_back(c);
    }
    return false;
}

bool ReadU32(uintptr_t p, uint32_t& v)
{
    if (!IsReadableInImage(p, 4)) return false;
    std::memcpy(&v, reinterpret_cast<const void*>(p), 4);
    return true;
}

bool ReadStringAt(uintptr_t slot, size_t maxLen, std::string& out, uint32_t* ptrOut = nullptr)
{
    uint32_t v = 0;
    if (!ReadU32(slot, v)) return false;
    if (ptrOut) *ptrOut = v;
    return ReadCString(v, maxLen, out);
}

std::string Hex(uintptr_t v)
{
    char b[16];
    snprintf(b, sizeof(b), "0x%08X", static_cast<unsigned>(v));
    return b;
}

// Printable form of a short string for the log.
std::string Printable(const std::string& s)
{
    std::string o;
    for (unsigned char c : s) {
        if (c >= 0x20 && c < 0x7F) o.push_back(static_cast<char>(c));
        else { char b[8]; snprintf(b, sizeof(b), "\\x%02X", c); o += b; }
    }
    return o;
}

// ---------------------------------------------------------------------------
// Build identity string - log only, never gates anything.
// ---------------------------------------------------------------------------
std::string ReadBrand()
{
    const Image& im = Img();
    if (!im.ok) return "unavailable";
    for (WORD i = 0; i < im.nsections; ++i) {
        const auto& s = im.sections[i];
        if (addr::kBrandFileOffset >= s.PointerToRawData &&
            addr::kBrandFileOffset < s.PointerToRawData + s.SizeOfRawData) {
            uintptr_t va = im.base + s.VirtualAddress + (addr::kBrandFileOffset - s.PointerToRawData);
            std::string b;
            if (ReadCString(va, 40, b) && b.find("Ver%d") != std::string::npos)
                return Printable(b);
            return "not found at the expected offset";
        }
    }
    return "not found at the expected offset";
}

// ---------------------------------------------------------------------------
// FAST PATH - known builds. Five checks, all required, in this order.
// ---------------------------------------------------------------------------
bool FastPath(GateResult& r, std::string& why)
{
    const Image& im = Img();

    // 1. The image is at its fixed base.
    if (im.base != addr::kImageBase) {
        why = "check 1: image base is " + Hex(im.base) + ", expected " + Hex(addr::kImageBase);
        return false;
    }
    // 2. 32-bit x86 PE with the expected .text section.
    if (!im.ok || im.nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        im.nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        why = "check 2: not a 32-bit x86 PE image";
        return false;
    }
    bool textOk = false;
    for (WORD i = 0; i < im.nsections; ++i) {
        const auto& s = im.sections[i];
        if (std::memcmp(s.Name, ".text\0\0\0", 8) == 0 &&
            s.VirtualAddress == addr::kTextRva &&
            s.Misc.VirtualSize == addr::kTextVirtualSize) {
            textOk = true;
            break;
        }
    }
    if (!textOk) {
        why = "check 2: .text section is not at the expected address and size";
        return false;
    }
    // 3. The stock vendor string is present and unmodified (with its NUL).
    //    This alone rejects every client whose vendor string was hex-patched,
    //    which is correct: those do not need this plugin.
    if (!IsReadableInImage(addr::kVendorString, sizeof(addr::kStockVendor)) ||
        std::memcmp(reinterpret_cast<const void*>(addr::kVendorString),
                    addr::kStockVendor, sizeof(addr::kStockVendor)) != 0) {
        std::string seen;
        ReadCString(addr::kVendorString, 11, seen);
        why = "check 3: vendor string at " + Hex(addr::kVendorString) +
              " is \"" + Printable(seen) + "\", not SONICTEAM";
        return false;
    }
    // 4. The pointer still points at that string: nobody has swapped it yet
    //    and the game has not consumed it yet.
    uint32_t v = 0;
    if (!ReadU32(addr::kVendorPtr, v) || v != addr::kVendorString) {
        why = "check 4: vendor pointer at " + Hex(addr::kVendorPtr) + " holds " +
              Hex(v) + ", expected " + Hex(addr::kVendorString);
        return false;
    }
    // 5. The instruction that consumes the pointer is intact.
    if (!IsReadableInImage(addr::kPushVendorInsn, sizeof(addr::kPushVendorBytes)) ||
        std::memcmp(reinterpret_cast<const void*>(addr::kPushVendorInsn),
                    addr::kPushVendorBytes, sizeof(addr::kPushVendorBytes)) != 0) {
        why = "check 5: the push of the vendor pointer at " + Hex(addr::kPushVendorInsn) +
              " is not the expected bytes";
        return false;
    }

    r.path = "fast";
    r.vendorSlot = addr::kVendorPtr;
    r.originalVendorString = addr::kVendorString;
    r.vendorFound = addr::kStockVendor;
    ReadStringAt(addr::kProductPtr, 63, r.productFound);
    ReadStringAt(addr::kDataPtr, 63, r.dataFound);
    r.cap = addr::kVendorBufferSize - 1;
    r.capReason = "destination buffer is 64 bytes on this known build";
    return true;
}

// ---------------------------------------------------------------------------
// SLOW PATH - unfamiliar builds.
//
// The 26-byte shape alone is NOT unique: on every build examined it matches
// twice. One match is the name initialiser (its three pointers resolve to
// "data", "PSOBB", "SONICTEAM"); the other resolves to nothing. So the rule
// is: exactly one match that ALSO passes that three-string corroboration.
// Zero, or two or more, corroborated matches: refuse. (Design amendment,
// approved 2026-09-29.) We never choose between corroborated candidates.
// ---------------------------------------------------------------------------
struct Candidate {
    uintptr_t at = 0;
    uint32_t  slotA = 0, slotB = 0, slotC = 0;
    uintptr_t callTarget = 0;
    std::string a, b, c;
    bool corroborated = false;
    std::string note;
};

bool MatchesShape(const uint8_t* p)
{
    return p[0] == 0xFF && p[1] == 0x35 &&
           p[6] == 0xFF && p[7] == 0x35 &&
           p[12] == 0xFF && p[13] == 0x35 &&
           p[18] == 0xE8 &&
           p[23] == 0x83 && p[24] == 0xC4 && p[25] == 0x0C;
}

bool InExecutableSection(uintptr_t p)
{
    const IMAGE_SECTION_HEADER* s = SectionOf(p, 1);
    return s && (s->Characteristics & IMAGE_SCN_MEM_EXECUTE);
}

bool InWritableSection(uintptr_t p)
{
    const IMAGE_SECTION_HEADER* s = SectionOf(p, 1);
    return s && (s->Characteristics & IMAGE_SCN_MEM_WRITE);
}

// Recover the vendor buffer's size from the start of the copy routine: its
// first two "mov esi, imm32" (opcode BE) load the vendor and product buffer
// addresses, and their difference is the vendor buffer's size. Returns 0 if
// it cannot be recovered with confidence.
// UNVERIFIED on any build other than the three examined, where the two
// immediates are 64 bytes apart.
size_t RecoverBufferSize(uintptr_t fn)
{
    const size_t window = 64;
    if (!IsReadableInImage(fn, window)) return 0;
    const auto* b = reinterpret_cast<const uint8_t*>(fn);
    uint32_t found[2] = {0, 0};
    int n = 0;
    for (size_t i = 0; i + 5 <= window && n < 2; ++i) {
        if (b[i] != 0xBE) continue;
        uint32_t imm;
        std::memcpy(&imm, b + i + 1, 4);
        if (InWritableSection(imm)) found[n++] = imm;
    }
    if (n < 2 || found[1] <= found[0]) return 0;
    size_t size = found[1] - found[0];
    if (size < 16 || size > 4096) return 0;
    return size;
}

bool SlowPath(GateResult& r, std::string& why)
{
    const Image& im = Img();
    if (!im.ok || im.nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        im.nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        why = "not a 32-bit x86 PE image";
        return false;
    }

    std::vector<Candidate> cands;
    for (WORD i = 0; i < im.nsections; ++i) {
        const auto& s = im.sections[i];
        if (!(s.Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        uintptr_t lo = im.base + s.VirtualAddress;
        size_t span = SectionSpan(s);
        if (span < addr::kPatternLen || !IsReadableInImage(lo, span)) continue;
        const auto* p = reinterpret_cast<const uint8_t*>(lo);
        for (size_t k = 0; k + addr::kPatternLen <= span; ++k) {
            if (!MatchesShape(p + k)) continue;
            Candidate c;
            c.at = lo + k;
            std::memcpy(&c.slotA, p + k + 2, 4);
            std::memcpy(&c.slotB, p + k + 8, 4);
            std::memcpy(&c.slotC, p + k + 14, 4);
            int32_t rel;
            std::memcpy(&rel, p + k + 19, 4);
            c.callTarget = c.at + 23 + static_cast<uintptr_t>(rel);  // wraps like the CPU
            cands.push_back(c);
            if (cands.size() > 32) break;   // pathological; refused below
        }
    }

    int corroborated = 0;
    Candidate* chosen = nullptr;
    for (auto& c : cands) {
        bool okA = ReadStringAt(c.slotA, 63, c.a) && c.a == "data";
        bool okB = ReadStringAt(c.slotB, 63, c.b) && c.b == "PSOBB";
        bool okC = ReadStringAt(c.slotC, 63, c.c) && c.c == addr::kStockVendor;
        bool okT = InExecutableSection(c.callTarget);
        c.corroborated = okA && okB && okC && okT;
        if (!c.corroborated) {
            c.note = std::string(okA ? "" : " [A]") + (okB ? "" : " [B]") +
                     (okC ? "" : " [C]") + (okT ? "" : " [call target]");
        }
        if (c.corroborated) { ++corroborated; chosen = &c; }
    }

    plog::Line("slow path: %u structural match(es) of the 26-byte pattern",
               static_cast<unsigned>(cands.size()));
    for (const auto& c : cands) {
        plog::Line("  at %s: [A]%s=\"%s\" [B]%s=\"%s\" [C]%s=\"%s\" call %s -> %s%s",
                   Hex(c.at).c_str(),
                   Hex(c.slotA).c_str(), Printable(c.a).c_str(),
                   Hex(c.slotB).c_str(), Printable(c.b).c_str(),
                   Hex(c.slotC).c_str(), Printable(c.c).c_str(),
                   Hex(c.callTarget).c_str(),
                   c.corroborated ? "CORROBORATED" : "not corroborated:",
                   c.note.c_str());
    }

    if (cands.size() > 32) { why = "too many structural matches"; return false; }
    if (corroborated != 1) {
        why = "found " + std::to_string(corroborated) +
              " corroborated match(es); exactly one is required";
        return false;
    }

    uint32_t orig = 0;
    ReadU32(chosen->slotC, orig);

    r.path = "slow";
    r.vendorSlot = chosen->slotC;
    r.originalVendorString = orig;
    r.vendorFound = chosen->c;
    r.productFound = chosen->b;
    r.dataFound = chosen->a;

    size_t buf = RecoverBufferSize(chosen->callTarget);
    if (buf > 0) {
        r.cap = buf - 1;
        if (r.cap < addr::kSafeFallbackCap) r.cap = addr::kSafeFallbackCap;
        if (r.cap > 255) r.cap = 255;
        r.capReason = "destination buffer recovered as " + std::to_string(buf) +
                      " bytes from the copy routine";
    } else {
        r.cap = addr::kSafeFallbackCap;
        r.capReason = "destination buffer size could not be recovered; 9 is the "
                      "length of the name being replaced, so it always fits";
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------

bool IsReadableInImage(uintptr_t p, size_t len)
{
    if (len == 0) return false;
    const IMAGE_SECTION_HEADER* s = SectionOf(p, len);
    if (!s || !(s->Characteristics & IMAGE_SCN_MEM_READ)) return false;

    // Every page in the range must be committed and readable.
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const uintptr_t pageMask = ~static_cast<uintptr_t>(si.dwPageSize - 1);
    uintptr_t page = p & pageMask;
    uintptr_t end = p + len;
    while (page < end) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(reinterpret_cast<LPCVOID>(page), &mbi, sizeof(mbi)) != sizeof(mbi))
            return false;
        if (mbi.State != MEM_COMMIT || !PageReadable(mbi.Protect)) return false;
        uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (regionEnd <= page) return false;
        page = regionEnd;
    }
    return true;
}

GateResult RunGate()
{
    GateResult r;
    r.brand = ReadBrand();

    std::string fastWhy, slowWhy;
    bool ok = FastPath(r, fastWhy);
    if (!ok) {
        plog::Line("fast path did not match: %s", fastWhy.c_str());
        ok = SlowPath(r, slowWhy);
        if (!ok) plog::Line("slow path did not match: %s", slowWhy.c_str());
    }

    if (ok) {
        r.passed = true;
    } else {
        r.passed = false;
        r.path = "none";
        r.refusal = "fast path: " + fastWhy + "; slow path: " + slowWhy;
    }

    // The compatibility report (design 5.3). Written on every launch.
    plog::Line("---- compatibility report ----");
    plog::Line("build identity : %s", r.brand.c_str());
    plog::Line("gate           : %s", r.passed ? "PASSED" : "REFUSED");
    plog::Line("path           : %s", r.path.c_str());
    if (r.passed) {
        plog::Line("vendor slot    : %s (holds %s)", Hex(r.vendorSlot).c_str(),
                   Hex(r.originalVendorString).c_str());
        plog::Line("strings found  : vendor \"%s\", product \"%s\", data \"%s\"",
                   Printable(r.vendorFound).c_str(), Printable(r.productFound).c_str(),
                   Printable(r.dataFound).c_str());
        plog::Line("name cap       : %u characters (%s)", static_cast<unsigned>(r.cap),
                   r.capReason.c_str());
    } else {
        plog::Line("Refusing to act: %s", r.refusal.c_str());
    }
    plog::Line("------------------------------");
    return r;
}
