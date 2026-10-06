cmake_minimum_required(VERSION 3.21)
project(AudibleTrafficRadios LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(AudibleTrafficRadios SHARED src/Main.cpp src/Wwise.cpp src/Patches.cpp src/Levels.cpp src/Muffling.cpp src/OpenAir.cpp src/Stations.cpp src/Tuning.cpp)

# RED4ext.SDK is header-only and lives in the read-only reference mirror. It is included, never
# built from there: nothing may write into _source.
target_include_directories(AudibleTrafficRadios PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../_source/RED4ext.SDK/include")

target_compile_definitions(AudibleTrafficRadios PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)

set_target_properties(AudibleTrafficRadios PROPERTIES
    OUTPUT_NAME "AudibleTrafficRadios"
    SUFFIX ".dll")

# A tuning build reads atr_tune.txt beside the DLL to change the level range while the game runs. Never released.
option(ATR_TUNE "Read atr_tune.txt for the level range" OFF)
if(ATR_TUNE)
    target_compile_definitions(AudibleTrafficRadios PRIVATE ATR_TUNE)
endif()
