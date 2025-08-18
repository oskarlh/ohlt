#pragma once

// IWYU pragma: begin_exports
#include "texinfo.h"
// IWYU pragma: end_exports

#include <cstddef>
#include <cstdint>
#include <limits>

// This file contains types and contants defined by file formats or game
// engine limitations

// Hard limit
using hull_count = std::uint8_t;
constexpr hull_count NUM_HULLS = 4;
using hull_bitset = std::uint8_t;
static_assert(std::numeric_limits<hull_bitset>::digits >= NUM_HULLS);
constexpr hull_bitset all_hulls_bitset = (1 << NUM_HULLS) - 1;
