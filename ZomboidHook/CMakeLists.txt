if (WIN32)
    set(PLATFORM_FILES
            src/win64/APIHijacker.cpp include/win64/APIHijacker.h
            src/OSCallHandler.cpp include/OSCallHandler.h
            src/SQLite.cpp include/SQLite.h
            src/win64/DLLMain.cpp)
    set(PLATFORM_LINK
            Detours)
endif()

add_library(ZomboidHook SHARED
        src/win64/APIHijacker.cpp include/win64/APIHijacker.h
        src/OSCallHandler.cpp include/OSCallHandler.h
        src/SQLite.cpp include/SQLite.h
        src/win64/DLLMain.cpp)
target_link_libraries(ZomboidHook PRIVATE Detours sqlite)
target_compile_definitions(ZomboidHook PRIVATE
        "DLLEXPORT=__declspec(dllexport)"
        STDCALL=__stdcall)
target_compile_options(ZomboidHook PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:-Wall>)
target_include_directories(ZomboidHook PRIVATE include)
set_target_properties(ZomboidHook PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES)
set_target_properties(ZomboidHook PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES)
