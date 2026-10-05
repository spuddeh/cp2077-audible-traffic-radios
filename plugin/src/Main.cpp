// ======================================================================================
// Mod Name: Audible Traffic Radios
// Author: Spuddeh
// Description: Lets the radios in traffic cars be heard when nothing else is tuned to their station.
// Mod Version: 0.1.0
// Credits: RED4ext by WopsS.
// ======================================================================================
//
// **A radio station plays only while its predicate says it is not silent.** The predicate (0x2a775c on
// 2.31) returns silent for a station with no counting listener or outside the four-station list. In
// radio mode 1 it also returns silent when every listener is a traffic car (listener kind 2), so a
// station that only traffic is tuned to never plays.
//
// The patch changes the kind that check compares against from 2 to 0xFF, a kind no listener has, so a
// traffic car counts like a world radio and its station plays. The four-station limit is unchanged.
//
// **The function is resolved by RED4ext hash and every byte is verified before any is written.**
// A different game build fails the check and nothing is patched, with a log line saying so.

#include <Windows.h>
#include <RED4ext/RED4ext.hpp>

#include <cstdint>
#include <cstring>
#include <string>

namespace
{
// RadioStation's silent predicate, called from RadioStation::PostSoundUpdate.
constexpr uint32_t kHashSilentPredicate = 2590772874;

// Its first bytes, so a resolved address that is not this function is refused.
constexpr uint8_t kPrologue[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C};

// The mode-1 listener loop and the patch site:
//   +0xc7  48 8B 08              mov rcx, [rax]            the listener
//   +0xca  48 85 C9              test rcx, rcx
//   +0xcd  74 09                 je  +0x09
//   +0xcf  80 B9 2C 01 00 00 02  cmp byte [rcx+0x12c], 2   listener kind == traffic
//   +0xd6  74 11                 je  +0x11                 next listener
//   +0xd8  40 8A C7              mov al, dil               not silent
constexpr size_t kSite = 0xc7;
constexpr uint8_t kExpected[] = {0x48, 0x8B, 0x08, 0x48, 0x85, 0xC9, 0x74, 0x09, 0x80, 0xB9,
                                 0x2C, 0x01, 0x00, 0x00, 0x02, 0x74, 0x11, 0x40, 0x8A, 0xC7};
constexpr size_t kPatchAt = 0xd5;
constexpr uint8_t kPatched[] = {0xFF};  // cmp byte [rcx+0x12c], 0xFF

const RED4ext::v1::Sdk* g_sdk = nullptr;
RED4ext::v1::PluginHandle g_handle = nullptr;

void Log(const std::string& aText)
{
    if (g_sdk && g_sdk->logger)
    {
        g_sdk->logger->Info(g_handle, aText.c_str());
    }
}

uintptr_t ResolveByHash(uint32_t aHash)
{
    using ResolveFn = uintptr_t (*)(uint32_t);
    const HMODULE red4ext = GetModuleHandleW(L"RED4ext.dll");
    const auto resolve = red4ext ? reinterpret_cast<ResolveFn>(GetProcAddress(red4ext, "RED4ext_ResolveAddress"))
                                 : nullptr;
    return resolve ? resolve(aHash) : 0;
}

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

void Patch()
{
    const auto fn = reinterpret_cast<uint8_t*>(ResolveByHash(kHashSilentPredicate));
    if (!fn)
    {
        Log("the radio station's silent predicate did not resolve - nothing patched");
        return;
    }
    if (std::memcmp(fn, kPrologue, sizeof(kPrologue)) != 0)
    {
        Log("the resolved function does not start as expected - not this game build, nothing patched");
        return;
    }
    if (std::memcmp(fn + kSite, kExpected, sizeof(kExpected)) == 0)
    {
        if (!WriteBytes(fn + kPatchAt, kPatched, sizeof(kPatched)))
        {
            Log("the patch site could not be made writable - nothing patched");
            return;
        }
        Log("patched: a station that only traffic cars are tuned to now plays");
        return;
    }
    uint8_t already[sizeof(kExpected)];
    std::memcpy(already, kExpected, sizeof(already));
    std::memcpy(already + (kPatchAt - kSite), kPatched, sizeof(kPatched));
    if (std::memcmp(fn + kSite, already, sizeof(already)) == 0)
    {
        Log("the patch is already in place - another copy of this plugin is loaded");
        return;
    }
    Log("the patch site does not hold the expected bytes - not this game build, nothing patched");
}
} // namespace

RED4EXT_C_EXPORT void RED4EXT_CALL Query(RED4ext::v1::PluginInfo* aInfo)
{
    aInfo->name = L"AudibleTrafficRadios";
    aInfo->author = L"Spuddeh";
    aInfo->version = RED4EXT_V1_SEMVER(0, 1, 0);
    aInfo->runtime = RED4EXT_V1_RUNTIME_VERSION_INDEPENDENT;
    aInfo->sdk = RED4EXT_V1_SDK_VERSION_CURRENT;
}

RED4EXT_C_EXPORT uint32_t RED4EXT_CALL Supports()
{
    return RED4EXT_API_VERSION_1;
}

RED4EXT_C_EXPORT bool RED4EXT_CALL Main(RED4ext::v1::PluginHandle aHandle,
                                        RED4ext::v1::EMainReason aReason, const RED4ext::v1::Sdk* aSdk)
{
    if (aReason == RED4ext::v1::EMainReason::Load)
    {
        g_sdk = aSdk;
        g_handle = aHandle;
        Patch();
    }
    return true;
}
