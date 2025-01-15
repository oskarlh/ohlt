#pragma once

#include <array>
#include "cmdlib.h"

typedef unsigned char byte;

#ifdef DOUBLEVEC_T
using vec_t = double;
#else
using vec_t = float;
#endif
using vec3_array = std::array<vec_t, 3>; // x, y, z
using float3_array = std::array<float, 3>;  // x, y, z
using double3_array = std::array<double, 3>;  // x, y, z

constexpr float3_array to_float3(const double3_array& input) noexcept {
	return { (float) input[0], (float) input[1], (float) input[2] };
}
constexpr const float3_array& to_float3(const float3_array& input) noexcept {
	return input;
}
constexpr double3_array to_double3(const float3_array& input) noexcept {
	return { (double) input[0], (double) input[1], (double) input[2] };
}
constexpr const double3_array& to_double3(const double3_array& input) noexcept {
	return input;
}

template<class T> concept any_vec3 = std::same_as<T, float3_array>
	|| std::same_as<T, double3_array>
;

template<class T> concept any_vec_t = std::same_as<T, float>
	|| std::same_as<T, double>
;
