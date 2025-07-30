#pragma once

// IWYU pragma: begin_exports
#include "texinfo.h"
// IWYU pragma: end_exports

#include <cstddef>
#include <cstdint>

// This file contains types and contants defined by file formats or game
// engine limitations

// Hard limit
using hull_count = std::uint8_t;
constexpr hull_count NUM_HULLS = 4;
