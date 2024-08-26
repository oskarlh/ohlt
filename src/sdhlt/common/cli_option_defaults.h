#include <cstddef>
namespace cli_option_defaults {
	constexpr bool chart = true;
	constexpr bool estimate = true;
	constexpr bool info = true;
	constexpr bool nulltex = true;

	// These are arbitrary
	constexpr std::size_t max_map_miptex = 0x2000000;
	constexpr std::size_t max_map_lightdata = 0x3000000;

}
