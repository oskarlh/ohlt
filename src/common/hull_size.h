#pragma once

#include "external_types/external_types.h"
#include "mathtypes.h"

#include <array>

using hull_sizes = std::array<std::array<double3_array, 2>, NUM_HULLS>;
extern hull_sizes const standard_hull_sizes;
