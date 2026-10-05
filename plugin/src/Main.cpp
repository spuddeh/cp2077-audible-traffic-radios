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
constexpr float kVanillaVolume = -8.0f;

// The range a car's radio plays in, in dB on the mixer. The mixer is set to the top; each voice is lowered from
// there by its own share of the range.
float g_levelTop = 12.0f;
float g_levelBottom = 6.0f;

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

// Sets the mixer's volume to aTarget when it holds the vanilla value or aPrevious; reports what it found.
Mixer SetMixerVolume(float aTarget, float aPrevious, float* aFound)
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
constexpr uint32_t kHashEngineRoot = 2549221846;
constexpr uint32_t kRtpcEngageMovingFaster = 139023859;
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
uint32_t g_round = 0;

bool IsNpcReceiver(uint64_t aName)
{
    for (const auto name : kNpcReceivers)
    {
        if (name == aName)
        {
            return true;
        }
    }
    return false;
}

bool SafeCollectVoices(uintptr_t aRootSlot, uint32_t* aOut, uint32_t aMax, uint32_t* aCount)
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
                if (!IsNpcReceiver(broadcast))
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
                            aOut[(*aCount)++] = id;
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

// The voice's share of the range, from its playing id: the same id always gets the same level.
float ShareOf(uint32_t aPlayingId)
{
    uint32_t h = aPlayingId * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return static_cast<float>(h) / 4294967295.0f;
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
    uint32_t voices[256];
    uint32_t count = 0;
    if (!SafeCollectVoices(rootSlot, voices, 256, &count))
    {
        stopped = true;
        Log("the station listeners could not be read - every car plays at the top of the range");
        return;
    }
    ++g_round;
    const float span = std::fmin(0.0f, g_levelBottom - g_levelTop);
    for (uint32_t i = 0; i < count; ++i)
    {
        auto [it, added] = g_levelled.try_emplace(voices[i], g_round);
        it->second = g_round;
        if (added)
        {
            setRtpc(kRtpcEngageMovingFaster, CurveValueFor(span * (1.0f - ShareOf(voices[i]))), voices[i], 0,
                    kCurveLinear, false);
        }
    }
    std::erase_if(g_levelled, [](const auto& aEntry) { return aEntry.second != g_round; });
}

#ifdef ATR_TUNE
// Tuning build only: atr_tune.txt beside the DLL, read every 2 s. One line, `levels <bottom dB> <top dB>`. A
// change sets the mixer to the new top and levels every live voice again.
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
    float bottom = 0.0f, top = 0.0f;
    const bool read = std::fscanf(f, " levels %f %f", &bottom, &top) == 2;
    std::fclose(f);
    if (!read || (bottom == g_levelBottom && top == g_levelTop))
    {
        return;
    }
    const float previous = g_levelTop;
    g_levelBottom = bottom;
    g_levelTop = top;
    float found = 0.0f;
    SetMixerVolume(g_levelTop, previous, &found);
    g_levelled.clear();
    char line[128];
    std::snprintf(line, sizeof(line), "tune: levels %.1f to %.1f dB, mixer at %.1f dB", bottom, top, found);
    Log(line);
}
#endif

// Once a second until the radio bank is loaded and the mixer set; the bank stays loaded for the session, save
// loads and the main menu included, so one write holds. After that, every 250 ms, each new radio voice gets its
// level.
bool OnUpdate(RED4ext::CGameApplication*)
{
    static bool mixerDone = false;
    static uint64_t next = 0;
    const uint64_t now = GetTickCount64();
    if (now < next)
    {
        return false;
    }
    if (!mixerDone)
    {
        next = now + 1000;
        float found = 0.0f;
        switch (SetMixerVolume(g_levelTop, g_levelTop, &found))
        {
        case Mixer::Set:
            Log("the NPC car radio mixer plays at " + std::to_string(found) + " dB");
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
    next = now + 250;
#ifdef ATR_TUNE
    ReadTuning();
#endif
    LevelVoices();
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
