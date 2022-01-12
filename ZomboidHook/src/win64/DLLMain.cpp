#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "OSCallHandler.h"
#include "win64/APIHijacker.h"

using namespace ZomboidHook;

DLLEXPORT BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD fdwReason, LPVOID) {
	switch (fdwReason) {
		case DLL_PROCESS_ATTACH:
			DisableThreadLibraryCalls(hInstance);
			sqlite3_initialize();
			APIHijacker::Instance().RegisterHandler(
					std::make_unique<OSCallHandler>(APIHijacker::Instance()));
			break;
		case DLL_PROCESS_DETACH:
			sqlite3_shutdown();
			break;
	}
	return TRUE;
}
