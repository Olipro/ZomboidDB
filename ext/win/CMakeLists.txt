add_library(Detours OBJECT
        Detours/src/creatwth.cpp
        Detours/src/detours.cpp Detours/src/detours.h
        Detours/src/detver.h
        Detours/src/disasm.cpp
        Detours/src/disolarm.cpp
        Detours/src/disolarm64.cpp
        Detours/src/disolia64.cpp
        Detours/src/disolx64.cpp
        Detours/src/disolx86.cpp
        Detours/src/image.cpp
        Detours/src/modules.cpp)
target_compile_definitions(Detours PRIVATE
        WIN32_LEAN_AND_MEAN
        _WIN32_WINNT=0x501)
target_include_directories(Detours PUBLIC Detours/src)
