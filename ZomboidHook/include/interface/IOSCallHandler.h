#pragma once

#include <cstdint>
#include <filesystem>
#include <utility>

#include "FileTimes.h"

namespace ZomboidHook {
	enum class FileIntent
	{
		SUCCEED,
		FAIL,
		PASSTHRU,
	};
	enum class SeekFrom
	{
		BEGIN,
		CURRENT,
		END,
	};
	enum class FileAttribute
	{
		NORMAL,
		DIRECTORY,
		NOT_FOUND,
		PASSTHRU,
	};
	struct FileInfo {
		const std::filesystem::path& path;
		int64_t handle;
	};
	class IOSCallHandler {
	public:
		[[nodiscard]] virtual FileIntent FileOpenOnly(FileInfo info)				= 0;
		[[nodiscard]] virtual FileIntent FileCreateOnly(FileInfo info)			= 0;
		[[nodiscard]] virtual FileIntent FileOpenOrCreate(FileInfo info)		= 0;
		[[nodiscard]] virtual FileIntent FileCreateAndWipe(FileInfo info)		= 0;
		[[nodiscard]] virtual FileIntent FileOpenOnlyAndWipe(FileInfo info) = 0;
		[[nodiscard]] virtual FileIntent
				FileRead(FileInfo info, uint8_t* buf, uint32_t& readLen) = 0;
		[[nodiscard]] virtual FileIntent
				FileWrite(FileInfo info, const uint8_t* buf, uint32_t& writeLen) = 0;
		[[nodiscard]] virtual FileIntent
				FileSeek(FileInfo info, SeekFrom pos, int64_t& distance)					= 0;
		[[nodiscard]] virtual FileIntent FileTruncateToCursor(FileInfo)				= 0;
		[[nodiscard]] virtual FileIntent FileTruncate(FileInfo, uint64_t len) = 0;
		[[nodiscard]] virtual FileIntent
				FileDelete(const std::filesystem::path& path) = 0;
		virtual void FileClosed(FileInfo info)						= 0;
		// No trampoline functionality here other than faking success, failure or
		// passthru. For now.
		[[nodiscard]] virtual FileIntent
				FileSetAttrib(const std::filesystem::path& path) = 0;

		// These functions are called together. If the first wants to passthru, the
		// rest are not called, obviously, since we're doing a passthru.
		[[nodiscard]] virtual FileIntent FileGetSize(FileInfo info,
																								 uint64_t& sizeOut,
																								 bool isStateless = false) = 0;
		[[nodiscard]] virtual FileAttribute
				FileGetAttrib(const std::filesystem::path& path) = 0;
		[[nodiscard]] virtual FileTimes
				FileGetTimes(const std::filesystem::path& path) = 0;

		virtual ~IOSCallHandler() = default;
	};
} // namespace ZomboidHook