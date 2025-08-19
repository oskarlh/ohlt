#pragma once
#include <cstddef>

#define DEFAULT_PARSE                   false
#define DEFAULT_TEXTUREPARSE            false
#define DEFAULT_WRITEEXTENTFILE         false
#define DEFAULT_DELETEEMBEDDEDLIGHTMAPS false

namespace ripent_cli_option_defaults {
	constexpr bool deleteEmbeddedLightMaps = false;
	constexpr bool parse = false;
	constexpr bool textureParse = false;
	constexpr bool writeExtentFile = false;
} // namespace ripent_cli_option_defaults
