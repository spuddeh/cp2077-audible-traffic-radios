cmake_minimum_required(VERSION 3.21)
project(AudibleTrafficRadios LANGUAGES CXX)

include(red4ext-plugin.cmake)

add_library(AudibleTrafficRadios SHARED src/Main.cpp src/Wwise.cpp src/Patches.cpp src/Levels.cpp src/Muffling.cpp src/OpenAir.cpp src/Stations.cpp src/Ducks.cpp src/Tuning.cpp)
red4ext_plugin(AudibleTrafficRadios)

# A tuning build reads atr_tune.txt beside the DLL to change the level range while the game runs. Never released.
option(ATR_TUNE "Read atr_tune.txt for the level range" OFF)
if(ATR_TUNE)
    target_compile_definitions(AudibleTrafficRadios PRIVATE ATR_TUNE)
endif()
