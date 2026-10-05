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
#include <initializer_list>
#include <string>
#include <unordered_set>
#include <vector>

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

// --- level boost (experiment) ---
// Every 250 ms, walk each station's listeners; for every traffic emitter (kind 2) whose receiver sound has a live
// Wwise voice, set `veh_engage_moving_faster` to 1 on the voice's game object (the NPC mixer's 0 dB end, which
// traffic never sets) and scale its output to each listener by kOutputGain.
constexpr uint32_t kHashEngineRoot = 2549221846;
constexpr size_t kRootAudioSystem = 0xa8;
constexpr size_t kAudioRadioManager = 0xe0;
constexpr size_t kManagerStations = 0x0;
constexpr size_t kManagerCount = 0xc;
constexpr size_t kStationListeners = 0x100;
constexpr size_t kStationListenerCount = 0x10c;
constexpr size_t kListenerKind = 0x12c;
constexpr uint8_t kKindTraffic = 2;
constexpr size_t kEmitterSounds = 0x58;
constexpr size_t kEmitterSoundCount = 0x64;
constexpr size_t kEmitterMetadata = 0x140;
constexpr size_t kMetadataReceiverEvent = 0x80;
constexpr size_t kSoundName = 0x8;
constexpr size_t kSoundPlayingId = 0x44;
constexpr size_t kSoundState = 0x59;

constexpr uint32_t kRtpcEngageMovingFaster = 139023859;  // veh_engage_moving_faster
constexpr float kOutputGain = 16.0f;                     // Wwise's ceiling, +24 dB
constexpr bool kBoost = false;

// --- receiver swap (experiment) ---
// TrafficVehicleEmitter::PlayRadio (0x9d8684) hands the emitter its receiver event from the sound set's vehicle
// audio data (+0x140 -> +0x80) each time a car's radio starts. Rewriting that name in every sound set seen makes
// later cars of that set receive through kSwapReceiver instead of their radio_car_*_npc event.
constexpr uintptr_t kRvaTrafficEmitterVtbl = 0x2b458a0;
constexpr const char* kSwapReceiver = "radio_default_int";  // the world radio receiver, heard in game
std::unordered_set<uintptr_t> g_swappedSets;
constexpr int kCurveLinear = 4;                          // AkCurveInterpolation_Linear

using AkGameObjectFromPlayingIdFn = uint64_t (*)(uint32_t);
using AkGetListenersFn = int (*)(uint64_t, uint64_t*, uint32_t*);
using AkSetRtpcValueFn = int (*)(uint32_t, float, uint64_t, int, int, bool);
using AkSetOutputBusVolumeFn = int (*)(uint64_t, uint64_t, float);

struct Ak
{
    AkGameObjectFromPlayingIdFn gameObject = nullptr;
    AkGetListenersFn listeners = nullptr;
    AkSetRtpcValueFn setRtpc = nullptr;
    AkSetOutputBusVolumeFn setOutputBusVolume = nullptr;
};
Ak g_ak;
uintptr_t g_rootSlot = 0;
uint64_t g_lastBoost = 0;
uint64_t g_boosted = 0;
std::vector<uint64_t> g_swapLog;  // receiver names replaced this pass, logged outside the SEH block

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

bool ResolveBoost()
{
    g_rootSlot = ResolveByHash(kHashEngineRoot);
    g_ak.gameObject = AtRva<AkGameObjectFromPlayingIdFn>(0x1ad2680, {0x8B, 0xD1, 0x48, 0x8B, 0x0D});
    g_ak.listeners = AtRva<AkGetListenersFn>(0x1ad27c0, {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C});
    g_ak.setRtpc = AtRva<AkSetRtpcValueFn>(0x1acf570, {0x48, 0x83, 0xEC, 0x48, 0x0F, 0xB6, 0x44, 0x24});
    g_ak.setOutputBusVolume = AtRva<AkSetOutputBusVolumeFn>(0x1ace9c0, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83});
    return g_rootSlot && g_ak.gameObject && g_ak.listeners && g_ak.setRtpc && g_ak.setOutputBusVolume;
}

template <typename T>
T Read(uintptr_t aAddress)
{
    return *reinterpret_cast<T*>(aAddress);
}

// The receiver voice's playing id on a traffic emitter, or 0.
uint32_t ReceiverVoice(uintptr_t aEmitter)
{
    const auto metadata = Read<uintptr_t>(aEmitter + kEmitterMetadata);
    const auto receiver = metadata ? Read<uint64_t>(metadata + kMetadataReceiverEvent) : 0;
    const auto sounds = Read<uintptr_t>(aEmitter + kEmitterSounds);
    const auto count = Read<uint32_t>(aEmitter + kEmitterSoundCount);
    for (uint32_t i = 0; receiver && sounds && i < count && i < 16; ++i)
    {
        const auto element = Read<uintptr_t>(sounds + i * 8);
        const auto entry = element ? Read<uintptr_t>(element) : 0;
        if (!entry || Read<uint64_t>(entry + kSoundName) != receiver)
        {
            continue;
        }
        const auto state = Read<uint8_t>(entry + kSoundState);
        const auto id = Read<uint32_t>(entry + kSoundPlayingId);
        return (id && state != 4 && state != 5) ? id : 0;
    }
    return 0;
}

void Boost(uintptr_t aEmitter)
{
    const uint32_t pid = ReceiverVoice(aEmitter);
    if (!pid)
    {
        return;
    }
    const uint64_t go = g_ak.gameObject(pid);
    if (go == ~0ull)
    {
        return;
    }
    g_ak.setRtpc(kRtpcEngageMovingFaster, 1.0f, go, 0, kCurveLinear, false);
    uint64_t listeners[4] = {};
    uint32_t count = 4;
    g_ak.listeners(go, listeners, &count);
    for (uint32_t i = 0; i < count && i < 4; ++i)
    {
        g_ak.setOutputBusVolume(go, listeners[i], kOutputGain);
    }
    ++g_boosted;
}

// Only a TrafficVehicleEmitter has vehicle audio data at +0x140: the vtable is checked before the read, because
// the game's own handler ends the process on a bad read before SEH runs.
uintptr_t SwapTarget(uintptr_t aEmitter, uint64_t aSwap)
{
    static const uintptr_t trafficVtbl = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + kRvaTrafficEmitterVtbl;
    if (Read<uintptr_t>(aEmitter) != trafficVtbl)
    {
        return 0;
    }
    const auto metadata = Read<uintptr_t>(aEmitter + kEmitterMetadata);
    if (!metadata || Read<uint64_t>(metadata + kMetadataReceiverEvent) == aSwap)
    {
        return 0;
    }
    return metadata;
}

bool SafeBoostAll()
{
    __try
    {
        const auto root = Read<uintptr_t>(g_rootSlot);
        const auto audio = root ? Read<uintptr_t>(root + kRootAudioSystem) : 0;
        const auto manager = audio ? Read<uintptr_t>(audio + kAudioRadioManager) : 0;
        if (!manager)
        {
            return true;
        }
        const auto stations = Read<uintptr_t>(manager + kManagerStations);
        const auto stationCount = Read<uint32_t>(manager + kManagerCount);
        for (uint32_t s = 0; stations && s < stationCount && s < 64; ++s)
        {
            const auto station = Read<uintptr_t>(stations + s * 8);
            const auto listeners = station ? Read<uintptr_t>(station + kStationListeners) : 0;
            const auto listenerCount = station ? Read<uint32_t>(station + kStationListenerCount) : 0;
            for (uint32_t l = 0; listeners && l < listenerCount && l < 512; ++l)
            {
                const auto emitter = Read<uintptr_t>(listeners + l * 8);
                if (emitter && Read<uint8_t>(emitter + kListenerKind) == kKindTraffic)
                {
                    static const uint64_t swap = RED4ext::CName(kSwapReceiver).hash;
                    if (const auto metadata = SwapTarget(emitter, swap))
                    {
                        g_swapLog.push_back(Read<uint64_t>(metadata + kMetadataReceiverEvent));
                        *reinterpret_cast<uint64_t*>(metadata + kMetadataReceiverEvent) = swap;
                        g_swappedSets.insert(metadata);
                    }
                    if (kBoost)
                    {
                        Boost(emitter);
                    }
                }
            }
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool OnUpdate(RED4ext::CGameApplication*)
{
    const uint64_t now = GetTickCount64();
    if (now - g_lastBoost < 250)
    {
        return false;
    }
    g_lastBoost = now;
    const uint64_t before = g_boosted;
    g_swapLog.clear();
    if (!SafeBoostAll())
    {
        Log("boost: the station walk faulted - skipped this pass");
    }
    for (const auto old : g_swapLog)
    {
        const char* text = RED4ext::CName(old).ToString();
        Log(std::string("swap: a traffic sound set now receives through ") + kSwapReceiver + " instead of " +
            (text && *text ? text : "?") + " (" + std::to_string(g_swappedSets.size()) + " sets)");
    }
    static uint64_t lastReport = 0;
    if (now - lastReport >= 10000 && g_boosted != before)
    {
        lastReport = now;
        Log("boost: " + std::to_string(g_boosted) + " voice updates so far");
    }
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
        Patch();
        if (ResolveBoost())
        {
            static RED4ext::v1::GameState state{
                .OnEnter = nullptr,
                .OnUpdate = OnUpdate,
                .OnExit = nullptr,
            };
            aSdk->gameStates->Add(aHandle, RED4ext::EGameStateType::Running, &state);
            Log(std::string("swap: traffic receivers move to ") + kSwapReceiver + (kBoost ? ", boost on" : ", boost off"));
        }
        else
        {
            Log("boost: a Wwise function or the engine root did not match this build - no boost");
        }
    }
    return true;
}
