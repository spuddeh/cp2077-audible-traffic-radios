// A level per car.

#include "Atr.hpp"

namespace atr
{
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

using AkSetRtpcByPlayingIdFn = int (*)(uint32_t aRtpc, float aValue, uint32_t aPlayingId, int32_t aMs, int aCurve,
                                       bool aBypass);

// Live NPC radio voices by playing id, with the round they were last seen in.
std::unordered_map<uint32_t, uint32_t> g_levelled;

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
    uint8_t kind;      // the listener kind: 2 traffic, 3 a car that left traffic
    uint64_t param;    // the emitter's +0x108, RadioEmitter::GetEntityId
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
                            aOut[(*aCount)++] = Voice{id, car ? car : id, static_cast<uint8_t>(receiver), car != 0,
                                                      kind, Read<uint64_t>(emitter + 0x108)};
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
            const float lower = span * (1.0f - ShareOf(voices[i].key));
            g_voices[voices[i].playingId] =
                LiveVoice{voices[i].key, voices[i].receiver, voices[i].car, ~0ull, g_levelTop + lower};
            // Bypass the parameter's own smoothing, which is built for a car's speed: the level applies at once.
            setRtpc(kRtpcEngageMovingFaster, CurveValueFor(lower), voices[i].playingId, 0, kCurveLinear, true);
#ifdef ATR_TUNE
            char line[192];
            std::snprintf(line, sizeof(line), "level: car %llx %s %+.1f dB kind %u param %llx",
                          static_cast<unsigned long long>(voices[i].key), kReceiverNames[voices[i].receiver],
                          g_levelTop + lower, voices[i].kind, static_cast<unsigned long long>(voices[i].param));
            Log(line);
#endif
        }
    }
    std::erase_if(g_levelled, [](const auto& aEntry) { return aEntry.second != g_round; });
    std::erase_if(g_voices, [](const auto& aEntry) { return !g_levelled.contains(aEntry.first); });
}
} // namespace atr
