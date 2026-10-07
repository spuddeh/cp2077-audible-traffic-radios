// Tuning build only: the tuning file, the perf lines, the EQ view and the overlay feed.

#include "Atr.hpp"

namespace atr
{
#ifdef ATR_TUNE
// --- tuning build: EQ view ---
// Every gain, Q and output value a Parametric EQ receives: CAkParameterEQFXParams::SetParam (0x1ab4640: this, short
// id, const float*, size) is hooked. Ids: band * 5 + 1 gain (dB, clamped to +-24), + 3 Q, 15 output gain (dB). The
// audio thread only records; the game thread logs, with each block's band 1 and band 3 frequency to tell the
// receivers apart (fields at +0x10 + band * 20).
using EqSetParamFn = int (*)(void* aThis, int16_t aId, const void* aValue, uint32_t aSize);
EqSetParamFn g_eqSetParam = nullptr;
struct EqCall
{
    void* self;
    int16_t id;
    float value;
};
std::mutex g_eqMutex;
std::vector<EqCall> g_eqCalls;

int EqSetParamDetour(void* aThis, int16_t aId, const void* aValue, uint32_t aSize)
{
    if (aValue && aSize == sizeof(float) && (aId % 5 == 1 || aId % 5 == 3 || aId == 15))
    {
        std::lock_guard lock(g_eqMutex);
        if (g_eqCalls.size() < 4096)
        {
            g_eqCalls.push_back({aThis, aId, *static_cast<const float*>(aValue)});
        }
    }
    return g_eqSetParam(aThis, aId, aValue, aSize);
}

void HookEqView(RED4ext::v1::PluginHandle aHandle, const RED4ext::v1::Sdk* aSdk)
{
    const auto target = AtRva<void*>(0x1ab4640, {0x4C, 0x8B, 0xC9, 0x4D, 0x85, 0xC0, 0x75, 0x05});
    const bool hooked = target && aSdk->hooking->Attach(aHandle, target, reinterpret_cast<void*>(&EqSetParamDetour),
                                                        reinterpret_cast<void**>(&g_eqSetParam));
    Log(hooked ? "eq view: hooked" : "eq view: SetParam not this game build's - no EQ view");
}

void DrainEqView()
{
    std::vector<EqCall> calls;
    {
        std::lock_guard lock(g_eqMutex);
        calls.swap(g_eqCalls);
    }
    for (const auto& call : calls)
    {
        const auto block = reinterpret_cast<uintptr_t>(call.self);
        const char* what = call.id == 15 ? "output" : call.id % 5 == 1 ? "gain" : "Q";
        char line[160];
        std::snprintf(line, sizeof(line), "eq %p (f1 %.0f, f3 %.0f): band %d %s = %.3f", call.self,
                      Read<float>(block + 0x10), Read<float>(block + 0x10 + 2 * 20), call.id / 5 + 1, what, call.value);
        Log(line);
    }
}
#endif

#ifdef ATR_TUNE
// Tuning build only: atr_tune.txt beside the DLL, read every 2 s. Lines `levels <bottom dB> <top dB>` and
// `muffle <dB> <low-pass>`. A change writes the mixer again on the next frame; a levels change also levels every
// live voice again. `open 1` treats every traffic car as fully open, for comparing by ear; `weights <window>
// <door> <torn-off door> <pane>` sets what each opening adds to a car's openness.
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
    float window = g_weightWindow, door = g_weightDoor, detached = g_weightDetached, pane = g_weightGlass;
    char text[128];
    while (std::fgets(text, sizeof(text), f))
    {
        std::sscanf(text, " levels %f %f", &bottom, &top);
        std::sscanf(text, " muffle %f %f", &muffleDb, &muffleLpf);
        std::sscanf(text, " open %d", &open);
        std::sscanf(text, " weights %f %f %f %f", &window, &door, &detached, &pane);
    }
    std::fclose(f);
    if (window != g_weightWindow || door != g_weightDoor || detached != g_weightDetached || pane != g_weightGlass)
    {
        g_weightWindow = window;
        g_weightDoor = door;
        g_weightDetached = detached;
        g_weightGlass = pane;
        char weights[128];
        std::snprintf(weights, sizeof(weights), "tune: openness window %.2f, door %.2f, torn-off door %.2f, pane %.2f",
                      window, door, detached, pane);
        Log(weights);
    }
    if ((open != 0) != g_forceOpen)
    {
        g_forceOpen = open != 0;
        Log(g_forceOpen ? "tune: every traffic car fully open" : "tune: openness from doors, windows and glass");
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
    double part[3] = {};       // levels and muffling, reading the cars, open-air
    double partWorst[3] = {};
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
    LARGE_INTEGER t[4];
    QueryPerformanceCounter(&t[0]);
    LevelVoices();
    Muffle();
    QueryPerformanceCounter(&t[1]);
    ReadCars();
    QueryPerformanceCounter(&t[2]);
    OpenAir();
    QueryPerformanceCounter(&t[3]);
    for (int i = 0; i < 3; ++i)
    {
        const double part = static_cast<double>(t[i + 1].QuadPart - t[i].QuadPart) * toMicros;
        g_cost.part[i] += part;
        g_cost.partWorst[i] = std::fmax(g_cost.partWorst[i], part);
    }
    const double us = static_cast<double>(t[3].QuadPart - t[0].QuadPart) * toMicros;
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
        std::snprintf(line, sizeof(line),
                      "perf parts: levels+muffle %.1f / %.1f us, reading cars %.1f / %.1f us, open-air %.1f / %.1f us "
                      "(average / worst)",
                      g_cost.part[0] / g_cost.scans, g_cost.partWorst[0], g_cost.part[1] / g_cost.scans,
                      g_cost.partWorst[1], g_cost.part[2] / g_cost.scans, g_cost.partWorst[2]);
        Log(line);
        std::snprintf(line, sizeof(line), "cars: %u reads, %u found as vehicles, %u open", g_carsRead, g_carsFound,
                      g_carsOpen);
        Log(line);
        g_carsRead = g_carsFound = g_carsOpen = 0;
        std::string playing = "playing:";
        for (const auto& [playingId, voice] : g_voices)
        {
            char entry[64];
            std::snprintf(entry, sizeof(entry), " %llx %+.1f dB open %.2f;", static_cast<unsigned long long>(voice.key),
                          voice.levelDb, OpennessOf(voice));
            playing += entry;
        }
        Log(playing);
    }
    g_cost = ScanCost{};
    g_cost.next = now + 10000;
}
#endif

#ifdef ATR_TUNE
// --- the area reverb view ---
// Once a second: the game-defined aux sends of the playing traffic car with the most openness
// (AK::SoundEngine::Query::GetGameObjectAuxSendValues, 0x1ad2470: game object, AkAuxSendValue[] of 16 bytes -
// listener u64, aux bus u32, control value f32 - and an in/out count), and the level of every reverb bus seen so
// far, metered through AK::SoundEngine::RegisterBusMeteringCallback (0x1acb900; info +0x00 the cookie, +0x10 the
// metering with RMS at +0x10, the channel count in the low byte of +0x18). A reverb bus carries its reverb effect,
// so it calls back. One `reverb:` line a second.
using AkGetAuxSendsFn = int (*)(uint64_t aGameObject, void* aValues, uint32_t* aCount);
using AkRegisterMeterFn = int (*)(uint32_t aBus, void (*aCallback)(void*), uint32_t aFlags, void* aCookie);
constexpr uint32_t kMeterRms = 4;
constexpr size_t kReverbMeters = 16;

struct ReverbMeter
{
    uint32_t bus = 0;
    std::atomic<double> energy{0.0};
    std::atomic<uint32_t> calls{0};
};
ReverbMeter g_reverb[kReverbMeters];

void ReverbMeterCallback(void* aInfo)
{
    const auto info = reinterpret_cast<uintptr_t>(aInfo);
    const auto slot = reinterpret_cast<uintptr_t>(*reinterpret_cast<void**>(info));
    const auto metering = *reinterpret_cast<uintptr_t*>(info + 0x10);
    const auto channels = *reinterpret_cast<uint8_t*>(info + 0x18);
    if (slot >= kReverbMeters || !metering || !channels)
    {
        return;
    }
    const auto* rms = *reinterpret_cast<float**>(metering + 0x10);
    if (!rms)
    {
        return;
    }
    double power = 0.0;
    for (uint8_t i = 0; i < channels; ++i)
    {
        power += static_cast<double>(rms[i]) * rms[i];
    }
    g_reverb[slot].energy.fetch_add(power / channels);
    g_reverb[slot].calls.fetch_add(1);
}

int SafeGetAuxSends(AkGetAuxSendsFn aGet, uint64_t aObject, uint8_t* aValues, uint32_t* aCount)
{
    __try
    {
        return aGet(aObject, aValues, aCount);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        *aCount = 0;
        return -1;
    }
}

// The send level a voice actually uses: CAkBehavioralCtx::GetAuxSendsValues (0x1ad66a0) reads the context's
// game-defined aux send volume (dB) at +0xb0, used only when +0x12a bit 0 is set, and gains every game-defined send
// with it. The context's registered game object is at +0x08. The detour records the last value per game object.
using GetAuxSendsFn = void (*)(void* aCtx, void* aArray);
GetAuxSendsFn g_getAuxSends = nullptr;
std::mutex g_sendMutex;
struct SendParams
{
    float send;
    float block[16];  // ctx +0x88 onwards, the context's final values
};
std::unordered_map<uintptr_t, SendParams> g_sendDb;

void GetAuxSendsDetour(void* aCtx, void* aArray)
{
    const auto ctx = reinterpret_cast<uintptr_t>(aCtx);
    if (Read<uint8_t>(ctx + 0x12a) & 1)
    {
        std::lock_guard lock(g_sendMutex);
        if (g_sendDb.size() < 4096)
        {
            SendParams& p = g_sendDb[Read<uintptr_t>(ctx + 0x08)];
            p.send = Read<float>(ctx + 0xb0);
            std::memcpy(p.block, reinterpret_cast<const void*>(ctx + 0x88), sizeof(p.block));
        }
    }
    g_getAuxSends(aCtx, aArray);
}

void HookSendView(RED4ext::v1::PluginHandle aHandle, const RED4ext::v1::Sdk* aSdk)
{
    const auto target = AtRva<void*>(0x1ad66a0, {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x55});
    const bool hooked = target && aSdk->hooking->Attach(aHandle, target, reinterpret_cast<void*>(&GetAuxSendsDetour),
                                                        reinterpret_cast<void**>(&g_getAuxSends));
    Log(hooked ? "send view: hooked" : "send view: GetAuxSendsValues not this game build's - no send view");
}

void ReverbView()
{
    static const auto get = AtRva<AkGetAuxSendsFn>(0x1ad2470, {0x40, 0x53, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x40});
    static const auto reg = AtRva<AkRegisterMeterFn>(0x1acb900, {0x48, 0x83, 0xEC, 0x38, 0x80, 0x3D});
    static const auto lock = WwiseLock();
    using GetObjFn = uintptr_t (*)(uintptr_t aRegistry, uint64_t aGameObject);
    static const auto getObj =
        AtRva<GetObjFn>(0x1b3b230, {0x44, 0x8B, 0x41, 0x30, 0x4C, 0x8B, 0xCA, 0x45, 0x85, 0xC0});
    static uint64_t next = 0;
    const uint64_t now = GetTickCount64();
    if (!get || !reg || !lock || now < next || !TryEnterCriticalSection(lock))
    {
        return;
    }
    next = now + 1000;
    const LiveVoice* best = nullptr;
    float bestOpen = -1.0f;
    for (const auto& [id, voice] : g_voices)
    {
        const float open = OpennessOf(voice);
        if (voice.gameObject != ~0ull && open > bestOpen)
        {
            best = &voice;
            bestOpen = open;
        }
    }
    std::string line = "reverb:";
    if (best)
    {
        alignas(8) uint8_t values[16 * 8] = {};
        uint32_t count = 8;
        SafeGetAuxSends(get, best->gameObject, values, &count);
        char part[96];
        const uintptr_t registry = Read<uintptr_t>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + 0x339f7b0);
        const uintptr_t object = getObj && registry ? getObj(registry, best->gameObject) : 0;
        float sendDb = std::numeric_limits<float>::quiet_NaN();
        float block[16] = {};
        {
            std::lock_guard sendLock(g_sendMutex);
            const auto found = g_sendDb.find(object);
            if (object && found != g_sendDb.end())
            {
                sendDb = found->second.send;
                std::memcpy(block, found->second.block, sizeof(block));
            }
            g_sendDb.clear();
        }
        std::snprintf(part, sizeof(part), " car %llx %s open %.2f send %.1f dB,", static_cast<unsigned long long>(best->key),
                      kReceiverNames[best->receiver], bestOpen, sendDb);
        line += part;
        for (int i = 0; i < 16; ++i)
        {
            std::snprintf(part, sizeof(part), "%s%.2f", i ? " " : " ctx[", block[i]);
            line += part;
        }
        line += "] sends";
        for (uint32_t i = 0; i < count && i < 8; ++i)
        {
            uint32_t bus = 0;
            float control = 0.0f;
            std::memcpy(&bus, values + i * 16 + 8, 4);
            std::memcpy(&control, values + i * 16 + 12, 4);
            std::snprintf(part, sizeof(part), " %u=%.2f", bus, control);
            line += part;
            bool known = false;
            for (auto& m : g_reverb)
            {
                known = known || m.bus == bus;
            }
            for (size_t s = 0; !known && bus && s < kReverbMeters; ++s)
            {
                if (!g_reverb[s].bus)
                {
                    g_reverb[s].bus = bus;
                    reg(bus, ReverbMeterCallback, kMeterRms, reinterpret_cast<void*>(s));
                    known = true;
                }
            }
        }
    }
    LeaveCriticalSection(lock);
    line += " | levels";
    for (auto& m : g_reverb)
    {
        if (!m.bus)
        {
            continue;
        }
        const double energy = m.energy.exchange(0.0);
        const uint32_t calls = m.calls.exchange(0);
        char part[48];
        std::snprintf(part, sizeof(part), " %u=%.1f", m.bus,
                      calls && energy > 0.0 ? 10.0 * std::log10(energy / calls) : -120.0);
        line += part;
    }
    Log(line);
}
#endif

} // namespace atr
