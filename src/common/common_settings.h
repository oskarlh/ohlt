#pragma once
#include "developer_level.h"
#include "threads.h"

#include <cstdint>

struct common_settings final {
	bool chart = true;
	developer_level developer = developer_level::disabled;
	bool estimate = true;
	bool info = true;
	bool log = true;
	std::uint8_t minLight = 0;
	bool nulltex = true;
	std::ptrdiff_t numberOfThreads = -1;
	bool verbose = false;
	q_threadpriority threadPriority
		= q_threadpriority::eThreadPriorityNormal;

	// This value is arbitrary
	std::size_t max_map_miptex = 0x200'0000;
};
