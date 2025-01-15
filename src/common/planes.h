#pragma once

#include "mathtypes.h"

#include <array>

struct mapplane_t {
    double3_array normal;
    double3_array origin;
    double dist;
    planetype type;
};

