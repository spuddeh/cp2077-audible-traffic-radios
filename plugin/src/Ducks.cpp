// Combat and police music leave traffic radios playing.

#include "Atr.hpp"

namespace atr
{
// --- 8. the NPC radio bus duck ---
// init.bnk ducks Music_Diagetic_Radios_Vehicle_NPC by -96 dB while combat or police music plays. Each ducking
// bus keeps its duck list at +0x110 (count +0x134); an entry chains through +0 and holds the target bus at +0x08,
// volume +0x0c, property +0x1c. Buses are the Wwise index's second table (0x58 past the first). Only the entry
// aimed at the NPC bus is written, so the player radio's entries on the same buses stay as RadioXL or the game
// set them. The quest-driven ducks on the NPC bus are left alone. A write while a duck is active applies from
// the next duck.
constexpr uint32_t kNpcRadioBus = 194813043;  // Music_Diagetic_Radios_Vehicle_NPC
constexpr uint32_t kDuckers[] = {
    2791646749,  // Music_Systemic_Combat
    318512183,   // Music_Systemic_Police
};
constexpr float kDuckVolume = -96.0f;

enum class Duck
{
    NotLoaded,
    Lifted,
    Unexpected,
};

Duck SafeLiftDuck(uint32_t aDucker)
{
    static const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    __try
    {
        const auto index = Read<uintptr_t>(base + 0x339f7b8);
        const auto buckets = index ? Read<uintptr_t>(index + 0x58 + 0x40) : 0;
        const auto count = index ? Read<uint32_t>(index + 0x58 + 0x48) : 0;
        if (!buckets || !count)
        {
            return Duck::NotLoaded;
        }
        uintptr_t bus = 0;
        for (auto node = Read<uintptr_t>(buckets + (aDucker % count) * 8); node; node = Read<uintptr_t>(node + 0x8))
        {
            if (Read<uint32_t>(node + 0x10) == aDucker)
            {
                bus = node - 0x10;
                break;
            }
        }
        if (!bus)
        {
            return Duck::NotLoaded;
        }
        const auto entries = Read<uint32_t>(bus + 0x134);
        uintptr_t entry = Read<uintptr_t>(bus + 0x110);
        for (uint32_t i = 0; entry && i < entries && i < 16; ++i, entry = Read<uintptr_t>(entry))
        {
            if (Read<uint32_t>(entry + 0x08) != kNpcRadioBus)
            {
                continue;
            }
            const float now = Read<float>(entry + 0x0c);
            if (Read<uint32_t>(entry + 0x1c) != 0 || (now != kDuckVolume && now != 0.0f))
            {
                return Duck::Unexpected;
            }
            *reinterpret_cast<float*>(entry + 0x0c) = 0.0f;
            return Duck::Lifted;
        }
        return Duck::Unexpected;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return Duck::Unexpected;
    }
}

// Tried each frame until both ducking buses are loaded; init.bnk stays loaded for the session, so one write holds.
bool LiftDucks()
{
    static const auto lock = WwiseLock();
    static bool done[std::size(kDuckers)] = {};
    if (!lock)
    {
        Log("the combat and police music duck is not this game build's - traffic radios still go quiet under it");
        return true;
    }
    if (!TryEnterCriticalSection(lock))
    {
        return false;
    }
    bool all = true;
    for (size_t i = 0; i < std::size(kDuckers); ++i)
    {
        if (done[i])
        {
            continue;
        }
        switch (SafeLiftDuck(kDuckers[i]))
        {
        case Duck::Lifted:
            done[i] = true;
            Log(std::string(i == 0 ? "combat" : "police") + " music no longer ducks traffic radios");
            break;
        case Duck::Unexpected:
            done[i] = true;
            Log(std::string(i == 0 ? "combat" : "police") +
                " music's duck on traffic radios is not the game's - left as it is");
            break;
        case Duck::NotLoaded:
            all = false;
            break;
        }
    }
    LeaveCriticalSection(lock);
    return all;
}
} // namespace atr
