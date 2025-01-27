#pragma once

#include "cmdlib.h"

#include <array>

typedef unsigned char byte;

using float3_array = std::array<float, 3>;	 // x, y, z
using double3_array = std::array<double, 3>; // x, y, z

template <class T>
concept any_vec3 = std::same_as<T, float3_array>
	|| std::same_as<T, double3_array>;

template <class T>
concept any_vec_element = std::same_as<T, float> || std::same_as<T, double>;

template <any_vec3 FirstVec3, any_vec3... Rest>
struct largest_vec3_helper {
	using type = std::conditional_t<
		std::is_same_v<FirstVec3, double3_array>,
		double3_array,
		typename largest_vec3_helper<Rest...>::type>;
};

template <any_vec3 FirstVec3>
struct largest_vec3_helper<FirstVec3> {
	using type = FirstVec3;
};

template <any_vec3 FirstVec3, any_vec3... Rest>
using largest_vec3 = largest_vec3_helper<FirstVec3, Rest...>::type;

constexpr float3_array to_float3(double3_array const & input) noexcept {
	return { (float) input[0], (float) input[1], (float) input[2] };
}

constexpr float3_array const & to_float3(float3_array const & input
) noexcept {
	return input;
}

constexpr double3_array to_double3(float3_array const & input) noexcept {
	return { (double) input[0], (double) input[1], (double) input[2] };
}

constexpr double3_array const & to_double3(double3_array const & input
) noexcept {
	return input;
}

template <any_vec3 Out>
constexpr float3_array to_vec3(any_vec3 auto const & input) noexcept
	requires(std::is_same_v<Out, float3_array>) {
	return to_float3(input);
}

template <any_vec3 Out>
constexpr double3_array to_vec3(any_vec3 auto const & input) noexcept
	requires(std::is_same_v<Out, double3_array>) {
	return to_double3(input);
}

template <any_vec_element VecElement>
constexpr std::array<VecElement, 3>
to_vec3(VecElement el0, VecElement el1, VecElement el2) noexcept {
	return std::array<VecElement, 3>{ el0, el1, el2 };
}
