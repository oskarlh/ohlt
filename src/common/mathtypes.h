#pragma once

#include <array>
#include "cmdlib.h"

typedef unsigned char byte;

#ifdef DOUBLEVEC_T
using vec_t = double;
#else
using vec_t = float;
#endif
using vec3_t = vec_t[3]; // x, y, z
using vec3_array = std::array<vec_t, 3>; // x, y, z
using float3_array = std::array<float, 3>;  // x, y, z
using double3_array = std::array<double, 3>;  // x, y, z


#include "transition.h"
