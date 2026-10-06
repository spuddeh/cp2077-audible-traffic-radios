// The two radio-gate patches in the station's silent predicate.

#include "Atr.hpp"

namespace atr
{
// --- 1. traffic-only stations play ---
// RadioStation's silent predicate (0x2a775c on 2.31) holds a station silent in radio mode 1 when every listener
// is a traffic car (listener kind 2). Comparing the kind against 0xFF, which no listener has, lets a traffic
// car count like any other radio.
constexpr uint32_t kHashSilentPredicate = 2590772874;
constexpr uint8_t kPredicatePrologue[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C};
//   +0xc7  48 8B 08              mov rcx, [rax]            the listener
//   +0xcf  80 B9 2C 01 00 00 02  cmp byte [rcx+0x12c], 2   listener kind == traffic
constexpr size_t kSite = 0xc7;
constexpr uint8_t kExpected[] = {0x48, 0x8B, 0x08, 0x48, 0x85, 0xC9, 0x74, 0x09, 0x80, 0xB9,
                                 0x2C, 0x01, 0x00, 0x00, 0x02, 0x74, 0x11, 0x40, 0x8A, 0xC7};
constexpr size_t kPatchAt = 0xd5;
constexpr uint8_t kPatched[] = {0xFF};

bool WriteBytes(void* aAt, const void* aData, size_t aLen)
{
    DWORD old = 0;
    if (!VirtualProtect(aAt, aLen, PAGE_EXECUTE_READWRITE, &old))
    {
        return false;
    }
    std::memcpy(aAt, aData, aLen);
    VirtualProtect(aAt, aLen, old, &old);
    FlushInstructionCache(GetCurrentProcess(), aAt, aLen);
    return true;
}

void PatchPredicate()
{
    const auto fn = reinterpret_cast<uint8_t*>(ResolveByHash(kHashSilentPredicate));
    if (!fn || std::memcmp(fn, kPredicatePrologue, sizeof(kPredicatePrologue)) != 0)
    {
        Log("the radio station's silent predicate is not this game build's - nothing patched");
        return;
    }
    if (std::memcmp(fn + kSite, kExpected, sizeof(kExpected)) == 0)
    {
        Log(WriteBytes(fn + kPatchAt, kPatched, sizeof(kPatched))
                ? "patched: a station that only traffic cars are tuned to now plays"
                : "the patch site could not be made writable - nothing patched");
        return;
    }
    uint8_t already[sizeof(kExpected)];
    std::memcpy(already, kExpected, sizeof(already));
    std::memcpy(already + (kPatchAt - kSite), kPatched, sizeof(kPatched));
    Log(std::memcmp(fn + kSite, already, sizeof(already)) == 0
            ? "the patch is already in place - another copy of this plugin is loaded"
            : "the patch site does not hold the expected bytes - not this game build, nothing patched");
}

// --- 2. other radios play while the player's car radio is on ---
// With the player in a car with its radio on, the radio mode is 2, and the same predicate holds silent every
// station no player receiver is tuned to: traffic, world radios and street music. Its mode 2 branch is a cold
// block with no hash, reached through the predicate's own jne at +0xab:
//   +0x00  83 F9 01           cmp ecx, 1
//   +0x03  0F 85 rel32        jne                      (not mode 2)
//   +0x09  40 38 BA 1E 02 00 00  cmp [rdx+0x21e], dil  station has a player receiver
//   +0x10  0F 94 C0           sete al                  1 = silent
// Replacing the sete with xor al, al plays the station as mode 0 does.
constexpr size_t kModeJne = 0xab;  // 0F 85 rel32
constexpr uint8_t kModeCold[] = {0x83, 0xF9, 0x01, 0x0F, 0x85};
constexpr uint8_t kModeColdCmp[] = {0x40, 0x38, 0xBA, 0x1E, 0x02, 0x00, 0x00, 0x0F, 0x94, 0xC0, 0xE9};
constexpr size_t kModeColdCmpAt = 0x09;
constexpr size_t kModeSeteAt = 0x10;
constexpr uint8_t kModeSete[] = {0x0F, 0x94, 0xC0};
constexpr uint8_t kModePlays[] = {0x32, 0xC0, 0x90};  // xor al, al; nop

void PatchCarRadioRule()
{
    const auto fn = reinterpret_cast<uint8_t*>(ResolveByHash(kHashSilentPredicate));
    if (!fn || std::memcmp(fn, kPredicatePrologue, sizeof(kPredicatePrologue)) != 0 || fn[kModeJne] != 0x0F ||
        fn[kModeJne + 1] != 0x85)
    {
        Log("the radio station's car radio rule is not this game build's - nothing patched");
        return;
    }
    int32_t rel = 0;
    std::memcpy(&rel, fn + kModeJne + 2, sizeof(rel));
    const auto cold = fn + kModeJne + 6 + rel;
    if (std::memcmp(cold, kModeCold, sizeof(kModeCold)) != 0 ||
        std::memcmp(cold + kModeColdCmpAt, kModeColdCmp, sizeof(kModeColdCmp)) != 0)
    {
        Log(std::memcmp(cold + kModeSeteAt, kModePlays, sizeof(kModePlays)) == 0
                ? "the car radio rule is already patched - another copy of this plugin is loaded"
                : "the car radio rule does not hold the expected bytes - not this game build, nothing patched");
        return;
    }
    Log(WriteBytes(cold + kModeSeteAt, kModePlays, sizeof(kModePlays))
            ? "patched: other radios play while the player's car radio is on"
            : "the car radio rule could not be made writable - nothing patched");
}
} // namespace atr
