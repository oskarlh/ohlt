#pragma once

#include "cli_option_defaults.h" // IWYU pragma: export
#include "compress.h"
#include "hlrad.h"

namespace cli_option_defaults {
	constexpr bool blockOpaque = true;
	constexpr vector_type rgbTransferCompressType = vector_type::vector32;
	constexpr float_type transferCompressType = float_type::float16;
	constexpr vis_method visMethod = vis_method::sparse_vismatrix;
} // namespace cli_option_defaults
