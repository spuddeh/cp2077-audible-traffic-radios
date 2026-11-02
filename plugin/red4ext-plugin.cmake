# Canonical: MyMods/_shared/plugin/red4ext-plugin.cmake. Copy it byte-identical into plugin/ and edit it
# here only; release-check fails a mod whose copy differs.
#
# red4ext_plugin(<target>) gives a RED4ext plugin the portfolio's build settings:
#   - RED4ext.SDK from RED4EXT_SDK_DIR, which plugin/build.ps1 sets to an export at the build's pinned commit
#   - C++20, UTF-8 sources, the dynamic CRT (the game installs the VC++ runtime)
#   - a PDB next to the DLL, recorded in the DLL by file name only (/PDBALTPATH), so a crash address
#     can be named later and no local path ships
#   - no incremental linking, and unreferenced or identical code removed (/OPT:REF, /OPT:ICF)

set(RED4EXT_SDK_DIR "" CACHE PATH "RED4ext.SDK checkout this DLL compiles against (set by plugin/build.ps1)")

function(red4ext_plugin target)
    if(NOT RED4EXT_SDK_DIR OR NOT EXISTS "${RED4EXT_SDK_DIR}/include/RED4ext/RED4ext.hpp")
        message(FATAL_ERROR "RED4EXT_SDK_DIR is not a RED4ext.SDK checkout: '${RED4EXT_SDK_DIR}'. Build with plugin/build.ps1.")
    endif()

    target_include_directories(${target} PRIVATE "${RED4EXT_SDK_DIR}/include")
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
    set_target_properties(${target} PROPERTIES
        CXX_EXTENSIONS OFF
        SUFFIX ".dll"
        MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /Zi)
        # The PDB's file name, spelled out: the Visual Studio generator escapes the linker's %_PDB% placeholder.
        target_link_options(${target} PRIVATE /DEBUG /OPT:REF /OPT:ICF /INCREMENTAL:NO
            "/PDBALTPATH:$<TARGET_PDB_FILE_NAME:${target}>")
    endif()
endfunction()
