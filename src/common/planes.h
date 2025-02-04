#pragma once

#include "mathtypes.h"

#include <array>

struct mapplane_t final {
	double3_array normal;
	double3_array origin;
	double dist; // Distance from the origin
	planetype type;
};
