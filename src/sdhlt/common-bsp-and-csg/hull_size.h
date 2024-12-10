#pragma once

#include <array>

#include "mathtypes.h"

constexpr std::size_t NUM_HULLS = 4;

using hull_sizes = std::array<std::array<double3_array, 2>, NUM_HULLS>;
extern const hull_sizes standard_hull_sizes;
