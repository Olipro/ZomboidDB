#pragma once

#include <filesystem>
#include <memory>

#include "FileTimes.h"
#include "interface/IMemMappedFile.h"

namespace ZomboidHook {
	class IFileOps {
	public:
		virtual bool FileExists(const std::filesystem::path& path) noexcept = 0;
		virtual std::unique_ptr<IMemMappedFile>
				MemMapFile(const std::filesystem::path& path)									= 0;
		virtual FileTimes GetFileTimes(const std::filesystem::path& path) = 0;
		virtual ~IFileOps()																								= default;
	};
} // namespace ZomboidHook