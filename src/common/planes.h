#pragma once

#include "mathtypes.h"

#include <array>

struct mapplane_t {
    double3_array normal;
    double3_array origin;
    vec_t dist;
    planetype type;
};

