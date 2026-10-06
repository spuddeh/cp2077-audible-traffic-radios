// ======================================================================================
// Mod Name: Audible Traffic Radios
// Author: Spuddeh
// Description: Lets the radios in traffic cars be heard, through the game's own car radio sound.
// Mod Version: 0.1.0
// Credits: RED4ext by WopsS.
// ======================================================================================
//
// Three changes to the game's own radio, nothing else:
//   1. A station that only traffic cars are tuned to plays. One byte in the station's silent predicate.
//   2. Other radios keep playing while the player's car radio is on. Three bytes in the same predicate.
//   3. The NPC car radio mixer (Wwise actor mixer 398448775) plays at the top of a level range instead of -8 dB,
//      set on the loaded object through Wwise's own property setter, and each car's radio sits somewhere in that
//      range. The receivers, their EQ, distance falloff and bus stay the game's.
//   4. From inside a car in first person, the same mixer is turned down and low-passed, as the game already does
//      for traffic engines but not for radios.
//   5. A traffic car with a door open or torn off, a window down, or no side windows plays its radio without the
//      receiver's EQ, for that car only.
//   6. Walls muffle traffic radios: the NPC mixer gets the world radio's two occlusion curves.
//
// **Every address is verified before it is used, and a value is written only over the one expected.** On any
// other game build the plugin logs a line and changes nothing.

#include <Windows.h>
#include <RED4ext/RED4ext.hpp>
#include <RED4ext/CString.hpp>
#include <RED4ext/GameEngine.hpp>
#include <RED4ext/RTTISystem.hpp>
#include <RED4ext/Scripting/IScriptable.hpp>
#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>
#include <RED4ext/Scripting/Natives/entEntityID.hpp>
#include <RED4ext/Scripting/Utils.hpp>
#include <RED4ext/TweakDB.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <map>
#include <set>
#include <utility>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

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

// --- 3. the NPC car radio mixer's volume ---
// g_pIndex (0x339f7b8): audio nodes are its first table, buckets at +0x40, count at +0x48; a node sits in bucket
// id % count, chained through +0x8, id at +0x10. That pointer is 0x10 into the object, whose +0x08 holds the
// vtable every parameter node shares (0x2ee0798) and whose property bundle is at +0x88 (a count byte, the prop
// ids, the values from the next 4-byte boundary). CAkParameterNode::SetAkProp (0x1b07990) takes the object
// start and runs under Wwise's global lock (CAkFunctionCritical, 0x1af6d90 / 0x1af7100).
constexpr uint32_t kNpcRadioMixer = 398448775;
constexpr uint8_t kPropVolume = 0;
constexpr uint8_t kPropLpf = 2;  // the actor-mixer property bundle's low-pass id: radio.bnk stores 10 to 64 under it
constexpr float kVanillaVolume = -8.0f;

// The range a car's radio plays in, in dB on the mixer. The mixer is set to the top; each voice is lowered from
// there by its own share of the range.
float g_levelTop = 12.0f;
float g_levelBottom = 0.0f;

// From inside a car: how far the mixer drops (dB) and how much low-pass it gets (0 to 100), at full veh_interior.
// The game's own values for traffic engines, tyres and horns (init.bnk, veh_interior at 1).
float g_muffleDb = -4.0f;
float g_muffleLpf = 25.0f;
// The mixer volume this plugin last wrote, which a later write must find there.
float g_mixerNow = std::numeric_limits<float>::quiet_NaN();

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

// Sets the mixer's volume to aTarget and its low-pass to aLpf when its volume holds the vanilla value or
// aPrevious; reports the volume it found.
Mixer SetMixerVolume(float aTarget, float aPrevious, float aLpf, float* aFound)
{
    static const auto setProp = AtRva<AkSetPropFn>(0x1b07990, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83});
    static const auto lockOn = AtRva<AkCriticalFn>(0x1af6d90, {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B});
    static const auto lockOff = AtRva<AkCriticalFn>(0x1af7100, {0x48, 0x83, 0xEC, 0x28, 0xFF, 0x15});
    if (!setProp || !lockOn || !lockOff)
    {
        *aFound = std::numeric_limits<float>::quiet_NaN();
        return Mixer::Unexpected;
    }
    alignas(16) uint8_t critical[16] = {};
    lockOn(critical);
    const uintptr_t object = SafeFindObject(kNpcRadioMixer);
    const float before = object ? SafeProp(object, kPropVolume) : std::numeric_limits<float>::quiet_NaN();
    Mixer result = Mixer::NotLoaded;
    if (object && (std::fabs(before - kVanillaVolume) < 0.001f || std::fabs(before - aPrevious) < 0.001f))
    {
        setProp(reinterpret_cast<void*>(object), kPropVolume, aTarget, 0.0f, 0.0f);
        setProp(reinterpret_cast<void*>(object), kPropLpf, aLpf, 0.0f, 0.0f);
        result = Mixer::Set;
    }
    else if (object)
    {
        result = std::fabs(before - aTarget) < 0.001f ? Mixer::Set : Mixer::Unexpected;
    }
    *aFound = object ? SafeProp(object, kPropVolume) : before;
    lockOff(critical);
    return result;
}

// --- 4. a level per car ---
// Each NPC car radio voice gets its own value of veh_engage_moving_faster (139023859), the mixer's vanilla level
// control: -12 dB at 0 to 0 dB at 1, an S-curve, added to the mixer. It is set per playing id through
// AK::SoundEngine::SetRTPCValueByPlayingID (0x1acf6a0), so the car's engine sounds on the same game object keep
// theirs. A voice is found the way the station update finds it: every listener of every station (engine root
// -> audio system +0xa8 -> radio manager +0xe0 -> stations), its broadcast event (+0x120) matched by name in its
// sounds (+0x58, count +0x64; name +0x8, playing id +0x44, state +0x59). Traffic (kind 2) and cars that left
// traffic (kind 3) both play through the NPC receivers.
//
// A car's radio voice restarts often while it is in earshot (slot changes, the 35 m edge), so the level is keyed
// on the car: a TrafficVehicleEmitter (vtable 0x2b458a0) keeps its car's entity id at +0x138. A kind 3 emitter has
// no such field and falls back to its playing id.
constexpr uint32_t kHashEngineRoot = 2549221846;
constexpr uint32_t kRtpcEngageMovingFaster = 139023859;
constexpr uintptr_t kRvaTrafficEmitterVtbl = 0x2b458a0;
constexpr float kCurveBottomDb = -12.0f;
constexpr int kCurveLinear = 4;
constexpr uint64_t kNpcReceivers[] = {
    RED4ext::FNV1a64("radio_car_lowend_npc"), RED4ext::FNV1a64("radio_car_muscle_npc"),
    RED4ext::FNV1a64("radio_car_sports_npc"), RED4ext::FNV1a64("radio_car_suv_npc"),
    RED4ext::FNV1a64("radio_car_truck_npc"),  RED4ext::FNV1a64("radio_car_hyper_npc"),
    RED4ext::FNV1a64("radio_car_police_npc"),
};

using AkSetRtpcByPlayingIdFn = int (*)(uint32_t aRtpc, float aValue, uint32_t aPlayingId, int32_t aMs, int aCurve,
                                       bool aBypass);

// Live NPC radio voices by playing id, with the round they were last seen in.
std::unordered_map<uint32_t, uint32_t> g_levelled;

// What the open-air cars need of each live voice: its car, receiver and Wwise game object (found later, under
// Wwise's lock).
struct LiveVoice
{
    uint64_t key;
    uint8_t receiver;
    bool car;  // the key is the car's entity id
    uint64_t gameObject = ~0ull;
};
std::unordered_map<uint32_t, LiveVoice> g_voices;
uint32_t g_round = 0;

// The receiver's index in kNpcReceivers, or -1.
int NpcReceiverIndex(uint64_t aName)
{
    for (int i = 0; i < static_cast<int>(std::size(kNpcReceivers)); ++i)
    {
        if (kNpcReceivers[i] == aName)
        {
            return i;
        }
    }
    return -1;
}

struct Voice
{
    uint32_t playingId;
    uint64_t key;      // the car's entity id, or the playing id
    uint8_t receiver;  // index in kNpcReceivers
    bool car;          // the key is the car's entity id
};

bool SafeCollectVoices(uintptr_t aRootSlot, uintptr_t aTrafficVtbl, Voice* aOut, uint32_t aMax, uint32_t* aCount)
{
    *aCount = 0;
    __try
    {
        const auto root = Read<uintptr_t>(aRootSlot);
        const auto audio = root ? Read<uintptr_t>(root + 0xa8) : 0;
        const auto manager = audio ? Read<uintptr_t>(audio + 0xe0) : 0;
        const auto stations = manager ? Read<uintptr_t>(manager) : 0;
        const auto stationCount = manager ? Read<uint32_t>(manager + 0xc) : 0;
        for (uint32_t i = 0; stations && i < stationCount && i < 128; ++i)
        {
            const auto station = Read<uintptr_t>(stations + i * 8);
            const auto listeners = station ? Read<uintptr_t>(station + 0x100) : 0;
            const auto listenerCount = station ? Read<uint32_t>(station + 0x10c) : 0;
            for (uint32_t l = 0; listeners && l < listenerCount && l < 64; ++l)
            {
                const auto emitter = Read<uintptr_t>(listeners + l * 8);
                const uint8_t kind = emitter ? Read<uint8_t>(emitter + 0x12c) : 0;
                if (kind != 2 && kind != 3)
                {
                    continue;
                }
                const auto broadcast = Read<uint64_t>(emitter + 0x120);
                const int receiver = NpcReceiverIndex(broadcast);
                if (receiver < 0)
                {
                    continue;
                }
                const auto sounds = Read<uintptr_t>(emitter + 0x58);
                const auto soundCount = Read<uint32_t>(emitter + 0x64);
                for (uint32_t k = 0; sounds && k < soundCount && k < 64; ++k)
                {
                    const auto element = Read<uintptr_t>(sounds + k * 8);
                    const auto sound = element ? Read<uintptr_t>(element) : 0;
                    if (sound && Read<uint64_t>(sound + 0x8) == broadcast)
                    {
                        const auto id = Read<uint32_t>(sound + 0x44);
                        if (id && Read<uint8_t>(sound + 0x59) == 3 && *aCount < aMax)
                        {
                            const uint64_t car =
                                Read<uintptr_t>(emitter) == aTrafficVtbl ? Read<uint64_t>(emitter + 0x138) : 0;
                            aOut[(*aCount)++] = Voice{id, car ? car : id, static_cast<uint8_t>(receiver), car != 0};
                        }
                        break;
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

// One of three independent hashes of the key, as 0 to 1.
float Hash01(uint64_t aKey, uint32_t aSeed)
{
    uint64_t h = (aKey + aSeed) * 0x9E3779B97F4A7C15ull;
    h ^= h >> 31;
    h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 29;
    return static_cast<float>(h >> 40) / 16777215.0f;
}

// The car's share of the range: the mean of three hashes, so most cars sit near the middle and few at either
// end. The same key always gets the same share.
float ShareOf(uint64_t aKey)
{
    return (Hash01(aKey, 1) + Hash01(aKey, 2) + Hash01(aKey, 3)) / 3.0f;
}

// The curve value that lowers the mixer by aDb (0 or less). The curve's points are amplitude - 1 (-0.7488 is
// -12 dB) with an S-curve between them, taken here as 3t^2 - 2t^3; the meter is the check on that.
float CurveValueFor(float aDb)
{
    const float bottom = std::pow(10.0f, kCurveBottomDb / 20.0f) - 1.0f;
    const float y = std::pow(10.0f, aDb / 20.0f) - 1.0f;
    const float t = std::fmin(1.0f, std::fmax(0.0f, (y - bottom) / -bottom));
    return 0.5f - std::sin(std::asin(1.0f - 2.0f * t) / 3.0f);
}

void LevelVoices()
{
    static const uintptr_t rootSlot = ResolveByHash(kHashEngineRoot);
    static const auto setRtpc =
        AtRva<AkSetRtpcByPlayingIdFn>(0x1acf6a0, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57});
    static bool stopped = false;
    if (stopped)
    {
        return;
    }
    if (!rootSlot || !setRtpc)
    {
        stopped = true;
        Log("the per-car level is not this game build's - every car plays at the top of the range");
        return;
    }
    static const uintptr_t trafficVtbl = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + kRvaTrafficEmitterVtbl;
    Voice voices[256];
    uint32_t count = 0;
    if (!SafeCollectVoices(rootSlot, trafficVtbl, voices, 256, &count))
    {
        stopped = true;
        Log("the station listeners could not be read - every car plays at the top of the range");
        return;
    }
    ++g_round;
    const float span = std::fmin(0.0f, g_levelBottom - g_levelTop);
    for (uint32_t i = 0; i < count; ++i)
    {
        auto [it, added] = g_levelled.try_emplace(voices[i].playingId, g_round);
        it->second = g_round;
        if (added)
        {
            g_voices[voices[i].playingId] = LiveVoice{voices[i].key, voices[i].receiver, voices[i].car};
            // Bypass the parameter's own smoothing, which is built for a car's speed: the level applies at once.
            setRtpc(kRtpcEngageMovingFaster, CurveValueFor(span * (1.0f - ShareOf(voices[i].key))),
                    voices[i].playingId, 0, kCurveLinear, true);
        }
    }
    std::erase_if(g_levelled, [](const auto& aEntry) { return aEntry.second != g_round; });
    std::erase_if(g_voices, [](const auto& aEntry) { return !g_levelled.contains(aEntry.first); });
}

// --- 5. muffled from inside a car ---
// veh_interior (290459857), read global through AK::SoundEngine::Query::GetRTPCValue (0x1ad2c60), is the game's
// own "inside a car" value: 1 in first person in a car, 0 in third person or on foot, lowered by broken glass. The
// mixer follows it: volume g_levelTop + g_muffleDb * value, low-pass g_muffleLpf * value.
//
// Both the query and the mixer write enter Wwise's global lock, a CRITICAL_SECTION the audio thread holds for its
// whole render pass, so waiting on it stalls the game thread for milliseconds. The lock is tried instead: when the
// audio thread has it, this frame is skipped. The section is re-entrant, so the calls inside take it again freely.
// Its address is the lea at +0x17 in CAkFunctionCritical's enter (0x1af6d90): 48 8D 0D rel32.
constexpr uint32_t kRtpcVehInterior = 290459857;
using AkGetRtpcValueFn = int (*)(uint32_t aRtpc, uint64_t aGameObject, uint32_t aPlayingId, float* aValue,
                                 int* aScope);
float g_interior = -1.0f;  // the value the mixer was last written for

float SafeInterior(AkGetRtpcValueFn aGet)
{
    __try
    {
        float value = 0.0f;
        int scope = 1;  // RTPCValue_Global
        return aGet(kRtpcVehInterior, ~0ull, 0, &value, &scope) == 1 ? value : -1.0f;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1.0f;
    }
}

LPCRITICAL_SECTION WwiseLock()
{
    const auto enter = AtRva<const uint8_t*>(0x1af6d90, {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B});
    if (!enter || enter[0x17] != 0x48 || enter[0x18] != 0x8D || enter[0x19] != 0x0D)
    {
        return nullptr;
    }
    int32_t rel = 0;
    std::memcpy(&rel, enter + 0x1a, sizeof(rel));
    return reinterpret_cast<LPCRITICAL_SECTION>(const_cast<uint8_t*>(enter) + 0x1e + rel);
}

void MuffleLocked(AkGetRtpcValueFn aGet, bool* aStopped);

void Muffle()
{
    static const auto get = AtRva<AkGetRtpcValueFn>(0x1ad2c60, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C});
    static const auto lock = WwiseLock();
    static bool stopped = false;
    if (stopped)
    {
        return;
    }
    if (!get || !lock)
    {
        stopped = true;
        Log("the in-car muffling is not this game build's - radios are not muffled from inside a car");
        return;
    }
    if (!TryEnterCriticalSection(lock))
    {
        return;
    }
    MuffleLocked(get, &stopped);
    LeaveCriticalSection(lock);
}

void MuffleLocked(AkGetRtpcValueFn get, bool* aStopped)
{
    const float interior = SafeInterior(get);
    if (interior < 0.0f || std::fabs(interior - g_interior) < 0.01f)
    {
        return;
    }
    const float target = g_levelTop + g_muffleDb * interior;
    float found = 0.0f;
    if (SetMixerVolume(target, g_mixerNow, g_muffleLpf * interior, &found) == Mixer::Set)
    {
        g_mixerNow = target;
        g_interior = interior;
    }
    else
    {
        *aStopped = true;
        Log("the NPC car radio mixer holds " + std::to_string(found) + " dB, not this plugin's - muffling stopped");
    }
}

// --- 6. open-air cars ---
// A traffic car with a seat door open or torn off, a window down, or no side windows plays its radio without the
// receiver's EQ: its own speakers, heard through the opening. Every 250 ms each car with a radio voice is read
// through RTTI (ScriptGameInstance.FindEntityByID on the emitter's entity id, then GetVehiclePS and GetDoorState /
// GetWindowState for seats 0 to 3); its record's hasSideWindows and player_audio_resource are read once. A car
// that left traffic (kind 3) has no entity id and keeps its EQ.
//
// The EQ goes off for that car's Wwise game object only: CAkParameterNodeBase::BypassFX (0x1ade2b0, the setter
// Wwise's Bypass Effect action uses) takes (node, effect slot, bypass, CAkRegisteredObj*, fromReset), one slot per
// call, and a voice that starts later on the same game object starts with it.
constexpr uint32_t kNpcReceiverSounds[] = {882536694, 381815666, 234614324, 924785061,
                                           329334756, 937325872, 686441992};  // kNpcReceivers' sounds, in order
constexpr uint32_t kEffectSlots = 2;  // the muscle receiver has two effects, the others one
constexpr int kSeatDoors = 4;         // EVehicleDoor seat_front_left .. seat_back_right
using AkBypassFxFn = void (*)(uintptr_t aNode, uint32_t aSlot, bool aBypass, uintptr_t aObject, bool aFromReset);
using AkGetObjFn = uintptr_t (*)(uintptr_t aRegistry, uint64_t aGameObject);
using AkGameObjectFn = uint64_t (*)(uint32_t aPlayingId);

bool g_forceOpen = false;  // tuning build: every traffic car open-air
#ifdef ATR_TUNE
// Tuning build only: cars read, found as vehicles and open, since the last perf line.
uint32_t g_carsRead = 0, g_carsFound = 0, g_carsOpen = 0;
#endif

struct Car
{
    bool open = false;
    int convertible = -1;  // not read yet
};
std::unordered_map<uint64_t, Car> g_cars;  // by entity id

// The bypass this plugin set, by game object and receiver sound.
std::map<std::pair<uint64_t, uint32_t>, bool> g_bypass;

struct CarRtti
{
    RED4ext::CBaseFunction* find = nullptr;
    RED4ext::CClass* vehicle = nullptr;
    RED4ext::CBaseFunction* ps = nullptr;
    RED4ext::CBaseFunction* recordId = nullptr;
    RED4ext::CBaseFunction* door = nullptr;
    RED4ext::CBaseFunction* window = nullptr;
};

bool ResolveCarRtti(CarRtti& aOut)
{
    auto* rtti = RED4ext::CRTTISystem::Get();
    auto* game = rtti ? rtti->GetClass("ScriptGameInstance") : nullptr;
    auto* ps = rtti ? rtti->GetClass("VehicleComponentPS") : nullptr;
    aOut.vehicle = rtti ? rtti->GetClass("vehicleBaseObject") : nullptr;
    if (!game || !ps || !aOut.vehicle)
    {
        return false;
    }
    aOut.find = game->GetFunction("FindEntityByID");
    aOut.ps = aOut.vehicle->GetFunction("GetVehiclePS");
    aOut.recordId = aOut.vehicle->GetFunction("GetRecordID");
    aOut.door = ps->GetFunction("GetDoorState");
    aOut.window = ps->GetFunction("GetWindowState");
    return aOut.find && aOut.ps && aOut.recordId && aOut.door && aOut.window;
}

// A door or window state (both enums: 0 closed, 1 open, 2 detached for doors).
uint8_t StateOf(RED4ext::IScriptable* aPs, RED4ext::CBaseFunction* aFn, int aDoor)
{
    uint64_t door = static_cast<uint64_t>(aDoor);
    uint64_t state = 0;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &door);
    RED4ext::ExecuteFunction(aPs, aFn, &state, args);
    return static_cast<uint8_t>(state);
}

bool IsConvertible(const RED4ext::TweakDBID& aRecord)
{
    auto* db = RED4ext::TweakDB::Get();
    if (!db || !aRecord.IsValid())
    {
        return false;
    }
    if (auto* flat = db->GetFlatValue(RED4ext::TweakDBID(aRecord, ".hasSideWindows")))
    {
        const auto value = flat->GetValue();
        if (value.value && !*static_cast<bool*>(value.value))
        {
            return true;
        }
    }
    if (auto* flat = db->GetFlatValue(RED4ext::TweakDBID(aRecord, ".player_audio_resource")))
    {
        const auto value = flat->GetValue();
        const std::string set = value.value ? static_cast<RED4ext::CString*>(value.value)->c_str() : "";
        return set.find("targa") != std::string::npos || set.find("cabrio") != std::string::npos;
    }
    return false;
}

bool CarIsOpen(const CarRtti& aRtti, RED4ext::ScriptGameInstance& aGame, uint64_t aEntityId, Car& aCar)
{
    RED4ext::ent::EntityID id(aEntityId);
    RED4ext::Handle<RED4ext::IScriptable> entity;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &aGame);
    args.emplace_back(nullptr, &id);
    if (!RED4ext::ExecuteFunction(static_cast<void*>(nullptr), aRtti.find, &entity, args) || !entity ||
        !entity->GetType()->IsA(aRtti.vehicle))
    {
        return false;
    }
#ifdef ATR_TUNE
    ++g_carsFound;
#endif
    if (aCar.convertible < 0)
    {
        RED4ext::TweakDBID record;
        RED4ext::ExecuteFunction(entity.instance, aRtti.recordId, &record);
        aCar.convertible = IsConvertible(record) ? 1 : 0;
    }
    if (aCar.convertible == 1)
    {
        return true;
    }
    RED4ext::Handle<RED4ext::IScriptable> ps;
    if (!RED4ext::ExecuteFunction(entity.instance, aRtti.ps, &ps) || !ps)
    {
        return false;
    }
    for (int door = 0; door < kSeatDoors; ++door)
    {
        if (StateOf(ps.instance, aRtti.door, door) != 0 || StateOf(ps.instance, aRtti.window, door) != 0)
        {
            return true;
        }
    }
    return false;
}

// Every 250 ms on the game thread: whether each car with a radio voice is open.
void ReadCars()
{
    static uint64_t next = 0;
    static CarRtti rtti;
    static int resolved = 0;  // 1 found, -1 not this game build's
    const uint64_t now = GetTickCount64();
    if (resolved < 0 || now < next)
    {
        return;
    }
    next = now + 250;
    if (!resolved)
    {
        resolved = ResolveCarRtti(rtti) ? 1 : -1;
        if (resolved < 0)
        {
            Log("open-air cars: the vehicle functions are not this game build's - every car keeps its EQ");
            return;
        }
    }
    auto* engine = RED4ext::CGameEngine::Get();
    auto* instance = engine && engine->framework ? engine->framework->gameInstance : nullptr;
    if (!instance)
    {
        return;
    }
    RED4ext::ScriptGameInstance game(instance);
    std::set<uint64_t> cars;
    for (const auto& [playingId, voice] : g_voices)
    {
        if (voice.car)
        {
            cars.insert(voice.key);
        }
    }
    std::erase_if(g_cars, [&](const auto& aEntry) { return !cars.contains(aEntry.first); });
    for (const auto key : cars)
    {
        auto& car = g_cars[key];
        car.open = CarIsOpen(rtti, game, key, car);
#ifdef ATR_TUNE
        ++g_carsRead;
        g_carsOpen += car.open ? 1 : 0;
#endif
    }
}

bool WantsOpen(const LiveVoice& aVoice)
{
    if (g_forceOpen)
    {
        return true;
    }
    const auto car = aVoice.car ? g_cars.find(aVoice.key) : g_cars.end();
    return car != g_cars.end() && car->second.open;
}

bool SafeBypass(AkBypassFxFn aBypass, AkGetObjFn aGetObj, uintptr_t aRegistry, uint64_t aGameObject, uint32_t aSound,
                bool aOn)
{
    __try
    {
        const uintptr_t node = SafeFindObject(aSound);
        const uintptr_t object = aRegistry ? aGetObj(aRegistry, aGameObject) : 0;
        if (!node || !object)
        {
            return false;
        }
        for (uint32_t slot = 0; slot < kEffectSlots; ++slot)
        {
            aBypass(node, slot, aOn, object, false);
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

uint64_t SafeGameObject(AkGameObjectFn aGameObject, uint32_t aPlayingId)
{
    __try
    {
        return aGameObject(aPlayingId);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return ~0ull;
    }
}

bool SafeObjectGone(AkGetObjFn aGetObj, uintptr_t aRegistry, uint64_t aGameObject)
{
    __try
    {
        return !aGetObj(aRegistry, aGameObject);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return true;
    }
}

// Every frame: Wwise's lock is tried only when a voice's game object is unknown or its car changed state.
void OpenAir()
{
    static const auto bypass =
        AtRva<AkBypassFxFn>(0x1ade2b0, {0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x6C, 0x24, 0x20});
    static const auto getObj =
        AtRva<AkGetObjFn>(0x1b3b230, {0x44, 0x8B, 0x41, 0x30, 0x4C, 0x8B, 0xCA, 0x45, 0x85, 0xC0});
    static const auto gameObject = AtRva<AkGameObjectFn>(0x1ad2680, {0x8B, 0xD1, 0x48, 0x8B, 0x0D});
    static const auto lock = WwiseLock();
    static const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    static bool stopped = false;
    static uint64_t nextSweep = 0;
    if (stopped)
    {
        return;
    }
    if (!bypass || !getObj || !gameObject || !lock)
    {
        stopped = true;
        Log("open-air cars: the effect bypass is not this game build's - every car keeps its EQ");
        return;
    }
    const uint64_t now = GetTickCount64();
    const bool sweep = now >= nextSweep;
    bool pending = sweep;
    for (const auto& [playingId, voice] : g_voices)
    {
        if (pending)
        {
            break;
        }
        if (voice.gameObject == ~0ull)
        {
            pending = true;
            break;
        }
        const auto have = g_bypass.find({voice.gameObject, kNpcReceiverSounds[voice.receiver]});
        pending = WantsOpen(voice) != (have != g_bypass.end() && have->second);
    }
    if (!pending || !TryEnterCriticalSection(lock))
    {
        return;
    }
    const uintptr_t registry = Read<uintptr_t>(base + 0x339f7b0);
    std::set<uint64_t> live;
    for (auto& [playingId, voice] : g_voices)
    {
        if (voice.gameObject == ~0ull)
        {
            voice.gameObject = SafeGameObject(gameObject, playingId);
        }
        if (voice.gameObject == ~0ull)
        {
            continue;
        }
        live.insert(voice.gameObject);
        const uint32_t sound = kNpcReceiverSounds[voice.receiver];
        const bool want = WantsOpen(voice);
        auto& have = g_bypass[{voice.gameObject, sound}];
        if (have != want && SafeBypass(bypass, getObj, registry, voice.gameObject, sound, want))
        {
            have = want;
        }
    }
    if (sweep)
    {
        // A game object that is gone takes its bypass with it.
        std::erase_if(g_bypass, [&](const auto& aEntry) {
            return !live.contains(aEntry.first.first) && SafeObjectGone(getObj, registry, aEntry.first.first);
        });
        nextSweep = now + 5000;
    }
    LeaveCriticalSection(lock);
}

// --- 7. muffled by walls ---
// The NPC receivers and their mixer read no occlusion parameter, so walls did not stop them; the world radio
// (radio_default_int) reads game_occlusion. Its two curves go onto the NPC mixer once, beside the volume write:
// volume 0 to -12 dB and low-pass 0 to 57. The attach is the parameter node's own SetRTPC virtual at slot 0x1c0
// (0x1adda50), the one a bank load calls: (object start, curve description, points), 1 on success.
constexpr uint32_t kRtpcGameOcclusion = 154512849;
struct AkCurveDesc
{
    uint8_t type;
    uint8_t accumulation;
    uint8_t scaling;
    uint8_t pad;
    uint32_t rtpc;
    uint32_t parameter;
    uint32_t curve;
    uint32_t count;
};
struct AkCurvePoint
{
    float from;
    float to;
    uint32_t interpolation;
};
using AkAttachRtpcFn = int (*)(uintptr_t aObject, const AkCurveDesc* aDesc, const AkCurvePoint* aPoints);

int SafeAttach(AkAttachRtpcFn aAttach, uintptr_t aObject, const AkCurveDesc& aDesc, const AkCurvePoint* aPoints)
{
    __try
    {
        return aAttach(aObject, &aDesc, aPoints);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

void AttachOcclusion()
{
    static const auto attach =
        AtRva<AkAttachRtpcFn>(0x1adda50, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10});
    static const auto lockOn = AtRva<AkCriticalFn>(0x1af6d90, {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B});
    static const auto lockOff = AtRva<AkCriticalFn>(0x1af7100, {0x48, 0x83, 0xEC, 0x28, 0xFF, 0x15});
    if (!attach || !lockOn || !lockOff)
    {
        Log("the occlusion curves are not this game build's - walls do not muffle traffic radios");
        return;
    }
    // The world radio's own curves (radio.bnk, sound 161720350), with curve ids of this plugin's.
    static const AkCurvePoint volume[] = {{0.0f, 0.0f, kCurveLinear}, {1.0f, -0.7488113641738892f, kCurveLinear}};
    static const AkCurvePoint lowPass[] = {{0.0f, 0.0f, kCurveLinear}, {1.0f, 57.0f, kCurveLinear}};
    const AkCurveDesc volumeDesc{0, 2, 2, 0, kRtpcGameOcclusion, kPropVolume, 0xA7700001, 2};  // additive, dB
    const AkCurveDesc lowPassDesc{0, 6, 0, 0, kRtpcGameOcclusion, kPropLpf, 0xA7700002, 2};    // filter
    alignas(16) uint8_t critical[16] = {};
    lockOn(critical);
    const uintptr_t mixer = SafeFindObject(kNpcRadioMixer);
    const int a = mixer ? SafeAttach(attach, mixer, volumeDesc, volume) : 0;
    const int b = mixer ? SafeAttach(attach, mixer, lowPassDesc, lowPass) : 0;
    lockOff(critical);
    Log(a == 1 && b == 1 ? "walls muffle traffic radios: occlusion attached to the NPC car radio mixer"
                         : "the occlusion curves did not attach - walls do not muffle traffic radios");
}

#ifdef ATR_TUNE
// Tuning build only: atr_tune.txt beside the DLL, read every 2 s. Lines `levels <bottom dB> <top dB>` and
// `muffle <dB> <low-pass>`. A change writes the mixer again on the next frame; a levels change also levels every
// live voice again. `open 1` treats every traffic car as open-air, for comparing by ear.
void ReadTuning()
{
    static uint64_t next = 0;
    const uint64_t now = GetTickCount64();
    if (now < next)
    {
        return;
    }
    next = now + 2000;
    wchar_t path[MAX_PATH] = {};
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&ReadTuning), &self);
    GetModuleFileNameW(self, path, MAX_PATH);
    std::wstring file(path);
    file = file.substr(0, file.find_last_of(L'\\') + 1) + L"atr_tune.txt";
    FILE* f = _wfopen(file.c_str(), L"r");
    if (!f)
    {
        return;
    }
    float bottom = g_levelBottom, top = g_levelTop, muffleDb = g_muffleDb, muffleLpf = g_muffleLpf;
    int open = g_forceOpen ? 1 : 0;
    char text[128];
    while (std::fgets(text, sizeof(text), f))
    {
        std::sscanf(text, " levels %f %f", &bottom, &top);
        std::sscanf(text, " muffle %f %f", &muffleDb, &muffleLpf);
        std::sscanf(text, " open %d", &open);
    }
    std::fclose(f);
    if ((open != 0) != g_forceOpen)
    {
        g_forceOpen = open != 0;
        Log(g_forceOpen ? "tune: every traffic car open-air" : "tune: open-air by doors and windows");
    }
    const bool levels = bottom != g_levelBottom || top != g_levelTop;
    const bool muffle = muffleDb != g_muffleDb || muffleLpf != g_muffleLpf;
    if (!levels && !muffle)
    {
        return;
    }
    g_levelBottom = bottom;
    g_levelTop = top;
    g_muffleDb = muffleDb;
    g_muffleLpf = muffleLpf;
    g_interior = -1.0f;  // makes the next frame write the mixer again
    if (levels)
    {
        g_levelled.clear();
    }
    char line[160];
    std::snprintf(line, sizeof(line), "tune: levels %.1f to %.1f dB, muffle %.1f dB low-pass %.0f", bottom, top,
                  muffleDb, muffleLpf);
    Log(line);
}
#endif

#ifdef ATR_TUNE
// Tuning build only: the voice scan's own cost, logged every 10 s as scans, average and worst time per scan, and
// the most voices seen in one scan.
struct ScanCost
{
    uint64_t scans = 0;
    double total = 0.0;
    double worst = 0.0;
    uint32_t voices = 0;
    uint64_t next = 0;
};
ScanCost g_cost;

void TimedLevelVoices()
{
    static const double toMicros = []
    {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return 1e6 / static_cast<double>(f.QuadPart);
    }();
    LARGE_INTEGER a, b;
    QueryPerformanceCounter(&a);
    LevelVoices();
    Muffle();
    ReadCars();
    OpenAir();
    QueryPerformanceCounter(&b);
    const double us = static_cast<double>(b.QuadPart - a.QuadPart) * toMicros;
    ++g_cost.scans;
    g_cost.total += us;
    g_cost.worst = std::fmax(g_cost.worst, us);
    g_cost.voices = (std::max)(g_cost.voices, static_cast<uint32_t>(g_levelled.size()));
    const uint64_t now = GetTickCount64();
    if (now < g_cost.next)
    {
        return;
    }
    if (g_cost.next)
    {
        char line[160];
        std::snprintf(line, sizeof(line), "perf: %llu scans, average %.1f us, worst %.1f us, up to %u voices",
                      static_cast<unsigned long long>(g_cost.scans), g_cost.total / g_cost.scans, g_cost.worst,
                      g_cost.voices);
        Log(line);
        std::snprintf(line, sizeof(line), "cars: %u reads, %u found as vehicles, %u open", g_carsRead, g_carsFound,
                      g_carsOpen);
        Log(line);
        g_carsRead = g_carsFound = g_carsOpen = 0;
    }
    g_cost = ScanCost{};
    g_cost.next = now + 10000;
}
#endif

// Once a second until the radio bank is loaded and the mixer set; the bank stays loaded for the session, save
// loads and the main menu included, so one write holds. After that, every frame, each new radio voice gets its
// level.
bool OnUpdate(RED4ext::CGameApplication*)
{
    static bool mixerDone = false;
    static uint64_t next = 0;
    if (!mixerDone)
    {
        const uint64_t now = GetTickCount64();
        if (now < next)
        {
            return false;
        }
        next = now + 1000;
        float found = 0.0f;
        switch (SetMixerVolume(g_levelTop, g_levelTop, 0.0f, &found))
        {
        case Mixer::Set:
            g_mixerNow = g_levelTop;
            Log("the NPC car radio mixer plays at " + std::to_string(found) + " dB");
            AttachOcclusion();
            mixerDone = true;
            break;
        case Mixer::Unexpected:
            Log("the NPC car radio mixer holds " + std::to_string(found) + " dB, not the game's -8 - left as it is");
            mixerDone = true;
            break;
        case Mixer::NotLoaded:
            break;
        }
        return false;
    }
#ifdef ATR_TUNE
    ReadTuning();
    TimedLevelVoices();
#else
    LevelVoices();
    Muffle();
    ReadCars();
    OpenAir();
#endif
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
        PatchCarRadioRule();
        static RED4ext::v1::GameState state{
            .OnEnter = nullptr,
            .OnUpdate = OnUpdate,
            .OnExit = nullptr,
        };
        aSdk->gameStates->Add(aHandle, RED4ext::EGameStateType::Running, &state);
    }
    return true;
}
