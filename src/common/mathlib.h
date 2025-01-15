#pragma once

#include "cmdlib.h" //--vluzacn
#include "mathtypes.h"
#include <algorithm>
#include <cmath>
#include <cstdint>


// We could use smaller epsilon for HLCSG and HLBSP
// (HLCSG and HLBSP use doubles for plane normals),
// which will totally eliminate all epsilon errors.
// But we choose this big epsilon to tolerate the imprecision caused by Hammer.
// Basically, this is a balance between precision and flexibility.
constexpr float NORMAL_EPSILON{0.00001f}; 
constexpr float ON_EPSILON{0.04f}; // We should ensure that (float)BOGUS_RANGE < (float)(BOGUS_RANGE + 0.2 * ON_EPSILON)
constexpr float EQUAL_EPSILON{0.004f};


//
// Vector Math
//


#define DotProduct(x,y) ( (x)[0] * (y)[0] + (x)[1] * (y)[1]  +  (x)[2] * (y)[2])
#define CrossProduct(a, b, dest) \
{ \
    (dest)[0] = (a)[1] * (b)[2] - (a)[2] * (b)[1]; \
    (dest)[1] = (a)[2] * (b)[0] - (a)[0] * (b)[2]; \
    (dest)[2] = (a)[0] * (b)[1] - (a)[1] * (b)[0]; \
}

constexpr auto dot_product(const any_vec3 auto& a, const any_vec3 auto& b) noexcept {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

template<any_vec3 VecA, any_vec3 VecB>
constexpr largest_vec3<VecA, VecB> cross_product(const VecA& a, const VecB& b) noexcept {
    return {
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0]
    };
}

template<any_vec3 VecA, any_vec3 VecB>
constexpr largest_vec3<VecA, VecB> vector_add(const VecA& a, const VecB& b) noexcept {
    return {
        a[0] + b[0],
        a[1] + b[1],
        a[2] + b[2]
    };
}

template<any_vec3 VecA, any_vec3 VecB>
constexpr largest_vec3<VecA, VecB> vector_subtract(const VecA& a, const VecB& b) noexcept {
    return {
        a[0] - b[0],
        a[1] - b[1],
        a[2] - b[2]
    };
}

constexpr void vector_fill(any_vec3 auto& vec, any_vec_t auto fillValue) noexcept {
    vec.fill(fillValue);
}

#define VectorMidpoint(a,b,c)    { (c)[0]=((a)[0]+(b)[0])/2; (c)[1]=((a)[1]+(b)[1])/2; (c)[2]=((a)[2]+(b)[2])/2; }

#define VectorFill(a,b)          { (a)[0]=(b); (a)[1]=(b); (a)[2]=(b);}
#define VectorAvg(a)             ( ( (a)[0] + (a)[1] + (a)[2] ) / 3 )

#define VectorSubtract(a,b,c)    { (c)[0]=(a)[0]-(b)[0]; (c)[1]=(a)[1]-(b)[1]; (c)[2]=(a)[2]-(b)[2]; }
#define VectorAdd(a,b,c)         { (c)[0]=(a)[0]+(b)[0]; (c)[1]=(a)[1]+(b)[1]; (c)[2]=(a)[2]+(b)[2]; }
#define VectorMultiply(a,b,c)    { (c)[0]=(a)[0]*(b)[0]; (c)[1]=(a)[1]*(b)[1]; (c)[2]=(a)[2]*(b)[2]; }

#define VectorAddVec(a,b,c)      { (c)[0]=(a)[0]+(b); (c)[1]=(a)[1]+(b); (c)[2]=(a)[2]+(b); }
#define VecSubtractVector(a,b,c) { (c)[0]=(a)-(b)[0]; (c)[1]=(a)-(b)[1]; (c)[2]=(a)-(b)[2]; }

#define VectorScale(a,b,c)       { (c)[0]=(a)[0]*(b);(c)[1]=(a)[1]*(b);(c)[2]=(a)[2]*(b); }

#define VectorCopy(a,b) { (b)[0]=(a)[0]; (b)[1]=(a)[1]; (b)[2]=(a)[2]; }
#define VectorClear(a)  { (a)[0] = (a)[1] = (a)[2] = 0.0; }


template<any_vec3 Vec3>
constexpr Vec3 vector_scale(Vec3& v, typename Vec3::value_type scale) noexcept {
    return Vec3{
        v[0] * scale,
        v[1] * scale,
        v[2] * scale
    };
}


constexpr auto vec3_max(const any_vec3 auto& v) noexcept {
    return std::max({ v[0], v[1], v[2] });
}
constexpr auto vec3_min(const any_vec3 auto& v) noexcept {
    return std::min({ v[0], v[1], v[2] });
}

#define VectorMaximum(a) ( std::max({ (a)[0], (a)[1], (a)[2] }) )
#define VectorMinimum(a) ( std::min({ (a)[0], (a)[1], (a)[2] }) )

#define VectorInverse(a) \
{ \
    (a)[0] = -((a)[0]); \
    (a)[1] = -((a)[1]); \
    (a)[2] = -((a)[2]); \
}
#define VectorRound(a) floor((a) + 0.5)
#define VectorMA(a, scale, b, dest) \
{ \
    (dest)[0] = (a)[0] + (scale) * (b)[0]; \
    (dest)[1] = (a)[1] + (scale) * (b)[1]; \
    (dest)[2] = (a)[2] + (scale) * (b)[2]; \
}

template<any_vec_t T>
constexpr T vector_length(const std::array<T, 3>& v) noexcept {
    return std::hypot(v[0], v[1], v[2]);
}

template<any_vec3 VecA, any_vec3 VecB>
constexpr largest_vec3<VecA, VecB> vector_minimums(const VecA& a, const VecB& b) noexcept {
    using vec_t = largest_vec3<VecA, VecB>::value_type;
    return {
        std::min(vec_t{a[0]}, vec_t{b[0]}),
        std::min(vec_t{a[1]}, vec_t{b[1]}),
        std::min(vec_t{a[2]}, vec_t{b[2]})
    };
}

template<any_vec3 VecA, any_vec3 VecB>
constexpr largest_vec3<VecA, VecB> vector_maximums(const VecA& a, const VecB& b) noexcept {
    using vec_t = largest_vec3<VecA, VecB>::value_type;
    return {
        std::max(vec_t{a[0]}, vec_t{b[0]}),
        std::max(vec_t{a[1]}, vec_t{b[1]}),
        std::max(vec_t{a[2]}, vec_t{b[2]})
    };
}


template<any_vec_t T>
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

template<any_vec_t T>
[[nodiscard]] constexpr std::array<T, 3> negate_vector(const std::array<T, 3>& v) noexcept {
    // We do 0 - x instead of just -x, so we don't unnecessarily
    // introduce signed zeroes (-0.0)
    return {T(0.0) - v[0], T(0.0) - v[1], T(0.0) - v[2]};
}


constexpr bool vectors_almost_same(const any_vec3 auto& v1, const any_vec3 auto& v2) {
    const bool significantDifference0 = std::fabs(v1[0] - v2[0]) > EQUAL_EPSILON;
    const bool significantDifference1 = std::fabs(v1[1] - v2[1]) > EQUAL_EPSILON;
    const bool significantDifference2 = std::fabs(v1[2] - v2[2]) > EQUAL_EPSILON;
    return !significantDifference0 && !significantDifference1 && !significantDifference2;
}

constexpr bool is_point_finite(const any_vec3 auto& p) noexcept {
    return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]);
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
constexpr planetype first_axial{planetype::plane_x};
constexpr planetype last_axial{planetype::plane_z};

constexpr float DIR_EPSILON = 0.0001;

template<any_vec_t T>
constexpr planetype plane_type_for_normal(const std::array<T, 3>& normal) noexcept {

    const T ax = std::fabs(normal[0]);
    const T ay = std::fabs(normal[1]);
    const T az = std::fabs(normal[2]);
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
