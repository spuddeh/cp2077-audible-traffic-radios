// ======================================================================================
// Mod Name: Audible Traffic Radios
// Author: Spuddeh
// Description: Lets the radios in traffic cars be heard, through the game's own car radio sound.
// Mod Version: 0.1.0
// Credits: RED4ext by WopsS.
// ======================================================================================
//
// Two changes to the game's own radio, nothing else:
//   1. A station that only traffic cars are tuned to plays. One byte in the station's silent predicate.
//   2. The NPC car radio mixer (Wwise actor mixer 398448775) plays at kMixerVolume instead of -8 dB, set on the
//      loaded object through Wwise's own property setter. The receivers, their EQ, distance falloff and bus
//      stay the game's.
//
// **Every address is verified before it is used, and a value is written only over the one expected.** On any
// other game build the plugin logs a line and changes nothing.

#include <Windows.h>
#include <RED4ext/RED4ext.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <string>

namespace
{
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

// An address in the game's image, or nullptr when its first bytes are not the expected ones.
template <typename T>
T AtRva(uintptr_t aRva, std::initializer_list<uint8_t> aPrologue)
{
    const auto address = reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + aRva);
    size_t i = 0;
    for (const auto b : aPrologue)
    {
        if (address[i++] != b)
        {
            return nullptr;
        }
    }
    return reinterpret_cast<T>(address);
}

template <typename T>
T Read(uintptr_t aAddress)
{
    return *reinterpret_cast<T*>(aAddress);
}

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

// --- 2. the NPC car radio mixer's volume ---
// g_pIndex (0x339f7b8): audio nodes are its first table, buckets at +0x40, count at +0x48; a node sits in bucket
// id % count, chained through +0x8, id at +0x10. That pointer is 0x10 into the object, whose +0x08 holds the
// vtable every parameter node shares (0x2ee0798) and whose property bundle is at +0x88 (a count byte, the prop
// ids, the values from the next 4-byte boundary). CAkParameterNode::SetAkProp (0x1b07990) takes the object
// start and runs under Wwise's global lock (CAkFunctionCritical, 0x1af6d90 / 0x1af7100).
constexpr uint32_t kNpcRadioMixer = 398448775;
constexpr uint8_t kPropVolume = 0;
constexpr float kVanillaVolume = -8.0f;
constexpr float kMixerVolume = 42.0f;

using AkSetPropFn = void (*)(void* aObject, int aProp, float aValue, float aMin, float aMax);
using AkCriticalFn = void (*)(void* aSelf);

uintptr_t SafeFindObject(uint32_t aId)
{
    static const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    __try
    {
        const auto index = Read<uintptr_t>(base + 0x339f7b8);
        const auto buckets = index ? Read<uintptr_t>(index + 0x40) : 0;
        const auto count = index ? Read<uint32_t>(index + 0x48) : 0;
        if (!buckets || !count)
        {
            return 0;
        }
        for (auto node = Read<uintptr_t>(buckets + (aId % count) * 8); node; node = Read<uintptr_t>(node + 0x8))
        {
            if (Read<uint32_t>(node + 0x10) == aId)
            {
                const uintptr_t object = node - 0x10;
                return Read<uintptr_t>(object + 0x08) == base + 0x2ee0798 ? object : 0;
            }
        }
        return 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

float SafeProp(uintptr_t aObject, uint8_t aProp)
{
    __try
    {
        const auto bundle = Read<uintptr_t>(aObject + 0x88);
        const uint8_t count = bundle ? Read<uint8_t>(bundle) : 0;
        for (uint8_t i = 0; i < count; ++i)
        {
            if (Read<uint8_t>(bundle + 1 + i) == aProp)
            {
                return Read<float>(bundle + ((count + 4u) & ~3u) + i * 4);
            }
        }
        return std::numeric_limits<float>::quiet_NaN();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return std::numeric_limits<float>::quiet_NaN();
    }
}

enum class Mixer
{
    NotLoaded,
    Set,
    Unexpected,
};

// Sets the mixer's volume when it holds the vanilla value; reports what it found.
Mixer SetMixerVolume(float* aFound, uintptr_t* aObject)
{
    static const auto setProp = AtRva<AkSetPropFn>(0x1b07990, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83});
    static const auto lockOn = AtRva<AkCriticalFn>(0x1af6d90, {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B});
    static const auto lockOff = AtRva<AkCriticalFn>(0x1af7100, {0x48, 0x83, 0xEC, 0x28, 0xFF, 0x15});
    if (!setProp || !lockOn || !lockOff)
    {
        *aFound = std::numeric_limits<float>::quiet_NaN();
        *aObject = 0;
        return Mixer::Unexpected;
    }
    alignas(16) uint8_t critical[16] = {};
    lockOn(critical);
    const uintptr_t object = SafeFindObject(kNpcRadioMixer);
    *aObject = object;
    const float before = object ? SafeProp(object, kPropVolume) : std::numeric_limits<float>::quiet_NaN();
    Mixer result = Mixer::NotLoaded;
    if (object && std::fabs(before - kVanillaVolume) < 0.001f)
    {
        setProp(reinterpret_cast<void*>(object), kPropVolume, kMixerVolume, 0.0f, 0.0f);
        result = Mixer::Set;
    }
    else if (object)
    {
        result = std::fabs(before - kMixerVolume) < 0.001f ? Mixer::Set : Mixer::Unexpected;
    }
    *aFound = object ? SafeProp(object, kPropVolume) : before;
    lockOff(critical);
    return result;
}

// Once a second until the mixer is set, then every ten seconds, so a reloaded radio bank is set again.
bool OnUpdate(RED4ext::CGameApplication*)
{
    static uint64_t next = 0;
    static Mixer last = Mixer::NotLoaded;
    static uintptr_t lastObject = 0;
    const uint64_t now = GetTickCount64();
    if (now < next || last == Mixer::Unexpected)
    {
        return false;
    }
    float found = 0.0f;
    uintptr_t object = 0;
    const Mixer result = SetMixerVolume(&found, &object);
    // Which mixer object the bank gave: a new address means the radio bank was loaded again.
    if (object != lastObject)
    {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "the NPC car radio mixer object is at %llx (was %llx)",
                      static_cast<unsigned long long>(object), static_cast<unsigned long long>(lastObject));
        Log(buf);
        lastObject = object;
    }
    if (result != last || (result == Mixer::Set && std::fabs(found - kMixerVolume) >= 0.001f))
    {
        if (result == Mixer::Set)
        {
            Log("the NPC car radio mixer plays at " + std::to_string(found) + " dB");
        }
        else if (result == Mixer::Unexpected)
        {
            Log("the NPC car radio mixer holds " + std::to_string(found) +
                " dB, not the game's -8 - left as it is");
        }
    }
    last = result;
    next = now + (result == Mixer::Set ? 10000 : 1000);
    return false;
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
        PatchPredicate();
        static RED4ext::v1::GameState state{
            .OnEnter = nullptr,
            .OnUpdate = OnUpdate,
            .OnExit = nullptr,
        };
        aSdk->gameStates->Add(aHandle, RED4ext::EGameStateType::Running, &state);
    }
    return true;
}
