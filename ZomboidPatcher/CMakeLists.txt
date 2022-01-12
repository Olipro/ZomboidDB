if (WIN32)
	set(PLATFORM_FILES
		src/win64/ZomboidPatcher.cpp)
	set(PLATFORM_LINK Detours)
endif()

	add_executable(ZomboidPatcher ${PLATFORM_FILES})
	target_link_libraries(ZomboidPatcher PRIVATE ${PLATFORM_LINK})
	set_target_properties(ZomboidPatcher PROPERTIES
		CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES)
