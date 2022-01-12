cmake_minimum_required(VERSION 3.17)
project(ZomboidPatcher)

add_subdirectory(ext)
add_subdirectory(ZomboidHook)
add_subdirectory(ZomboidPatcher)

set_target_properties(Detours PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES)
