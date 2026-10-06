// Shared helpers: logging, addresses, and the loaded Wwise objects (index lookup, the NPC mixer).

#include "Atr.hpp"

namespace atr
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

// --- 3. the NPC car radio mixer's volume ---
// g_pIndex (0x339f7b8): audio nodes are its first table, buckets at +0x40, count at +0x48; a node sits in bucket
// id % count, chained through +0x8, id at +0x10. That pointer is 0x10 into the object, whose +0x08 holds the
// vtable every parameter node shares (0x2ee0798) and whose property bundle is at +0x88 (a count byte, the prop
// ids, the values from the next 4-byte boundary). CAkParameterNode::SetAkProp (0x1b07990) takes the object
// start and runs under Wwise's global lock (CAkFunctionCritical, 0x1af6d90 / 0x1af7100).
constexpr float kVanillaVolume = -8.0f;

// The range a car's radio plays in, in dB on the mixer. The mixer is set to the top; each voice is lowered from
// there by its own share of the range.
float g_levelTop = 9.0f;
float g_levelBottom = 0.0f;

// From inside a car: how far the mixer drops (dB) and how much low-pass it gets (0 to 100), at full veh_interior.
// The game's own values for traffic engines, tyres and horns (init.bnk, veh_interior at 1).
float g_muffleDb = -4.0f;
float g_muffleLpf = 25.0f;
// The mixer volume this plugin last wrote, which a later write must find there.
float g_mixerNow = std::numeric_limits<float>::quiet_NaN();


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
} // namespace atr
