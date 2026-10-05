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
#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <unordered_map>
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
// Dev levels, set from atr_dev.txt: `fast <0..1>` and `gain <0..16>`; a negative value leaves it alone.
float g_devFast = -1.0f;
float g_devGain = -1.0f;

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
    if (g_devFast >= 0.0f)
    {
        g_ak.setRtpc(kRtpcEngageMovingFaster, g_devFast, go, 0, kCurveLinear, false);
    }
    uint64_t listeners[4] = {};
    uint32_t count = 4;
    g_ak.listeners(go, listeners, &count);
    for (uint32_t i = 0; i < count && i < 4; ++i)
    {
        if (g_devGain >= 0.0f)
        {
            g_ak.setOutputBusVolume(go, listeners[i], g_devGain);
        }
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

// --- receiver repoint (experiment) ---
// A sound's name resolves to its Wwise event through audio::GMetadataManager's Map<CName, AudioEventMetadata>
// (+0x120; values at +0x130, 0x68 bytes each, wwiseId at +0x30), looked up by SoundBuilder::SetupPlayContext.
// Repointing a vanilla receiver's row at a cloned event keeps the car on its vanilla receiver name. The row
// is written only while it still holds the vanilla id, the FNV-1 of the name.
constexpr uintptr_t kRvaMetadataManager = 0x3427900;
constexpr size_t kMetadataEventMap = 0x120;
constexpr size_t kMetadataEventValues = 0x130;
constexpr size_t kEventMetadataStride = 0x68;
constexpr size_t kEventMetadataWwiseId = 0x30;
using MapFindFn = bool (*)(void* aMap, const uint64_t* aKey, uint32_t* aIndex);

struct RepointRow
{
    const char* vanilla;
    const char* clone;
};
constexpr RepointRow kRepoint[] = {
    {"radio_car_lowend_npc", "atr_v0_exact"},
    {"radio_car_suv_npc", "atr_v1_world_bus"},
    {"radio_car_sports_npc", "atr_v2_world_attenuation"},
};
bool g_repointed = false;
std::unordered_map<std::string, uint32_t> g_devWritten;  // vanilla event -> id last written to its row

uint32_t Fnv1(const char* aName)
{
    uint32_t h = 2166136261u;
    for (const char* c = aName; *c; ++c)
    {
        h *= 16777619u;
        h ^= static_cast<uint8_t>(*c >= 'A' && *c <= 'Z' ? *c + 32 : *c);
    }
    return h;
}

// 0 missing map, 1 row not found, 2 row holds another id, 3 written, 4 already the clone. A row is written
// only while it holds the vanilla id or aPrevious, the id this plugin last wrote to it.
int SafeRepointRow(MapFindFn aFind, uint64_t aKey, uint32_t aVanilla, uint32_t aClone, uint32_t aPrevious = 0)
{
    __try
    {
        const auto manager = Read<uintptr_t>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + kRvaMetadataManager);
        if (!manager)
        {
            return 0;
        }
        uint32_t index = 0;
        if (!aFind(reinterpret_cast<void*>(manager + kMetadataEventMap), &aKey, &index))
        {
            return 1;
        }
        const auto values = Read<uintptr_t>(manager + kMetadataEventValues);
        auto* id = reinterpret_cast<uint32_t*>(values + index * kEventMetadataStride + kEventMetadataWwiseId);
        if (*id == aClone)
        {
            return 4;
        }
        if (*id != aVanilla && (!aPrevious || *id != aPrevious))
        {
            return 2;
        }
        *id = aClone;
        return 3;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

// The clone bank cites radio.bnk's objects (the mixer's parent, the receive plugins, the effects), so it is
// loaded once the game is running rather than at audio start-up, where it fails with AK_IDNotFound.
using LoadBankMemoryCopyFn = int (*)(const void* aData, uint32_t aSize, uint32_t* aBankId);
constexpr const char* kBankFile = "atr_npc_receivers.bnk";

std::filesystem::path PluginDir()
{
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&PluginDir), &self);
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(self, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
}

bool LoadBankFile(const std::filesystem::path& aPath)
{
    static const auto load = AtRva<LoadBankMemoryCopyFn>(0x1ac7e00, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74});
    if (!load)
    {
        Log("bank: LoadBankMemoryCopy did not match this build - not loaded");
        return false;
    }
    std::ifstream file(aPath, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.size() < 8 || std::memcmp(bytes.data(), "BKHD", 4) != 0)
    {
        Log("bank: " + aPath.filename().string() + " is missing or not a soundbank");
        return false;
    }
    std::vector<uint8_t> aligned(bytes.size() + 16);
    auto* at = aligned.data() + ((16 - (reinterpret_cast<uintptr_t>(aligned.data()) & 15)) & 15);
    std::memcpy(at, bytes.data(), bytes.size());
    uint32_t bankId = 0;
    const int result = load(at, static_cast<uint32_t>(bytes.size()), &bankId);
    Log("bank: " + aPath.filename().string() + " LoadBankMemoryCopy -> " + std::to_string(result) + " (bank " +
        std::to_string(bankId) + ", " +
        std::to_string(bytes.size()) + " bytes)");
    return result == 1 || result == 69;  // AK_Success, AK_BankAlreadyLoaded
}

void Repoint()
{
    static int bankState = 0;  // 0 not tried, 1 loaded, 2 failed
    if (bankState == 0)
    {
        bankState = LoadBankFile(PluginDir() / kBankFile) ? 1 : 2;
    }
    if (bankState == 2)
    {
        g_repointed = true;
        Log("repoint: the clone bank did not load - vanilla receivers left as they are");
        return;
    }
    static const auto find = AtRva<MapFindFn>(0xb4ec2c, {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74});
    if (!find)
    {
        Log("repoint: the metadata map lookup did not match this build - nothing repointed");
        g_repointed = true;
        return;
    }
    static const char* kResult[] = {"metadata not loaded yet", "no such row", "row holds an unexpected id",
                                    "repointed", "already repointed"};
    bool pending = false;
    std::string line = "repoint:";
    for (const auto& row : kRepoint)
    {
        const int r = SafeRepointRow(find, RED4ext::CName(row.vanilla).hash, Fnv1(row.vanilla), Fnv1(row.clone));
        pending |= r == 0;
        if (r == 3 || r == 4)
        {
            g_devWritten[row.vanilla] = Fnv1(row.clone);
        }
        line += std::string(" ") + row.vanilla + " -> " + row.clone + " " + kResult[r] + ";";
    }
    if (!pending)
    {
        g_repointed = true;
        Log(line);
    }
}

// --- dev meter ---
// AK::SoundEngine::RegisterBusMeteringCallback (0x1acb900) on the NPC vehicle radio bus and the world radio bus.
// The callback runs on the audio thread with AkBusMeteringCallbackInfo: metering at +0x10 (per-channel linear
// arrays at [m+0] and [m+0x10]), channel count in the low byte of +0x18, as MixBusMeter::OnMeteringCallback
// reads it. The engine registers no callback on the NPC bus, so this one takes nothing from it.
using RegisterMeterFn = int (*)(uint32_t aBus, void (*aCallback)(void*), uint32_t aFlags, void* aCookie);
constexpr uint32_t kNpcRadioBus = 194813043;
constexpr uint32_t kWorldRadioBus = 918052088;
constexpr uint32_t kMeterPeakAndRms = 1 | 4;
struct MeterReading
{
    std::atomic<float> a{0.0f};
    std::atomic<float> b{0.0f};
    std::atomic<float> maxA{0.0f};
    std::atomic<uint32_t> calls{0};
    std::atomic<uint32_t> channels{0};
    std::atomic<float> raw{0.0f};
    std::atomic<uint32_t> bus{0};
};
MeterReading g_npcMeter;
MeterReading g_worldMeter;

void ReadMeter(void* aInfo, MeterReading& aOut)
{
    const auto info = reinterpret_cast<uintptr_t>(aInfo);
    const auto metering = *reinterpret_cast<uintptr_t*>(info + 0x10);
    const auto channels = *reinterpret_cast<uint8_t*>(info + 0x18);
    aOut.calls.fetch_add(1);
    aOut.channels.store(*reinterpret_cast<uint32_t*>(info + 0x18));
    if (metering && *reinterpret_cast<float**>(metering))
    {
        aOut.raw.store(**reinterpret_cast<float**>(metering));
    }
    if (!metering || !channels)
    {
        return;
    }
    const auto* a = *reinterpret_cast<float**>(metering);
    const auto* b = *reinterpret_cast<float**>(metering + 0x10);
    float sa = 0.0f, sb = 0.0f;
    for (uint8_t i = 0; i < channels; ++i)
    {
        sa = std::max(sa, a ? a[i] : 0.0f);
        sb = std::max(sb, b ? b[i] : 0.0f);
    }
    aOut.a.store(sa);
    aOut.b.store(sb);
    if (sa > aOut.maxA.load())
    {
        aOut.maxA.store(sa);
    }
}

void NpcMeterCallback(void* aInfo)
{
    ReadMeter(aInfo, g_npcMeter);
}

void WorldMeterCallback(void* aInfo)
{
    ReadMeter(aInfo, g_worldMeter);
}

std::string Db(float aLinear)
{
    char buf[16];
    if (aLinear <= 0.000001f)
    {
        return "-inf";
    }
    std::snprintf(buf, sizeof(buf), "%.1f", 20.0f * std::log10(aLinear));
    return buf;
}

RegisterMeterFn g_registerMeter = nullptr;

void MeterTick()
{
    static bool registered = false;
    if (!registered)
    {
        registered = true;
        const auto reg = AtRva<RegisterMeterFn>(0x1acb900, {0x48, 0x83, 0xEC, 0x38, 0x80, 0x3D, 0x49, 0x3E});
        if (!reg)
        {
            Log("meter: RegisterBusMeteringCallback did not match this build");
            return;
        }
        g_registerMeter = reg;
        g_npcMeter.bus = kNpcRadioBus;
        g_worldMeter.bus = kWorldRadioBus;
        Log("meter: npc bus " + std::to_string(reg(kNpcRadioBus, &NpcMeterCallback, kMeterPeakAndRms, nullptr)) +
            ", world bus " + std::to_string(reg(kWorldRadioBus, &WorldMeterCallback, kMeterPeakAndRms, nullptr)));
    }
    char diag[160];
    std::snprintf(diag, sizeof(diag), "meter: calls npc %u (cfg %08x raw %g)  bus %u: %u (cfg %08x raw %g)",
                  g_npcMeter.calls.exchange(0), g_npcMeter.channels.load(), g_npcMeter.raw.load(),
                  g_worldMeter.bus.load(), g_worldMeter.calls.exchange(0), g_worldMeter.channels.load(),
                  g_worldMeter.raw.load());
    Log(diag);
    Log("meter: npc " + Db(g_npcMeter.a.load()) + "/" + Db(g_npcMeter.b.load()) + " dB (max " +
        Db(g_npcMeter.maxA.exchange(0.0f)) + ")  world " + Db(g_worldMeter.a.load()) + "/" +
        Db(g_worldMeter.b.load()) + " dB (max " + Db(g_worldMeter.maxA.exchange(0.0f)) + ")");
}

// --- dev harness ---
// While atr_dev.txt sits beside the plugin, it is re-read every second and applied when it changes:
//   bank <absolute path>       load a test bank once
//   map <vanilla event> <event> point the vanilla receiver's metadata row at <event> (its own name restores it)
// Dev only: the file is never shipped.
std::string g_devText;
std::unordered_set<std::string> g_devBanks;

void DevTick()
{
    std::ifstream file(PluginDir() / "atr_dev.txt");
    if (!file)
    {
        return;
    }
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (text == g_devText)
    {
        return;
    }
    static const auto find = AtRva<MapFindFn>(0xb4ec2c, {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74});
    static const char* kResult[] = {"metadata not loaded yet", "no such row", "row holds an unexpected id",
                                    "repointed", "already there"};
    std::istringstream lines(text);
    std::string line;
    bool pending = false;
    while (std::getline(lines, line))
    {
        std::istringstream words(line);
        std::string verb, a, b;
        words >> verb;
        if (verb == "bank")
        {
            std::getline(words >> std::ws, a);
            while (!a.empty() && (a.back() == '\r' || a.back() == ' '))
            {
                a.pop_back();
            }
            if (!a.empty() && g_devBanks.insert(a).second)
            {
                LoadBankFile(a);
            }
        }
        else if (verb == "meter")
        {
            uint32_t bus = 0;
            words >> bus;
            if (g_registerMeter && bus)
            {
                g_worldMeter.bus = bus;
                Log("dev: meter on bus " + std::to_string(bus) + " -> " +
                    std::to_string(g_registerMeter(bus, &WorldMeterCallback, kMeterPeakAndRms, nullptr)));
            }
        }
        else if (verb == "fast" || verb == "gain")
        {
            float value = -1.0f;
            words >> value;
            (verb == "fast" ? g_devFast : g_devGain) = value;
            Log("dev: " + verb + " " + std::to_string(value));
        }
        else if (verb == "map" && (words >> a >> b) && find)
        {
            const uint32_t target = Fnv1(b.c_str());
            const auto previous = g_devWritten.count(a) ? g_devWritten[a] : 0u;
            const int r = SafeRepointRow(find, RED4ext::CName(a.c_str()).hash, Fnv1(a.c_str()), target, previous);
            pending |= r == 0;
            if (r == 3 || r == 4)
            {
                g_devWritten[a] = target;
            }
            Log("dev: " + a + " -> " + b + " " + kResult[r]);
        }
    }
    if (!pending)
    {
        g_devText = text;
    }
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
                    if (g_devFast >= 0.0f || g_devGain >= 0.0f)
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
    if (!g_repointed)
    {
        Repoint();
    }
    static uint64_t lastDev = 0;
    if (now - lastDev >= 1000)
    {
        lastDev = now;
        DevTick();
        MeterTick();
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
            Log(std::string("repoint: traffic receivers will play the cloned NPC chains") + std::string());
        }
        else
        {
            Log("boost: a Wwise function or the engine root did not match this build - no boost");
        }
    }
    return true;
}
