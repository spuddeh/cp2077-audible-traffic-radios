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
//   5. A traffic car with a door open or torn off, a window down, broken glass or no side windows plays its radio without the
//      receiver's EQ, for that car only.
//   6. Walls muffle traffic radios: the NPC mixer gets the world radio's two occlusion curves.
//   7. Traffic cars can pick the stations no shipped list carries, on themed lists, and Morro Rock is spelt
//      right on the six lists that misspell it.
//   8. Combat and police music no longer duck traffic radios: their -96 dB duck on the NPC radio bus is set to 0.
//   9. Open cars fill more of the street's echo: the receivers' area reverb send rises with openness.
//  10. The player's own car, heard from outside, plays at the top of the range and follows the Car Radio slider.
//
// **Every address is verified before it is used, and a value is written only over the one expected.** On any
// other game build the plugin logs a line and changes nothing.

#include "Atr.hpp"

namespace atr
{
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
    static bool ducksDone = false;
    if (!ducksDone)
    {
        ducksDone = LiftDucks();
    }
#ifdef ATR_TUNE
    ReadTuning();
    ThemeStations();
    TimedLevelVoices();
    DrainEqView();
    ReverbView();
    static uint64_t nextOverlay = 0;
    if (GetTickCount64() >= nextOverlay)
    {
        nextOverlay = GetTickCount64() + 250;
        WriteOverlay();
    }
#else
    ThemeStations();
    LevelVoices();
    Muffle();
    ReadCars();
    OpenAir();
#endif
    OwnCar();
    return false;
}
} // namespace atr

using namespace atr;

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
#ifdef ATR_TUNE
        HookEqView(aHandle, aSdk);
        HookSendView(aHandle, aSdk);
#endif
        static RED4ext::v1::GameState state{
            .OnEnter = nullptr,
            .OnUpdate = OnUpdate,
            .OnExit = nullptr,
        };
        aSdk->gameStates->Add(aHandle, RED4ext::EGameStateType::Running, &state);
    }
    return true;
}
