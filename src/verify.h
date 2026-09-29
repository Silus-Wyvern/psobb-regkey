// verify.h - the gate. Fail closed: if the running image is not positively
// recognised, the plugin does nothing at all.
//
// Two paths, tried in order:
//   fast - known builds: fixed addresses, five checks (see verify.cpp).
//   slow - unfamiliar builds: find the name-initialiser call by its byte
//          shape, then require the three pointers it pushes to resolve to
//          "data", "PSOBB" and "SONICTEAM". Exactly ONE such corroborated
//          match is required; zero or more than one means refuse.

#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

struct GateResult {
    bool        passed = false;
    std::string path;            // "fast", "slow", or "none"
    std::string refusal;         // why it refused, when !passed

    uintptr_t   vendorSlot = 0;  // address of the vendor pointer global
    uintptr_t   originalVendorString = 0; // what the slot held when checked

    std::string vendorFound;     // the strings the pointers resolved to
    std::string productFound;
    std::string dataFound;

    size_t      cap = 0;         // max characters for a replacement name
    std::string capReason;

    std::string brand;           // build identity string, log only
};

// Runs the full gate against the process's main image and writes the
// compatibility report to the log. Reads memory only; writes nothing.
GateResult RunGate();

// True if [p, p+len) lies inside a readable section of the main image and
// is committed, readable memory. Used before every dereference.
bool IsReadableInImage(uintptr_t p, size_t len);
