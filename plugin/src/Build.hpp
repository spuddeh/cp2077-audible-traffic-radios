// What differs between the game builds the plugin supports, one table per build.
//
// Every address the plugin reads or calls outside the two hash-resolved radio functions lives in a table. The
// plugin uses the table whose every byte check passes (`SelectBuild`); the game version is never consulted, so a
// hotfix that keeps the layout keeps working. The newest two builds are kept: a new build is a new table, checked
// site by site against its exe, and the oldest is removed at the same time.
//
// A table holds image offsets (RVAs). Wwise's own layouts (the index, parameter nodes, buses, curves) are the same
// on both builds; only where its code and globals sit moved.

#pragma once

#include <cstddef>
#include <cstdint>

namespace atr
{
struct Build
{
    const char* name;

    // --- Wwise functions ---
    uintptr_t setRtpc;                  // AK::SoundEngine::SetRTPCValue
    uintptr_t setRtpcByPlayingId;       // AK::SoundEngine::SetRTPCValueByPlayingID
    uintptr_t getRtpcValue;             // AK::SoundEngine::Query::GetRTPCValue
    uintptr_t gameObjectFromPlayingId;  // AK::SoundEngine::Query::GetGameObjectFromPlayingID
    uintptr_t registryGetObj;           // CAkRegistryMgr::GetObj
    uintptr_t criticalEnter;            // CAkFunctionCritical's constructor; +0x17 is lea rcx, [the lock]
    uintptr_t criticalLeave;            // CAkFunctionCritical's destructor
    uintptr_t setAkProp;                // CAkParameterNode::SetAkProp
    uintptr_t nodeAddRtpcCurve;         // CAkParameterNodeBase::AddRtpcCurve
    uintptr_t fxAddRtpcCurve;           // CAkFxBase::AddRtpcCurve
    uint8_t fxAddRtpcCurvePrologue[12];

    // --- Wwise globals and vtables ---
    uintptr_t index;          // g_pIndex
    uintptr_t registry;       // g_pRegistryMgr
    uintptr_t paramNodeVtbl;  // the vtable at +0x08 of every parameter node
    uintptr_t fxCustomVtbl;   // CAkFxCustom's, at +0x00 of the indexed pointer

    // --- the game's radio ---
    size_t rootAudioSystem;       // engine root -> audio system; +0xe0 of it is the radio manager
    uintptr_t trafficEmitterVtbl; // audio::TrafficVehicleEmitter

    // --- broken glass ---
    uintptr_t glassShattered;       // game::VehicleDestruction::Glass::IsShattered
    uintptr_t glassDestructionRead; // vehicle::BaseObject::OnGlassDestruction: mov r9, [rcx+vehicleDestruction]
    uint32_t vehicleDestruction;    // vehicle::BaseObject -> game::VehicleDestruction
    uintptr_t glassCountRead;       // the save code: cmp [rdx+0x2a4], ebx
    uintptr_t glassPanesRead;       // add rcx, [rdx+0x298]
    uintptr_t glassWindshieldRead;  // mov rcx, [rax+0x2a8]

    // --- the tuning build only ---
    uintptr_t eqSetParam;          // CAkParameterEQFXParams::SetParam
    uintptr_t ctxGetAuxSends;      // CAkBehavioralCtx::GetAuxSendsValues
    uintptr_t getAuxSends;         // AK::SoundEngine::Query::GetGameObjectAuxSendValues
    uintptr_t registerBusMeter;    // AK::SoundEngine::RegisterBusMeteringCallback
};

// The prologues every build shares, checked before a call.
constexpr uint8_t kSetRtpcPrologue[] = {0x48, 0x83, 0xEC, 0x48, 0x0F, 0xB6, 0x44, 0x24};
constexpr uint8_t kSetRtpcByPlayingIdPrologue[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57};
constexpr uint8_t kGetRtpcValuePrologue[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C};
constexpr uint8_t kGameObjectPrologue[] = {0x8B, 0xD1, 0x48, 0x8B, 0x0D};
constexpr uint8_t kGetObjPrologue[] = {0x44, 0x8B, 0x41, 0x30, 0x4C, 0x8B, 0xCA, 0x45, 0x85, 0xC0};
constexpr uint8_t kCriticalEnterPrologue[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B};
constexpr uint8_t kCriticalLeavePrologue[] = {0x48, 0x83, 0xEC, 0x28, 0xFF, 0x15};
constexpr uint8_t kSetAkPropPrologue[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83};
constexpr uint8_t kNodeAddRtpcCurvePrologue[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10};
constexpr uint8_t kGlassShatteredPrologue[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x55, 0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC};
constexpr uint8_t kGlassCountBytes[] = {0x39, 0x9A, 0xA4, 0x02, 0x00, 0x00};
constexpr uint8_t kGlassPanesBytes[] = {0x48, 0x03, 0x8A, 0x98, 0x02, 0x00, 0x00};
constexpr uint8_t kGlassWindshieldBytes[] = {0x48, 0x8B, 0x88, 0xA8, 0x02, 0x00, 0x00};
constexpr uint8_t kEqSetParamPrologue[] = {0x4C, 0x8B, 0xC9, 0x4D, 0x85, 0xC0, 0x75, 0x05};
constexpr uint8_t kCtxGetAuxSendsPrologue[] = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x55};
constexpr uint8_t kGetAuxSendsPrologue[] = {0x40, 0x53, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x40};
constexpr uint8_t kRegisterBusMeterPrologue[] = {0x48, 0x83, 0xEC, 0x38, 0x80, 0x3D};

// Steam and GOG 2.31.
constexpr Build kBuild231{
    .name = "2.31",
    .setRtpc = 0x1acf570,
    .setRtpcByPlayingId = 0x1acf6a0,
    .getRtpcValue = 0x1ad2c60,
    .gameObjectFromPlayingId = 0x1ad2680,
    .registryGetObj = 0x1b3b230,
    .criticalEnter = 0x1af6d90,
    .criticalLeave = 0x1af7100,
    .setAkProp = 0x1b07990,
    .nodeAddRtpcCurve = 0x1adda50,
    .fxAddRtpcCurve = 0x1b50b20,
    .fxAddRtpcCurvePrologue = {0x40, 0x55, 0x56, 0x48, 0x83, 0xEC, 0x38, 0x48, 0x8B, 0xF1, 0x48, 0xC7},
    .index = 0x339f7b8,
    .registry = 0x339f7b0,
    .paramNodeVtbl = 0x2ee0798,
    .fxCustomVtbl = 0x2f75ac0,
    .rootAudioSystem = 0xa8,
    .trafficEmitterVtbl = 0x2b458a0,
    .glassShattered = 0x273094,
    .glassDestructionRead = 0x25f84f6,
    .vehicleDestruction = 0x600,
    .glassCountRead = 0x2743cd,
    .glassPanesRead = 0x2743df,
    .glassWindshieldRead = 0x274437,
    .eqSetParam = 0x1ab4640,
    .ctxGetAuxSends = 0x1ad66a0,
    .getAuxSends = 0x1ad2470,
    .registerBusMeter = 0x1acb900,
};

// Steam 2.5.
constexpr Build kBuild25{
    .name = "2.5",
    .setRtpc = 0x1b37450,
    .setRtpcByPlayingId = 0x1b37580,
    .getRtpcValue = 0x1b3ab80,
    .gameObjectFromPlayingId = 0x1b3a560,
    .registryGetObj = 0x1ba3130,
    .criticalEnter = 0x1b5ed10,
    .criticalLeave = 0x1b5f080,
    .setAkProp = 0x1b6f4b0,
    .nodeAddRtpcCurve = 0x1b45d00,
    .fxAddRtpcCurve = 0x1bb9800,
    .fxAddRtpcCurvePrologue = {0x40, 0x55, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0xF1},
    .index = 0x39c5d00,
    .registry = 0x39c5d08,
    .paramNodeVtbl = 0x3489e90,
    .fxCustomVtbl = 0x351f558,
    .rootAudioSystem = 0xc8,
    .trafficEmitterVtbl = 0x3057728,
    .glassShattered = 0xe99808,
    .glassDestructionRead = 0x2b9587a,
    .vehicleDestruction = 0x620,
    .glassCountRead = 0x389cd9,
    .glassPanesRead = 0x389ceb,
    .glassWindshieldRead = 0x389d43,
    .eqSetParam = 0x1b1c3e0,
    .ctxGetAuxSends = 0x1b3e6b0,
    .getAuxSends = 0x1b3a350,
    .registerBusMeter = 0x1b33900,
};

constexpr const Build* kBuilds[] = {&kBuild25, &kBuild231};

// The build whose checks passed, or null: nothing that reads a table runs without one.
inline const Build* g_build = nullptr;
} // namespace atr
