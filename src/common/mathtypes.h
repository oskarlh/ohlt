#pragma once

#include "cmdlib.h"

#include <array>

using byte = unsigned char;

using float3_array = std::array<float, 3>;	 // x, y, z
using double3_array = std::array<double, 3>; // x, y, z

template <class T>
concept any_vec3 = std::floating_point<typename T::value_type>
	&& std::same_as<T, std::array<typename T::value_type, 3>>;

template <any_vec3 FirstVec3, any_vec3... Rest>
struct largest_vec3_helper final {
	using type = std::conditional_t<
		std::is_same_v<FirstVec3, double3_array>,
		double3_array,
		typename largest_vec3_helper<Rest...>::type>;
};

template <any_vec3 FirstVec3>
struct largest_vec3_helper<FirstVec3> final {
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

template <std::floating_point OutElement, std::floating_point InElement>
constexpr std::array<OutElement, 3>
to_vec3(std::array<InElement, 3> const & input) noexcept {
	return std::array{ OutElement(input[0]),
					   OutElement(input[1]),
					   OutElement(input[2]) };
}

template <std::floating_point OutElement, std::floating_point InElement>
constexpr std::array<OutElement, 3> const &
to_vec3(std::array<InElement, 3> const & input) noexcept
	requires(std::is_same_v<OutElement, InElement>) {
	return input;
}
