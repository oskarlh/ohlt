#pragma once

#include <cstddef>
#include "developer_level.h"

namespace cli_option_defaults {
	constexpr bool chart = true;
	constexpr developer_level_t developer = DEVELOPER_LEVEL_ALWAYS;
	constexpr bool estimate = true;
	constexpr bool info = true;
	constexpr bool log = true;
	constexpr bool nulltex = true;
	constexpr bool verbose = false;

	// These values are arbitrary
	constexpr std::size_t max_map_miptex = 0x2000000;
	constexpr std::size_t max_map_lightdata = 0x3000000;

}
