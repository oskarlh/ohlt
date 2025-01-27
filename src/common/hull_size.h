#pragma once

#include "mathtypes.h"

#include <array>

constexpr std::size_t NUM_HULLS = 4;

using hull_sizes = std::array<std::array<double3_array, 2>, NUM_HULLS>;
extern hull_sizes const standard_hull_sizes;
