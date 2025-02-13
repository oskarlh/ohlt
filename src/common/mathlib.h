#pragma once

#include "cmdlib.h" //--vluzacn
#include "mathtypes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

// We could use smaller epsilon for HLCSG and HLBSP
// (HLCSG and HLBSP use doubles for plane normals),
// which will totally eliminate all epsilon errors.
// But we choose this big epsilon to tolerate the imprecision caused by
// Hammer. Basically, this is a balance between precision and flexibility.
constexpr float NORMAL_EPSILON{ 0.00001f };
constexpr float ON_EPSILON{
	0.04f
}; // We should ensure that (float)BOGUS_RANGE <
   // (float)(BOGUS_RANGE + 0.2 * ON_EPSILON)
constexpr float EQUAL_EPSILON{ 0.004f };

//
// Vector Math
//

constexpr auto
dot_product(any_vec3 auto const & a, any_vec3 auto const & b) noexcept {
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

template <any_vec3 VecA, any_vec3 VecB>
constexpr largest_vec3<VecA, VecB>
cross_product(VecA const & a, VecB const & b) noexcept {
	return { a[1] * b[2] - a[2] * b[1],
			 a[2] * b[0] - a[0] * b[2],
			 a[0] * b[1] - a[1] * b[0] };
}

constexpr auto
vector_add(any_vec3 auto const & a, any_vec3 auto const & b) noexcept {
	return std::array{ a[0] + b[0], a[1] + b[1], a[2] + b[2] };
}

constexpr auto
vector_add(any_vec3 auto const & a, any_vec_element auto b) noexcept {
	return std::array{ a[0] + b, a[1] + b, a[2] + b };
}

constexpr auto
vector_add(any_vec_element auto a, any_vec3 auto const & b) noexcept {
	return vector_add(b, a);
}

constexpr auto
vector_multiply(any_vec3 auto const & a, any_vec3 auto const & b) noexcept {
	return std::array{ a[0] * b[0], a[1] * b[1], a[2] * b[2] };
}

constexpr auto
vector_subtract(any_vec3 auto const & a, any_vec3 auto const & b) noexcept {
	return std::array{ a[0] - b[0], a[1] - b[1], a[2] - b[2] };
}

constexpr auto
vector_subtract(any_vec3 auto const & a, any_vec_element auto b) noexcept {
	return std::array{ a[0] - b, a[1] - b, a[2] - b };
}

constexpr auto
vector_subtract(any_vec_element auto a, any_vec3 auto const & b) noexcept {
	return std::array{ a - b[0], a - b[1], a - b[2] };
}

constexpr auto vector_average(any_vec3 auto const & v) {
	return (v[0] + v[1] + v[2]) / 3;
}

#define DotProduct(x, y) \
	((x)[0] * (y)[0] + (x)[1] * (y)[1] + (x)[2] * (y)[2])
#define CrossProduct(a, b, dest)                       \
	{                                                  \
		(dest)[0] = (a)[1] * (b)[2] - (a)[2] * (b)[1]; \
		(dest)[1] = (a)[2] * (b)[0] - (a)[0] * (b)[2]; \
		(dest)[2] = (a)[0] * (b)[1] - (a)[1] * (b)[0]; \
	}

#define VectorSubtract(a, b, c)   \
	{                             \
		(c)[0] = (a)[0] - (b)[0]; \
		(c)[1] = (a)[1] - (b)[1]; \
		(c)[2] = (a)[2] - (b)[2]; \
	}

#define VectorScale(a, b, c)   \
	{                          \
		(c)[0] = (a)[0] * (b); \
		(c)[1] = (a)[1] * (b); \
		(c)[2] = (a)[2] * (b); \
	}

constexpr auto
vector_scale(any_vec3 auto const & v, any_vec_element auto scale) noexcept {
	return std::array{ v[0] * scale, v[1] * scale, v[2] * scale };
}

constexpr auto vector_max_element(any_vec3 auto const & v) noexcept {
	return std::max({ v[0], v[1], v[2] });
}

constexpr auto vector_min_element(any_vec3 auto const & v) noexcept {
	return std::min({ v[0], v[1], v[2] });
}

template <any_vec3 Multiplicand, any_vec3 Multiplier, any_vec3 ToAdd>
constexpr auto vector_fma(
	Multiplicand const & multiplicand,
	Multiplier const & multiplier,
	ToAdd const & toAdd
) noexcept {
	using result_element = std::common_type_t<
		typename Multiplicand::value_type,
		typename Multiplier::value_type,
		typename ToAdd::value_type>;

	return std::array{ std::fma(
						   (result_element) multiplicand[0],
						   (result_element) multiplier[0],
						   (result_element) toAdd[0]
					   ),
					   std::fma(
						   (result_element) multiplicand[1],
						   (result_element) multiplier[1],
						   (result_element) toAdd[1]
					   ),
					   std::fma(
						   (result_element) multiplicand[2],
						   (result_element) multiplier[2],
						   (result_element) toAdd[2]
					   ) };
}

constexpr auto vector_fma(
	any_vec3 auto const & multiplicand,
	any_vec_element auto multiplier,
	any_vec3 auto const & toAdd
) noexcept {
	return vector_fma(
		multiplicand,
		std::array{ multiplier, multiplier, multiplier },
		toAdd
	);
}

template <any_vec3 T>
constexpr T::value_type vector_length(T const & v) noexcept {
	return std::hypot(v[0], v[1], v[2]);
}

template <any_vec3 T>
constexpr T::value_type
distance_between_points(T const & pointA, T const & pointB) noexcept {
	return vector_length(vector_subtract(pointA, pointB));
}

template <any_vec3 VecA, any_vec3 VecB>
constexpr largest_vec3<VecA, VecB>
vector_minimums(VecA const & a, VecB const & b) noexcept {
	using vec_element = largest_vec3<VecA, VecB>::value_type;
	return { std::min(vec_element{ a[0] }, vec_element{ b[0] }),
			 std::min(vec_element{ a[1] }, vec_element{ b[1] }),
			 std::min(vec_element{ a[2] }, vec_element{ b[2] }) };
}

template <any_vec3 VecA, any_vec3 VecB>
constexpr largest_vec3<VecA, VecB>
vector_maximums(VecA const & a, VecB const & b) noexcept {
	using vec_element = largest_vec3<VecA, VecB>::value_type;
	return { std::max(vec_element{ a[0] }, vec_element{ b[0] }),
			 std::max(vec_element{ a[1] }, vec_element{ b[1] }),
			 std::max(vec_element{ a[2] }, vec_element{ b[2] }) };
}

template <any_vec3 VecA, any_vec3 VecB>
constexpr largest_vec3<VecA, VecB>
midpoint_between(VecA const & a, VecB const & b) noexcept {
	return vector_scale(vector_add(a, b), 0.5f);
}

template <any_vec_element T>
constexpr T normalize_vector(std::array<T, 3>& v) {
	T length = vector_length(v);
	if (length < NORMAL_EPSILON) {
		v = {};
		return 0.0;
	}

	v[0] /= length;
	v[1] /= length;
	v[2] /= length;
	return length;
}

template <any_vec_element T>
[[nodiscard]] constexpr std::array<T, 3>
negate_vector(std::array<T, 3> const & v) noexcept {
	// We do 0 - x instead of just -x, so we don't unnecessarily
	// introduce signed zeroes (-0.0)
	return { T(0.0) - v[0], T(0.0) - v[1], T(0.0) - v[2] };
}

constexpr bool
vectors_almost_same(any_vec3 auto const & v1, any_vec3 auto const & v2) {
	bool const significantDifference0 = std::fabs(v1[0] - v2[0])
		> EQUAL_EPSILON;
	bool const significantDifference1 = std::fabs(v1[1] - v2[1])
		> EQUAL_EPSILON;
	bool const significantDifference2 = std::fabs(v1[2] - v2[2])
		> EQUAL_EPSILON;
	return !significantDifference0 && !significantDifference1
		&& !significantDifference2;
}

constexpr bool is_point_finite(any_vec3 auto const & p) noexcept {
	return std::isfinite(p[0]) && std::isfinite(p[1])
		&& std::isfinite(p[2]);
}

//
// Planetype Math
//

enum class planetype {
	plane_x = 0,
	plane_y,
	plane_z,
	plane_anyx,
	plane_anyy,
	plane_anyz
};
constexpr planetype first_axial{ planetype::plane_x };
constexpr planetype last_axial{ planetype::plane_z };

constexpr float DIR_EPSILON = 0.0001;

template <any_vec_element T>
constexpr planetype plane_type_for_normal(std::array<T, 3> const & normal
) noexcept {
	T const ax = std::fabs(normal[0]);
	T const ay = std::fabs(normal[1]);
	T const az = std::fabs(normal[2]);
	if (ax > 1.0 - DIR_EPSILON && ay < DIR_EPSILON && az < DIR_EPSILON) {
		return planetype::plane_x;
	}

	if (ay > 1.0 - DIR_EPSILON && az < DIR_EPSILON && ax < DIR_EPSILON) {
		return planetype::plane_y;
	}

	if (az > 1.0 - DIR_EPSILON && ax < DIR_EPSILON && ay < DIR_EPSILON) {
		return planetype::plane_z;
	}

	if ((ax >= ay) && (ax >= az)) {
		return planetype::plane_anyx;
	}
	if ((ay >= ax) && (ay >= az)) {
		return planetype::plane_anyy;
	}
	return planetype::plane_anyz;
}

std::uint16_t float_to_half(float v);
float half_to_float(std::uint16_t h);
