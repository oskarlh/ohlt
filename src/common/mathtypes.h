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

template<class T> concept any_vec3 = std::same_as<T, float3_array>
	|| std::same_as<T, double3_array>
;

template<class T> concept any_vec_t = std::same_as<T, float>
	|| std::same_as<T, double>
;

#include "transition.h"
