#pragma once

#include <array>
#include <cstdint>

using float_color_element
	= float; // Values outside of [0, 1.0] should eventually be clamped
using float_rgb = std::array<float_color_element, 3>;
using int8_color_element = std::uint8_t; // [0, 255]
using int8_rgb = std::array<int8_color_element, 3>;

// For temporary variables during calculations - values outside [0, 255]
// should be clamped before being cast to int_color_element.
// Unless otherwise noted
using extended_int_color_element = std::int32_t;
