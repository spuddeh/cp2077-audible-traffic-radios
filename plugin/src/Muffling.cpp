// Muffled from inside a car, and by walls.

#include "Atr.hpp"

namespace atr
{
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
#ifdef ATR_TUNE
        char line[128];
        std::snprintf(line, sizeof(line), "muffle: veh_interior %.2f, mixer %+.1f dB, low-pass %.0f", interior, target,
                      g_muffleLpf * interior);
        Log(line);
#endif
    }
    else
    {
        *aStopped = true;
        Log("the NPC car radio mixer holds " + std::to_string(found) + " dB, not this plugin's - muffling stopped");
    }
}

// --- 7. muffled by walls ---
// The NPC receivers and their mixer read no occlusion parameter, so walls did not stop them; the world radio
// (radio_default_int) reads game_occlusion. Its two curves go onto the NPC mixer once, beside the volume write:
// volume 0 to -12 dB and low-pass 0 to 57. The attach is the parameter node's own SetRTPC virtual at slot 0x1c0
// (0x1adda50), the one a bank load calls: (object start, curve description, points), 1 on success.
constexpr uint32_t kRtpcGameOcclusion = 154512849;
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
    const auto [faded, fades] = AttachEqFades();
    const auto [sent, sends] = AttachReverbSends();
    const int own = mixer ? AttachOwnCarCurve(mixer) : 0;
    lockOff(critical);
    Log("open-air cars: " + std::to_string(faded) + " of " + std::to_string(fades) + " EQ fade curves attached");
    Log("open-air cars: " + std::to_string(sent) + " of " + std::to_string(sends) + " area reverb curves attached");
    Log(own == 1 ? "the player's own car follows the Car Radio slider" : "the own-car curve did not attach - the player's car plays at its traffic level");
    Log(a == 1 && b == 1 ? "walls muffle traffic radios: occlusion attached to the NPC car radio mixer"
                         : "the occlusion curves did not attach - walls do not muffle traffic radios");
}

} // namespace atr
