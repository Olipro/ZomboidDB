#pragma once

#include <ctime>

namespace ZomboidHook {
	struct FileTimes {
		time_t creationTime;
		time_t lastModified;
		time_t lastAccessed;
	};
} // namespace ZomboidHook
