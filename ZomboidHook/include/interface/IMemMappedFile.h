#pragma once

#include <cstdint>

namespace ZomboidHook {
	class IMemMappedFile {
	public:
		virtual uint8_t* data() noexcept = 0;
		virtual size_t size() noexcept	 = 0;
		virtual ~IMemMappedFile()				 = default;
	};
} // namespace ZomboidHook