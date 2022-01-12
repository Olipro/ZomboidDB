#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "interface/IFileOps.h"
#include "interface/IOSCallHandler.h"

#include <cstdint>
#include <memory>

namespace ZomboidHook {
	struct OSFunctions {
		decltype(::CreateFileW)* CreateFileW							 = ::CreateFileW;
		decltype(::DeleteFileW)* DeleteFileW							 = ::DeleteFileW;
		decltype(::ReadFile)* ReadFile										 = ::ReadFile;
		decltype(::WriteFile)* WriteFile									 = ::WriteFile;
		decltype(::GetFileSize)* GetFileSize							 = ::GetFileSize;
		decltype(::GetFileSizeEx)* GetFileSizeEx					 = ::GetFileSizeEx;
		decltype(::SetFilePointer)* SetFilePointer				 = ::SetFilePointer;
		decltype(::SetFilePointerEx)* SetFilePointerEx		 = ::SetFilePointerEx;
		decltype(::GetFileAttributesW)* GetFileAttributesW = ::GetFileAttributesW;
		decltype(::GetFileAttributesExW)* GetFileAttributesExW =
				::GetFileAttributesExW;
		decltype(::SetFileAttributesW)* SetFileAttributesW = ::SetFileAttributesW;
		decltype(::SetEndOfFile)* SetEndOfFile						 = ::SetEndOfFile;
		decltype(::SetFileInformationByHandle)* SetFileInformationByHandle =
				::SetFileInformationByHandle;
		decltype(::GetFileType)* GetFileType = ::GetFileType;
		decltype(::CloseHandle)* CloseHandle = ::CloseHandle;
		OSFunctions()												 = default;
	};
	class Detour;
	class ActiveHook {
		friend class ::ZomboidHook::Detour;
		std::unique_ptr<PVOID> real;
		PVOID fake;

		ActiveHook(PVOID real, PVOID fake);

	public:
		ActiveHook(ActiveHook&& rhs) noexcept;
		~ActiveHook();
	};
	class APIHijacker : public IFileOps {
		static APIHijacker instance;
		static HANDLE CreateFileW(LPCWSTR file,
															DWORD desiredAccess,
															DWORD shareMode,
															LPSECURITY_ATTRIBUTES secAttribs,
															DWORD creationDisposition,
															DWORD flagsAndAttributes,
															HANDLE templateFile);
		static BOOL DeleteFileW(LPCWSTR path);
		static BOOL ReadFile(HANDLE file,
												 LPVOID buffer,
												 DWORD numBytesToRead,
												 LPDWORD numBytesRead,
												 LPOVERLAPPED overlapped);
		static BOOL WriteFile(HANDLE file,
													LPCVOID buf,
													DWORD numBytesToWrite,
													LPDWORD numBytesWritten,
													LPOVERLAPPED overlapped);
		static DWORD GetFileSize(HANDLE file, LPDWORD fileSizeHigh);
		static BOOL GetFileSizeEx(HANDLE file, PLARGE_INTEGER fileSize);
		static DWORD SetFilePointer(HANDLE file,
																LONG distanceToMove,
																PLONG distanceToMoveHigh,
																DWORD moveMethod);
		static BOOL SetFilePointerEx(HANDLE file,
																 LARGE_INTEGER distanceToMove,
																 PLARGE_INTEGER newFilePointer,
																 DWORD moveMethod);
		static DWORD GetFileAttributesW(LPCWSTR fileName);
		static BOOL GetFileAttributesExW(LPCWSTR fileName,
																		 GET_FILEEX_INFO_LEVELS infoLevelId,
																		 LPVOID fileInformation);
		static BOOL SetFileAttributesW(LPCWSTR fileName, DWORD fileAttributes);
		static BOOL SetEndOfFile(HANDLE file);
		static BOOL SetFileInformationByHandle(
				HANDLE file,
				FILE_INFO_BY_HANDLE_CLASS fileInformationClass,
				LPVOID fileInformation,
				DWORD bufferSize);
		static DWORD GetFileType(HANDLE file);
		static BOOL CloseHandle(HANDLE handle);
		OSFunctions trampoline;
		std::unique_ptr<IOSCallHandler> oscHandler;
		std::vector<ActiveHook> activeHooks;

	public:
		static APIHijacker& Instance() noexcept;
		APIHijacker() noexcept;
		void RegisterHandler(std::unique_ptr<IOSCallHandler>&& oscHandler);

		bool FileExists(const std::filesystem::path& path) noexcept override;
		std::unique_ptr<IMemMappedFile>
				MemMapFile(const std::filesystem::path& path) override;
		FileTimes GetFileTimes(const std::filesystem::path& path) override;
		~APIHijacker();
	};
} // namespace ZomboidHook
