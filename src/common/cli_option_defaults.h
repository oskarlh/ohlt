#pragma once

#include "color.h"
#include "developer_level.h"
#include "threads.h"

#include <cstddef>

namespace cli_option_defaults {
	constexpr bool chart = true;
	constexpr developer_level developer = developer_level::disabled;
	constexpr bool estimate = true;
	constexpr bool info = true;
	constexpr bool log = true;
	constexpr float_color_element minLight = 0.0f;
	constexpr bool nulltex = true;
	constexpr std::ptrdiff_t numberOfThreads = -1;
	constexpr bool verbose = false;
	constexpr q_threadpriority threadPriority
		= q_threadpriority::eThreadPriorityNormal;

	// This value is arbitrary
	constexpr std::size_t max_map_miptex = 0x200'0000;
} // namespace cli_option_defaults
