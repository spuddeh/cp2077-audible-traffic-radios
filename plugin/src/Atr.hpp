// Audible Traffic Radios: what the plugin's files share.
#pragma once

#include <Windows.h>
#include <RED4ext/RED4ext.hpp>
#include <RED4ext/CString.hpp>
#include <RED4ext/GameEngine.hpp>
#include <RED4ext/RTTISystem.hpp>
#include <RED4ext/ResourceLoader.hpp>
#include <RED4ext/ResourcePath.hpp>
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
#include <mutex>
#include <set>
#include <utility>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace atr
{
extern const RED4ext::v1::Sdk* g_sdk;
extern RED4ext::v1::PluginHandle g_handle;

void Log(const std::string& aText);
uintptr_t ResolveByHash(uint32_t aHash);

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

// --- shared types and constants ---
constexpr uint32_t kNpcRadioMixer = 398448775;
constexpr uint8_t kPropVolume = 0;
constexpr uint8_t kPropLpf = 2;  // the actor-mixer property bundle's low-pass id: radio.bnk stores 10 to 64 under it

using AkSetPropFn = void (*)(void* aObject, int aProp, float aValue, float aMin, float aMax);
using AkCriticalFn = void (*)(void* aSelf);

enum class Mixer
{
    NotLoaded,
    Set,
    Unexpected,
};

constexpr int kCurveLinear = 4;
constexpr uint64_t kNpcReceivers[] = {
    RED4ext::FNV1a64("radio_car_lowend_npc"), RED4ext::FNV1a64("radio_car_muscle_npc"),
    RED4ext::FNV1a64("radio_car_sports_npc"), RED4ext::FNV1a64("radio_car_suv_npc"),
    RED4ext::FNV1a64("radio_car_truck_npc"),  RED4ext::FNV1a64("radio_car_hyper_npc"),
    RED4ext::FNV1a64("radio_car_police_npc"),
};
constexpr const char* kReceiverNames[] = {"lowend", "muscle", "sports", "suv", "truck", "hyper", "police"};

// What the open-air cars need of each live voice: its car, receiver and Wwise game object (found later, under
// Wwise's lock).
struct LiveVoice
{
    uint64_t key;
    uint8_t receiver;
    bool car;  // the key is the car's entity id
    uint64_t gameObject = ~0ull;
    float levelDb = 0.0f;  // on the mixer, the top of the range less the car's share
};

using AkGetRtpcValueFn = int (*)(uint32_t aRtpc, uint64_t aGameObject, uint32_t aPlayingId, float* aValue,
                                 int* aScope);

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

// --- shared state ---
extern float g_levelTop;
extern float g_levelBottom;
extern float g_muffleDb;
extern float g_muffleLpf;
extern float g_mixerNow;
extern float g_interior;
extern std::unordered_map<uint32_t, uint32_t> g_levelled;
extern std::unordered_map<uint32_t, LiveVoice> g_voices;
extern bool g_forceOpen;
extern float g_weightWindow;
extern float g_weightDoor;
extern float g_weightDetached;
extern float g_weightGlass;
#ifdef ATR_TUNE
extern uint32_t g_carsRead, g_carsFound, g_carsOpen;
#endif

// --- what each file offers the others ---
uintptr_t SafeFindObject(uint32_t aId);                                         // Wwise.cpp
Mixer SetMixerVolume(float aTarget, float aPrevious, float aLpf, float* aFound);  // Wwise.cpp
LPCRITICAL_SECTION WwiseLock();                                                  // Muffling.cpp
void PatchPredicate();                                                           // Patches.cpp
void PatchCarRadioRule();                                                        // Patches.cpp
void LevelVoices();                                                              // Levels.cpp
void Muffle();                                                                   // Muffling.cpp
void AttachOcclusion();                                                          // Muffling.cpp
void ReadCars();                                                                 // OpenAir.cpp
void OpenAir();                                                                  // OpenAir.cpp
float OpennessOf(const LiveVoice& aVoice);                                       // OpenAir.cpp
std::pair<int, int> AttachEqFades();                                             // OpenAir.cpp
void ThemeStations();                                                            // Stations.cpp
bool LiftDucks();                                                                // Ducks.cpp
#ifdef ATR_TUNE
void WriteOverlay();                                                             // OpenAir.cpp
void ReadTuning();                                                               // Tuning.cpp
void TimedLevelVoices();                                                         // Tuning.cpp
void DrainEqView();                                                              // Tuning.cpp
void HookEqView(RED4ext::v1::PluginHandle aHandle, const RED4ext::v1::Sdk* aSdk);  // Tuning.cpp
#endif
} // namespace atr
