#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "detours.h"

#include <iostream>

int wmain(int argc, const wchar_t* const argv[]) {
	if (argc != 2) {
		std::cout
				<< "wrong arg count, first arg should be path to Zomboid x64 EXE\n";
		return 1;
	}
	auto f = CreateFileW(argv[1],
											 GENERIC_READ | GENERIC_WRITE,
											 0,
											 nullptr,
											 OPEN_EXISTING,
											 FILE_ATTRIBUTE_NORMAL,
											 nullptr);
	if (f == INVALID_HANDLE_VALUE) {
		std::cout << "failed to open file ";
		std::wcout << argv[1];
		std::cout << '\n';
		return 2;
	}
	auto df = DetourBinaryOpen(f);
	if (df == nullptr) {
		std::cout << "DetourBinaryOpen failed\n";
		return 3;
	}
	bool allDone = false;
	DetourBinaryEditImports(
			df,
			&allDone,
			[](auto allDonePtr, auto, auto ppszOutFile) {
				auto& t = *static_cast<decltype(allDone)*>(allDonePtr);
				if (!t)
					*ppszOutFile = "ZomboidHook.dll";
				t = true;
				return TRUE;
			},
			nullptr,
			nullptr,
			nullptr);
	DetourBinaryWrite(df, f);
	DetourBinaryClose(df);
	CloseHandle(f);
	std::cout << "Done\n";
	return 0;
}
