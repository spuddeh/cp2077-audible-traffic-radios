// Open-air traffic cars: reading the cars, openness, and the receiver EQ fades.

#include "Atr.hpp"

namespace atr
{
// --- 6. open-air cars ---
// Each traffic car with a radio voice gets an openness from 0 to 1: a weight per window down, door open, door torn
// off and shattered pane, added up, or 1 for a car with no side windows. Every 250 ms the car is read through RTTI
// (ScriptGameInstance.FindEntityByID on the emitter's entity id, then GetVehiclePS and GetDoorState /
// GetWindowState for seats 0 to 3); its record's hasSideWindows and player_audio_resource are read once. A car
// that left traffic (kind 3) has no entity id and stays closed.
//
// Openness is atr_open_air, a game parameter of this plugin's set on the car's Wwise game object, and the receiver
// EQs' treble shelves fade with it (section 8): the closed body's muffling lifts, the car's own sound stays.
constexpr uint32_t kNpcReceiverSounds[] = {882536694, 381815666, 234614324, 924785061,
                                           329334756, 937325872, 686441992};  // kNpcReceivers' sounds, in order
constexpr uint32_t kRtpcOpenAir = 1698419250;  // atr_open_air
constexpr int kSeatDoors = 4;         // EVehicleDoor seat_front_left .. seat_back_right
using AkGetObjFn = uintptr_t (*)(uintptr_t aRegistry, uint64_t aGameObject);
using AkGameObjectFn = uint64_t (*)(uint32_t aPlayingId);
using AkSetRtpcFn = int (*)(uint32_t aRtpc, float aValue, uint64_t aGameObject, int32_t aMs, int aCurve,
                           bool aBypass);

// What each opening adds to a car's openness.
float g_weightWindow = 0.25f;
float g_weightDoor = 0.5f;
float g_weightDetached = 0.75f;
float g_weightGlass = 0.25f;

bool g_forceOpen = false;  // tuning build: every traffic car open-air
#ifdef ATR_TUNE
// Tuning build only: cars read, found as vehicles and open, since the last perf line.
uint32_t g_carsRead = 0, g_carsFound = 0, g_carsOpen = 0;
#endif

struct Car
{
    float openness = 0.0f;
    int convertible = -1;  // not read yet
};
std::unordered_map<uint64_t, Car> g_cars;  // by entity id

// The openness this plugin set, by game object.
std::unordered_map<uint64_t, float> g_openSet;

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

// Broken glass: vehicle::BaseObject keeps its game::VehicleDestruction at +0x600, whose data (its first field)
// lists the panes at +0x298 (count +0x2a4, 0x30 bytes each) and the windshield at +0x2a8. Each pane answers
// game::VehicleDestruction::Glass::IsShattered (0x273094), the call the save code uses for brokenGlass. The saved
// brokenGlass itself is only written when the car is saved, so it is not read here.
using GlassShatteredFn = bool (*)(uintptr_t aGlass);

GlassShatteredFn GlassCheck()
{
    static const auto shattered =
        AtRva<GlassShatteredFn>(0x273094, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x55, 0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC});
    // The offsets as the game's own code uses them: OnGlassDestruction reads +0x600, the save code +0x2a4,
    // +0x298 and +0x2a8.
    static const bool offsets = AtRva<const uint8_t*>(0x25f84f6, {0x4C, 0x8B, 0x89, 0x00, 0x06, 0x00, 0x00}) &&
                                AtRva<const uint8_t*>(0x2743cd, {0x39, 0x9A, 0xA4, 0x02, 0x00, 0x00}) &&
                                AtRva<const uint8_t*>(0x2743df, {0x48, 0x03, 0x8A, 0x98, 0x02, 0x00, 0x00}) &&
                                AtRva<const uint8_t*>(0x274437, {0x48, 0x8B, 0x88, 0xA8, 0x02, 0x00, 0x00});
    return offsets ? shattered : nullptr;
}

// Shattered panes, windshield included.
uint32_t SafeGlassBroken(GlassShatteredFn aShattered, uintptr_t aVehicle)
{
    __try
    {
        const auto destruction = Read<uintptr_t>(aVehicle + 0x600);
        const auto data = destruction ? Read<uintptr_t>(destruction) : 0;
        if (!data)
        {
            return 0;
        }
        uint32_t broken = 0;
        const auto panes = Read<uintptr_t>(data + 0x298);
        const auto count = Read<uint32_t>(data + 0x2a4);
        for (uint32_t i = 0; panes && i < count && i < 32; ++i)
        {
            broken += aShattered(panes + i * 0x30) ? 1 : 0;
        }
        const auto windshield = Read<uintptr_t>(data + 0x2a8);
        return broken + (windshield && aShattered(windshield) ? 1 : 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

float CarOpenness(const CarRtti& aRtti, RED4ext::ScriptGameInstance& aGame, uint64_t aEntityId, Car& aCar)
{
    RED4ext::ent::EntityID id(aEntityId);
    RED4ext::Handle<RED4ext::IScriptable> entity;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &aGame);
    args.emplace_back(nullptr, &id);
    if (!RED4ext::ExecuteFunction(static_cast<void*>(nullptr), aRtti.find, &entity, args) || !entity ||
        !entity->GetType()->IsA(aRtti.vehicle))
    {
        return 0.0f;
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
        return 1.0f;
    }
    static const auto shattered = GlassCheck();
    float open = shattered ? g_weightGlass * SafeGlassBroken(shattered, reinterpret_cast<uintptr_t>(entity.instance))
                           : 0.0f;
    RED4ext::Handle<RED4ext::IScriptable> ps;
    if (RED4ext::ExecuteFunction(entity.instance, aRtti.ps, &ps) && ps)
    {
        for (int door = 0; door < kSeatDoors; ++door)
        {
            const uint8_t state = StateOf(ps.instance, aRtti.door, door);
            open += state == 2 ? g_weightDetached : state == 1 ? g_weightDoor : 0.0f;
            open += StateOf(ps.instance, aRtti.window, door) != 0 ? g_weightWindow : 0.0f;
        }
    }
    return std::fmin(1.0f, open);
}

#ifdef ATR_TUNE
float OpennessOf(const LiveVoice& aVoice);

// Tuning build only: each car with a radio voice as `<entity id> <level dB> <openness> <receiver>` in atr_live.txt, in the Radio
// Probe Overlay's folder when that dev mod is installed, which draws it on the car.
void WriteOverlay()
{
    static const std::wstring path = []
    {
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring dir(exe);
        dir = dir.substr(0, dir.find_last_of(L'\\') + 1) + L"plugins\\cyber_engine_tweaks\\mods\\RadioProbeOverlay\\";
        return GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES ? std::wstring() : dir + L"atr_live.txt";
    }();
    if (path.empty())
    {
        return;
    }
    FILE* f = _wfopen(path.c_str(), L"w");
    if (!f)
    {
        return;
    }
    std::set<uint64_t> written;
    for (const auto& [playingId, voice] : g_voices)
    {
        if (voice.car && written.insert(voice.key).second)
        {
            std::fprintf(f, "%llu %.1f %.2f %s\n", static_cast<unsigned long long>(voice.key), voice.levelDb,
                         OpennessOf(voice), kReceiverNames[voice.receiver]);
        }
    }
    std::fclose(f);
}
#endif

// Every 250 ms on the game thread: how open each car with a radio voice is.
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
        if (!GlassCheck())
        {
            Log("open-air cars: broken glass is not this game build's - doors, windows and convertibles only");
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
        car.openness = CarOpenness(rtti, game, key, car);
#ifdef ATR_TUNE
        ++g_carsRead;
        g_carsOpen += car.openness > 0.0f ? 1 : 0;
#endif
    }
}

float OpennessOf(const LiveVoice& aVoice)
{
    if (g_forceOpen)
    {
        return 1.0f;
    }
    const auto car = aVoice.car ? g_cars.find(aVoice.key) : g_cars.end();
    return car != g_cars.end() ? car->second.openness : 0.0f;
}

bool Differs(const std::unordered_map<uint64_t, float>& aSet, uint64_t aGameObject, float aValue)
{
    const auto have = aSet.find(aGameObject);
    return have == aSet.end() ? aValue > 0.0f : std::fabs(have->second - aValue) > 0.01f;
}

int SafeSetRtpc(AkSetRtpcFn aSet, uint32_t aRtpc, float aValue, uint64_t aGameObject)
{
    __try
    {
        return aSet(aRtpc, aValue, aGameObject, 250, kCurveLinear, false);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
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

// Every frame: Wwise's lock is tried only when a voice's game object is unknown or its car's openness changed.
void OpenAir()
{
    static const auto getObj =
        AtRva<AkGetObjFn>(0x1b3b230, {0x44, 0x8B, 0x41, 0x30, 0x4C, 0x8B, 0xCA, 0x45, 0x85, 0xC0});
    static const auto gameObject = AtRva<AkGameObjectFn>(0x1ad2680, {0x8B, 0xD1, 0x48, 0x8B, 0x0D});
    static const auto setRtpc = AtRva<AkSetRtpcFn>(0x1acf570, {0x48, 0x83, 0xEC, 0x48, 0x0F, 0xB6, 0x44, 0x24});
    static const auto lock = WwiseLock();
    static const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    static bool stopped = false;
    static uint64_t nextSweep = 0;
    if (stopped)
    {
        return;
    }
    if (!getObj || !gameObject || !setRtpc || !lock)
    {
        stopped = true;
        Log("open-air cars: the Wwise calls are not this game build's - every car stays closed");
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
        pending = voice.gameObject == ~0ull || Differs(g_openSet, voice.gameObject, OpennessOf(voice));
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
        const float open = OpennessOf(voice);
        if (Differs(g_openSet, voice.gameObject, open) &&
            SafeSetRtpc(setRtpc, kRtpcOpenAir, open, voice.gameObject) == 1)
        {
            g_openSet[voice.gameObject] = open;
        }
    }
    if (sweep)
    {
        // A game object that is gone takes its parameter value with it.
        const auto gone = [&](uint64_t aObject) {
            return !live.contains(aObject) && SafeObjectGone(getObj, registry, aObject);
        };
        std::erase_if(g_openSet, [&](const auto& aEntry) { return gone(aEntry.first); });
        nextSweep = now + 5000;
    }
    LeaveCriticalSection(lock);
}

// --- 8. the receiver EQs fade with openness ---
// Each NPC receiver's Parametric EQ mixes the car's own sound (its bass and mid bands) with the closed body (its
// treble shelf). Opening lifts only the treble shelf, to 0 dB at 1, so each car keeps CDPR's character for it; the
// output level takes off what the lift adds in loudness (pink noise, K-weighted) and adds kOpenRise, the same for
// every receiver, so the receivers keep their balance.
// An effect parameter takes the curve's value in place of its own setting (measured through SetParam: a closed
// car's band 3 gain arrived as 0, not -20), and a curve marked dB is converted again on the way. So every curve is
// exclusive with no scaling and carries the absolute value: the EQ's own setting at 0, the open value at 1, five
// points (gains and output linear in dB, Q in ratio). The attach is CAkFxBase's own (0x1b50b20, the
// one a bank load calls for an effect's RTPC): (effect, curve description, points), 1 on success, and it reaches
// effect instances already playing. Effects sit in g_pIndex's table 9, the indexed pointer being the object (vtable
// CAkFxCustom 0x2f75ac0). A Parametric EQ parameter is band * 5 + (0 type, 1 gain, 2 frequency, 3 Q, 4 on), and 15
// the output level.
struct EqFade
{
    uint32_t fx;
    uint8_t type[3];  // 3 notch, 4 low shelf, 5 high shelf, 6 peaking
    float gain[3];
    float q[3];
    bool on[3];
    float output;
    float louder;  // dB the treble lift adds in loudness, taken off the output level
};
constexpr EqFade kEqFades[] = {
    {828314749, {4, 3, 5}, {-5.5f, -24.0f, -24.0f}, {1.0f, 0.5f, 1.0f}, {true, true, true}, 4.0f, 6.9f},  // lowend
    {600002266, {4, 6, 5}, {6.5f, 6.0f, -24.0f}, {1.0f, 0.5f, 0.5f}, {true, true, true}, 3.0f, 5.9f},     // muscle
    {145669955, {6, 3, 5}, {8.0f, 3.5f, -20.0f}, {0.5f, 1.0f, 0.5f}, {true, true, true}, 0.0f, 2.4f},     // sports
    {869899093, {6, 3, 5}, {8.0f, 3.5f, -20.0f}, {0.5f, 1.0f, 0.5f}, {true, true, true}, 2.0f, 2.4f},     // suv
    {735462065, {6, 3, 5}, {8.0f, 3.5f, -20.0f}, {0.5f, 1.0f, 0.5f}, {true, true, true}, 2.0f, 2.4f},     // truck
    {947417206, {6, 3, 5}, {8.0f, 3.5f, -20.0f}, {0.5f, 1.0f, 0.5f}, {true, true, true}, 2.0f, 2.4f},     // hyper
    {364560772, {4, 6, 5}, {-24.0f, 0.0f, -24.0f}, {1.0f, 1.0f, 1.0f}, {true, false, true}, 0.0f, 1.0f},  // police
};
constexpr uint8_t kHighShelf = 5;
constexpr float kOpenRise = 3.0f;  // dB an open car gains over a closed one
constexpr uint32_t kFadePoints = 5;
using AkFxAttachRtpcFn = int (*)(uintptr_t aFx, const AkCurveDesc* aDesc, const AkCurvePoint* aPoints);

uintptr_t SafeFindFx(uint32_t aId)
{
    static const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    __try
    {
        const auto index = Read<uintptr_t>(base + 0x339f7b8);
        const auto table = index ? index + 9 * 0x58 : 0;
        const auto buckets = table ? Read<uintptr_t>(table + 0x40) : 0;
        const auto count = table ? Read<uint32_t>(table + 0x48) : 0;
        if (!buckets || !count)
        {
            return 0;
        }
        for (auto fx = Read<uintptr_t>(buckets + (aId % count) * 8); fx; fx = Read<uintptr_t>(fx + 0x8))
        {
            if (Read<uint32_t>(fx + 0x10) == aId)
            {
                return Read<uintptr_t>(fx) == base + 0x2f75ac0 ? fx : 0;
            }
        }
        return 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

int SafeFxAttach(AkFxAttachRtpcFn aAttach, uintptr_t aFx, const AkCurveDesc& aDesc, const AkCurvePoint* aPoints)
{
    __try
    {
        return aAttach(aFx, &aDesc, aPoints);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

// Called under Wwise's lock. Returns the curves attached and the curves tried.
std::pair<int, int> AttachEqFades()
{
    static const auto attach =
        AtRva<AkFxAttachRtpcFn>(0x1b50b20, {0x40, 0x55, 0x56, 0x48, 0x83, 0xEC, 0x38, 0x48, 0x8B, 0xF1, 0x48, 0xC7});
    int done = 0, tried = 0;
    uint32_t curveId = 0xA7710000;
    for (const auto& eq : kEqFades)
    {
        const uintptr_t fx = attach ? SafeFindFx(eq.fx) : 0;
        // aValue gives the curve's value at openness 0 to 1; five points keep the steps even.
        const auto add = [&](uint32_t aParameter, uint8_t aAccumulation, uint8_t aScaling, auto aValue) {
            AkCurvePoint points[kFadePoints];
            for (uint32_t i = 0; i < kFadePoints; ++i)
            {
                const float t = static_cast<float>(i) / (kFadePoints - 1);
                points[i] = {t, aValue(t), kCurveLinear};
            }
            const AkCurveDesc desc{0, aAccumulation, aScaling, 0, kRtpcOpenAir, aParameter, curveId++, kFadePoints};
            ++tried;
            done += fx && SafeFxAttach(attach, fx, desc, points) == 1 ? 1 : 0;
        };
        const auto from = [](float aClosed, float aOpen) { return [=](float aT) { return aClosed + (aOpen - aClosed) * aT; }; };
        for (uint32_t band = 0; band < 3; ++band)
        {
            if (eq.on[band] && eq.type[band] == kHighShelf)
            {
                add(band * 5 + 1, 1, 0, from(eq.gain[band], 0.0f));
            }
        }
        add(15, 1, 0, from(eq.output, eq.output - eq.louder + kOpenRise));
    }
    return {done, tried};
}

} // namespace atr
