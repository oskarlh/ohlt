#pragma once

#include "cli_option_defaults.h"
#include "compress.h"
#include "qrad.h"

namespace cli_option_defaults {
	constexpr bool blockOpaque = true;
	constexpr vector_type rgbTransferCompressType = vector_type::vector32;
	constexpr float_type transferCompressType = float_type::float16;
	constexpr vis_method visMethod = vis_method::sparse_vismatrix;
} // namespace cli_option_defaults
