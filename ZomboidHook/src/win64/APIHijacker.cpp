#include "win64/APIHijacker.h"
#include "detours.h"

#include <cassert>
#include <stdexcept>
#include <unordered_map>

namespace fs = std::filesystem;
using namespace ZomboidHook;

APIHijacker APIHijacker::instance{};

// Magic values courtesy of Microsoft. Thanks guys, who cares about APIs and
// abstraction anyway?
static FILETIME TimetToFileTime(time_t t) {
	ULARGE_INTEGER fTime;
	fTime.QuadPart = (t * 10000000LL) + 116444736000000000LL;
	return {.dwLowDateTime = fTime.LowPart, .dwHighDateTime = fTime.HighPart};
}

static time_t FileTimeToTime(const FILETIME& t) {
	ULARGE_INTEGER tTime;
	tTime.QuadPart = static_cast<decltype(tTime.QuadPart)>(t.dwHighDateTime)
											 << 32 |
									 t.dwLowDateTime;
	return static_cast<time_t>((tTime.QuadPart - 116444736000000000LL) /
														 10000000LL);
}

class ReservedHandle { // We use CreateEvent() to obtain a unique handle to
											 // avoid conflicts.
protected:
	HANDLE handle = CreateEventA(nullptr, true, false, nullptr);

public:
	ReservedHandle() noexcept = default;
	explicit ReservedHandle(HANDLE handle) noexcept : handle{handle} {}
	ReservedHandle(const ReservedHandle&) = delete;
	ReservedHandle(ReservedHandle&& rhs) noexcept : handle{rhs.handle} {
		rhs.handle = nullptr;
	}

	bool operator==(const ReservedHandle& rhs) const noexcept {
		return handle == rhs.handle;
	}

	operator HANDLE() const noexcept {
		return handle;
	}

	operator int64_t() const noexcept {
		return reinterpret_cast<int64_t>(handle);
	}

	~ReservedHandle() {
		if (handle)
			CloseHandle(handle);
	}

	struct Hash {
		size_t operator()(const ReservedHandle& h) const noexcept {
			return reinterpret_cast<size_t>(static_cast<HANDLE>(h));
		}
	};
};

class UnownedHandle : public ReservedHandle {
public:
	UnownedHandle(HANDLE handle) : ReservedHandle{handle} {}
	~UnownedHandle() {
		handle = nullptr;
	}
};

class MemMappedFile : public IMemMappedFile {
	ReservedHandle handle;
	ReservedHandle fMap;
	uint8_t* buf;
	size_t len;

public:
	MemMappedFile(const fs::path& path, const OSFunctions& real) :
			handle{real.CreateFileW(path.c_str(),
															GENERIC_READ,
															FILE_SHARE_READ,
															nullptr,
															OPEN_EXISTING,
															0,
															nullptr)},
			fMap{CreateFileMappingW(handle, nullptr, PAGE_READONLY, 0, 0, nullptr)},
			buf{static_cast<uint8_t*>(MapViewOfFile(fMap, FILE_MAP_READ, 0, 0, 0))} {
		LARGE_INTEGER fSize;
		real.GetFileSizeEx(handle, &fSize);
		len = fSize.QuadPart;
	}

	uint8_t* data() noexcept override {
		return buf;
	}

	size_t size() noexcept override {
		return len;
	}

	~MemMappedFile() override {
		UnmapViewOfFile(buf);
	}
};

ActiveHook::ActiveHook(PVOID real, PVOID fake) :
		real{std::make_unique<PVOID>(real)}, fake{fake} {}

ActiveHook::ActiveHook(ActiveHook&& rhs) noexcept :
		real{std::move(rhs.real)}, fake{rhs.fake} {}

ActiveHook::~ActiveHook() {
	if (!real)
		return;
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	DetourDetach(real.get(), fake);
	DetourTransactionCommit();
}

class ZomboidHook::Detour {
public:
	Detour() {
		if (DetourTransactionBegin() != NO_ERROR) [[unlikely]]
			throw std::runtime_error{"Failed to start Detours Transaction"};
		DetourUpdateThread(GetCurrentThread());
	}

	template <typename T>
	[[nodiscard]] ActiveHook Hook(T*& real, T& fake) {
		ActiveHook hook{reinterpret_cast<PVOID>(real),
										reinterpret_cast<PVOID>(&fake)};
		if (DetourAttachEx(hook.real.get(),
											 hook.fake,
											 reinterpret_cast<PDETOUR_TRAMPOLINE*>(&real),
											 nullptr,
											 nullptr) != NO_ERROR) [[unlikely]]
			throw std::runtime_error{"DetourAttach failed."};
		return hook;
	}

	~Detour() noexcept(false) {
		if (DetourTransactionCommit() != NO_ERROR) [[unlikely]]
			throw std::runtime_error{"Failed to commit Detours Transaction"};
	}
};

APIHijacker& APIHijacker::Instance() noexcept {
	return instance;
}

APIHijacker::APIHijacker() noexcept = default;

void APIHijacker::RegisterHandler(
		std::unique_ptr<IOSCallHandler>&& newHandler) {
	assert(!oscHandler);
	oscHandler = std::move(newHandler);
	// Got a handler, apply detours.
	Detour d;
	activeHooks.emplace_back(d.Hook(trampoline.CreateFileW, CreateFileW));
	activeHooks.emplace_back(d.Hook(trampoline.DeleteFileW, DeleteFileW));
	activeHooks.emplace_back(d.Hook(trampoline.ReadFile, ReadFile));
	activeHooks.emplace_back(d.Hook(trampoline.WriteFile, WriteFile));
	activeHooks.emplace_back(d.Hook(trampoline.GetFileSize, GetFileSize));
	activeHooks.emplace_back(d.Hook(trampoline.GetFileSizeEx, GetFileSizeEx));
	activeHooks.emplace_back(d.Hook(trampoline.SetFilePointer, SetFilePointer));
	activeHooks.emplace_back(
			d.Hook(trampoline.SetFilePointerEx, SetFilePointerEx));
	activeHooks.emplace_back(
			d.Hook(trampoline.GetFileAttributesW, GetFileAttributesW));
	activeHooks.emplace_back(
			d.Hook(trampoline.GetFileAttributesExW, GetFileAttributesExW));
	activeHooks.emplace_back(
			d.Hook(trampoline.SetFileAttributesW, SetFileAttributesW));
	activeHooks.emplace_back(d.Hook(trampoline.SetEndOfFile, SetEndOfFile));
	activeHooks.emplace_back(d.Hook(trampoline.SetFileInformationByHandle,
																	SetFileInformationByHandle));
	activeHooks.emplace_back(d.Hook(trampoline.GetFileType, GetFileType));
	activeHooks.emplace_back(d.Hook(trampoline.CloseHandle, CloseHandle));
}

bool APIHijacker::FileExists(const std::filesystem::path& path) noexcept {
	return trampoline.GetFileAttributesW(path.wstring().c_str()) !=
				 INVALID_FILE_ATTRIBUTES;
}

std::unique_ptr<IMemMappedFile>
		APIHijacker::MemMapFile(const std::filesystem::path& path) {
	return std::make_unique<MemMappedFile>(path, trampoline);
}

FileTimes APIHijacker::GetFileTimes(const fs::path& path) {
	WIN32_FILE_ATTRIBUTE_DATA data;
	trampoline.GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data);
	auto accessTime = FileTimeToTime(data.ftLastAccessTime);
	auto writeTime	= FileTimeToTime(data.ftLastWriteTime);
	auto createTime = FileTimeToTime(data.ftCreationTime);
	return {.creationTime = createTime,
					.lastModified = writeTime,
					.lastAccessed = accessTime};
}

static std::unordered_map<ReservedHandle, fs::path, ReservedHandle::Hash>
		reservedHandles;

HANDLE APIHijacker::CreateFileW(LPCWSTR file,
																DWORD desiredAccess,
																DWORD shareMode,
																LPSECURITY_ATTRIBUTES secAttribs,
																DWORD creationDisposition,
																DWORD flagsAndAttributes,
																HANDLE templateFile) {
	auto intent = FileIntent::PASSTHRU;
	ReservedHandle rh;
	switch (creationDisposition) {
		case CREATE_ALWAYS:
			intent = instance.oscHandler->FileCreateAndWipe({file, rh});
			break;
		case CREATE_NEW:
			intent = instance.oscHandler->FileCreateOnly({file, rh});
			break;
		case OPEN_ALWAYS:
			intent = instance.oscHandler->FileOpenOrCreate({file, rh});
			break;
		case OPEN_EXISTING:
			intent = instance.oscHandler->FileOpenOnly({file, rh});
			break;
		case TRUNCATE_EXISTING:
			intent = instance.oscHandler->FileOpenOnlyAndWipe({file, rh});
		default:
			break; // Clearly someone screwed up their API call.
	}
	switch (intent) {
		case FileIntent::SUCCEED:
			return reservedHandles.emplace(std::move(rh), file).first->first;
		case FileIntent::FAIL:
			SetLastError(creationDisposition == CREATE_NEW ? ERROR_FILE_EXISTS
																										 : ERROR_FILE_NOT_FOUND);
			return INVALID_HANDLE_VALUE;
		case FileIntent::PASSTHRU:
			break;
	}
	return instance.trampoline.CreateFileW(file,
																				 desiredAccess,
																				 shareMode,
																				 secAttribs,
																				 creationDisposition,
																				 flagsAndAttributes,
																				 templateFile);
}

BOOL APIHijacker::DeleteFileW(LPCWSTR path) {
	switch (instance.oscHandler->FileDelete(path)) {
		case FileIntent::SUCCEED:
			return TRUE;
		case FileIntent::FAIL:
			SetLastError(ERROR_FILE_NOT_FOUND);
			return FALSE;
		case FileIntent::PASSTHRU:
			break;
	}
	return instance.trampoline.DeleteFileW(path);
}

static auto FindHandle(UnownedHandle file) {
	return reservedHandles.find(file);
}

BOOL APIHijacker::ReadFile(HANDLE file,
													 LPVOID buffer,
													 DWORD numBytesToRead,
													 PDWORD numBytesRead,
													 LPOVERLAPPED overlapped) {
	if (auto iter = FindHandle(file); iter != reservedHandles.end()) {
		uint32_t bytesToRead = numBytesToRead;
		auto intent = instance.oscHandler->FileRead({iter->second, iter->first},
																								static_cast<uint8_t*>(buffer),
																								bytesToRead);
		switch (intent) {
			case FileIntent::SUCCEED:
				if (numBytesRead)
					*numBytesRead = bytesToRead;
				return TRUE;
			case FileIntent::FAIL:
				SetLastError(ERROR_INVALID_USER_BUFFER);
				return FALSE;
			case FileIntent::PASSTHRU:
				break;
		}
	}
	return instance.trampoline.ReadFile(file,
																			buffer,
																			numBytesToRead,
																			numBytesRead,
																			overlapped);
}

BOOL APIHijacker::WriteFile(HANDLE file,
														LPCVOID buf,
														DWORD numBytesToWrite,
														LPDWORD numBytesWritten,
														LPOVERLAPPED overlapped) {
	if (auto iter = FindHandle(file); iter != reservedHandles.end()) {
		uint32_t bytesToWrite = numBytesToWrite;
		auto intent =
				instance.oscHandler->FileWrite({iter->second, iter->first},
																			 static_cast<const uint8_t*>(buf),
																			 bytesToWrite);
		switch (intent) {
			case FileIntent::SUCCEED:
				if (numBytesWritten)
					*numBytesWritten = bytesToWrite;
				return TRUE;
			case FileIntent::FAIL:
				SetLastError(ERROR_INVALID_USER_BUFFER);
				return FALSE;
			case FileIntent::PASSTHRU:
				break;
		}
	}
	return instance.trampoline.WriteFile(file,
																			 buf,
																			 numBytesToWrite,
																			 numBytesWritten,
																			 overlapped);
}

DWORD APIHijacker::GetFileSize(HANDLE file, LPDWORD fileSizeHigh) {
	if (auto iter = FindHandle(file); iter != reservedHandles.end()) {
		uint64_t sizeOut;
		auto intent =
				instance.oscHandler->FileGetSize({iter->second, iter->first}, sizeOut);
		switch (intent) {
			case FileIntent::SUCCEED:
				if (fileSizeHigh)
					*fileSizeHigh = (sizeOut & 0xFFFFFFFF00000000ull) >> 32;
				return sizeOut & 0xFFFFFFFFul;
			case FileIntent::FAIL:
				if (fileSizeHigh)
					*fileSizeHigh = 0;
				return INVALID_FILE_SIZE;
			case FileIntent::PASSTHRU:
				break;
		}
	}
	return instance.trampoline.GetFileSize(file, fileSizeHigh);
}

BOOL APIHijacker::GetFileSizeEx(HANDLE file, PLARGE_INTEGER fileSize) {
	if (auto iter = FindHandle(file); iter != reservedHandles.end()) {
		uint64_t sizeOut;
		auto intent =
				instance.oscHandler->FileGetSize({iter->second, iter->first}, sizeOut);
		switch (intent) {
			case FileIntent::SUCCEED:
				fileSize->QuadPart = static_cast<decltype(fileSize->QuadPart)>(sizeOut);
				return TRUE;
			case FileIntent::FAIL:
				fileSize->QuadPart = 0;
				return FALSE;
			case FileIntent::PASSTHRU:
				break;
		}
	}
	return instance.trampoline.GetFileSizeEx(file, fileSize);
}

DWORD APIHijacker::SetFilePointer(HANDLE file,
																	LONG distanceToMove,
																	PLONG distanceToMoveHigh,
																	DWORD moveMethod) {
	if (auto iter = FindHandle(file); iter != reservedHandles.end()) {
		int64_t distance = distanceToMoveHigh ? *distanceToMoveHigh : 0;
		distance <<= 32;
		distance |= distanceToMove;
		auto from		= moveMethod == FILE_BEGIN		 ? SeekFrom::BEGIN
									: moveMethod == FILE_CURRENT ? SeekFrom::CURRENT
																							 : SeekFrom::END;
		auto intent = instance.oscHandler->FileSeek({iter->second, iter->first},
																								from,
																								distance);
		switch (intent) {
			case FileIntent::SUCCEED:
				if (distanceToMoveHigh)
					*distanceToMoveHigh = static_cast<LONG>(distance >> 32);
				return distance & 0xFFFFFFFF;
			case FileIntent::FAIL:
				if (distanceToMoveHigh)
					*distanceToMoveHigh = 0;
				return INVALID_SET_FILE_POINTER;
			case FileIntent::PASSTHRU:
				break;
		}
	}
	return instance.trampoline.SetFilePointer(file,
																						distanceToMove,
																						distanceToMoveHigh,
																						moveMethod);
}

BOOL APIHijacker::SetFilePointerEx(HANDLE file,
																	 LARGE_INTEGER distanceToMove,
																	 PLARGE_INTEGER newFilePointer,
																	 DWORD moveMethod) {
	if (auto iter = FindHandle(file); iter != reservedHandles.end()) {
		int64_t distance = distanceToMove.QuadPart;
		auto from				 = moveMethod == FILE_BEGIN			? SeekFrom::BEGIN
											 : moveMethod == FILE_CURRENT ? SeekFrom::CURRENT
																										: SeekFrom::END;
		auto intent = instance.oscHandler->FileSeek({iter->second, iter->first},
																								from,
																								distance);
		switch (intent) {
			case FileIntent::SUCCEED:
				if (newFilePointer)
					newFilePointer->QuadPart = distance;
				return TRUE;
			case FileIntent::FAIL:
				return FALSE;
			case FileIntent::PASSTHRU:
				break;
		}
	}
	return instance.trampoline.SetFilePointerEx(file,
																							distanceToMove,
																							newFilePointer,
																							moveMethod);
}

DWORD APIHijacker::GetFileAttributesW(LPCWSTR fileName) {
	switch (instance.oscHandler->FileGetAttrib(fileName)) {
		case FileAttribute::NORMAL:
			return FILE_ATTRIBUTE_NORMAL;
		case FileAttribute::DIRECTORY:
			return FILE_ATTRIBUTE_DIRECTORY;
		case FileAttribute::NOT_FOUND:
			return INVALID_FILE_ATTRIBUTES;
		case FileAttribute::PASSTHRU:
			break;
	}
	return instance.trampoline.GetFileAttributesW(fileName);
}

BOOL APIHijacker::GetFileAttributesExW(LPCWSTR fileName,
																			 GET_FILEEX_INFO_LEVELS infoLevelId,
																			 LPVOID fileInformation) {
	assert(infoLevelId == GetFileExInfoStandard);
	fs::path path		 = fileName;
	auto& attribData = *static_cast<WIN32_FILE_ATTRIBUTE_DATA*>(fileInformation);
	uint64_t fSize;
	switch (instance.oscHandler->FileGetSize({path, 0}, fSize, true)) {
		case FileIntent::SUCCEED:
			attribData.nFileSizeLow	 = fSize & 0xFFFFFFFF;
			attribData.nFileSizeHigh = fSize >> 32;
			break;
		case FileIntent::FAIL:
			SetLastError(ERROR_FILE_NOT_FOUND);
			return FALSE;
		case FileIntent::PASSTHRU:
			return instance.trampoline.GetFileAttributesExW(fileName,
																											infoLevelId,
																											fileInformation);
	}
	attribData.dwFileAttributes = GetFileAttributesW(fileName);
	auto fileTimes							= instance.oscHandler->FileGetTimes(path);
	attribData.ftCreationTime		= TimetToFileTime(fileTimes.creationTime);
	attribData.ftLastAccessTime = TimetToFileTime(fileTimes.lastAccessed);
	attribData.ftLastWriteTime	= TimetToFileTime(fileTimes.lastModified);
	return TRUE;
}

BOOL APIHijacker::SetFileAttributesW(LPCWSTR fileName, DWORD fileAttributes) {
	switch (instance.oscHandler->FileSetAttrib(fileName)) {
		case FileIntent::SUCCEED:
			return TRUE;
		case FileIntent::FAIL:
			return FALSE;
		case FileIntent::PASSTHRU:
			break;
	}
	return instance.trampoline.SetFileAttributesW(fileName, fileAttributes);
}

BOOL APIHijacker::SetEndOfFile(HANDLE file) {
	if (auto iter = FindHandle(file); iter != reservedHandles.end()) {
		switch (instance.oscHandler->FileTruncateToCursor(
				{iter->second, iter->first})) {
			case FileIntent::SUCCEED:
				return TRUE;
			case FileIntent::FAIL:
				return FALSE;
			case FileIntent::PASSTHRU:
				break;
		}
	}
	return instance.trampoline.SetEndOfFile(file);
}

BOOL APIHijacker::SetFileInformationByHandle(
		HANDLE file,
		FILE_INFO_BY_HANDLE_CLASS fileInformationClass,
		LPVOID fileInformation,
		DWORD bufferSize) {
	if (auto iter = FindHandle(file); iter != reservedHandles.end()) {
		if (fileInformationClass == FileEndOfFileInfo) {
			auto& data = *static_cast<FILE_END_OF_FILE_INFO*>(fileInformation);
			switch (instance.oscHandler->FileTruncate({iter->second, iter->first},
																								data.EndOfFile.QuadPart)) {
				case FileIntent::SUCCEED:
					return TRUE;
				case FileIntent::FAIL:
					return FALSE;
				case FileIntent::PASSTHRU:
					break;
			}
		} else {
			return TRUE; // Don't care about other attribs when intercepting, so just
									 // lie to the caller.
		}
	}
	return instance.trampoline.SetFileInformationByHandle(file,
																												fileInformationClass,
																												fileInformation,
																												bufferSize);
}

DWORD APIHijacker::GetFileType(HANDLE file) {
	// We only care about pretending to handle disk files, no need to dispatch.
	return FindHandle(file) != reservedHandles.end()
						 ? FILE_TYPE_DISK
						 : instance.trampoline.GetFileType(file);
}

BOOL APIHijacker::CloseHandle(HANDLE handle) {
	auto ref = FindHandle(handle);
	if (ref == reservedHandles.end()) [[likely]]
		return instance.trampoline.CloseHandle(handle);
	instance.oscHandler->FileClosed({ref->second, ref->first});
	reservedHandles.erase(ref);
	return TRUE;
}

APIHijacker::~APIHijacker() {}
