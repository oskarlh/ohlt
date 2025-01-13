#pragma once

#include "mathtypes.h"

#include <array>

struct mapplane_t {
    vec3_array normal{};
    vec3_array origin{};
    vec_t dist{};
    planetype type{};
};

