// Stations on traffic cars.

#include "Atr.hpp"

namespace atr
{
// --- 9. stations on traffic cars ---
// A traffic car picks its station from its sound set's matchingStartupRadioStations (audioVehicleMetadata in
// base\sound\metadata\cooked_metadata.audio_metadata). The stations no shipped list carries go onto themed lists,
// the way the game themes Delamain (jazz) or the Villefort executives (Pacific Dreams); Samizdat stays off every
// list, as world radios skip it too. Six lists spell Morro Rock "radio_station 01_att_rock", which names no
// station, and are corrected. Lists are edited in place once the metadata is loaded; a list edited after load is
// the one traffic picks from. Police lists get the spelling fix and no themed stations.
constexpr const char* kSharedStations[] = {
    "radio_station_01_att_rock", "radio_station_02_aggro_ind",  "radio_station_03_elec_ind",
    "radio_station_04_hiphop",   "radio_station_05_pop",        "radio_station_07_aggro_techno",
    "radio_station_09_downtempo", "radio_station_10_latino",    "radio_station_11_metal",
};
struct StationTheme
{
    const char* station;
    bool sharedOnly;              // only lists that hold all of kSharedStations
    const char* sets[8];          // sound set name fragments; none means every list the first rule allows
};
constexpr StationTheme kThemes[] = {
    {"radio_station_12_growl_fm", true, {}},
    {"radio_station_14_impulse_fm", true,
     {"rayfield", "herrera", "porsche", "mizutani", "quadra_sport", "quadra_turbo", "kusanagi"}},
    {"radio_station_13_dark_star", true, {"tyger", "bandit", "scavenger", "misfit", "nomad"}},
    {"radio_station_08_jazz", false, {"villefort_cortes", "villefort_deleon", "limo"}},
};

void ThemeStations()
{
    static bool done = false;
    static uint64_t next = 0;
    const uint64_t now = GetTickCount64();
    if (done || now < next)
    {
        return;
    }
    next = now + 1000;
    auto* loader = RED4ext::ResourceLoader::Get();
    auto token = loader ? loader->FindToken(RED4ext::ResourcePath(R"(base\sound\metadata\cooked_metadata.audio_metadata)"))
                        : RED4ext::SharedPtr<RED4ext::ResourceToken<>>();
    if (!token || !token->IsLoaded() || !token->Get())
    {
        return;
    }
    done = true;
    auto* rtti = RED4ext::CRTTISystem::Get();
    auto* cooked = rtti->GetClass("audioCookedMetadataResource");
    auto* vehicle = rtti->GetClass("audioVehicleMetadata");
    auto* entriesProp = cooked ? cooked->GetProperty("entries") : nullptr;
    auto* nameProp = vehicle ? vehicle->GetProperty("name") : nullptr;
    auto* listProp = vehicle ? vehicle->GetProperty("matchingStartupRadioStations") : nullptr;
    if (!entriesProp || !nameProp || !listProp)
    {
        Log("traffic stations: the vehicle audio metadata is not this game build's - lists left as shipped");
        return;
    }
    const RED4ext::CName police("radio_station_police");
    const RED4ext::CName misspelt("radio_station 01_att_rock");
    const RED4ext::CName morroRock("radio_station_01_att_rock");
    uint32_t fixed = 0, added = 0, lists = 0;
    auto& entries = *entriesProp->GetValuePtr<RED4ext::DynArray<RED4ext::Handle<RED4ext::ISerializable>>>(
        token->Get().GetPtr());
    for (auto& entry : entries)
    {
        if (!entry || !entry->GetType()->IsA(vehicle))
        {
            continue;
        }
        auto& list = *listProp->GetValuePtr<RED4ext::DynArray<RED4ext::CName>>(entry.GetPtr());
        const auto holds = [&](RED4ext::CName aStation) {
            for (const auto& s : list)
            {
                if (s == aStation)
                {
                    return true;
                }
            }
            return false;
        };
        for (auto& s : list)
        {
            if (s == misspelt)
            {
                s = morroRock;
                ++fixed;
            }
        }
        if (holds(police))
        {
            continue;
        }
        ++lists;
        bool shared = true;
        for (const auto* s : kSharedStations)
        {
            shared = shared && holds(RED4ext::CName(s));
        }
        const std::string name = nameProp->GetValuePtr<RED4ext::CName>(entry.GetPtr())->ToString();
        for (const auto& theme : kThemes)
        {
            bool fits = !theme.sharedOnly || shared;
            if (fits && theme.sets[0])
            {
                fits = false;
                for (const auto* fragment : theme.sets)
                {
                    fits = fits || (fragment && name.find(fragment) != std::string::npos);
                }
            }
            const RED4ext::CName station(theme.station);
            if (fits && !holds(station))
            {
                list.PushBack(station);
                ++added;
            }
        }
    }
    Log("traffic stations: " + std::to_string(added) + " stations added across " + std::to_string(lists) +
        " lists, Morro Rock corrected on " + std::to_string(fixed));
}
} // namespace atr
