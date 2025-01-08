#pragma once

#include "cmdlib.h" //--vluzacn


#include "mathtypes.h"
#include "win32fix.h"
#include "mathlib.h"
#include "bspfile.h"
#include "bounding_box.h"
#include <variant>

#define MAX_POINTS_ON_WINDING 128
// TODO: FIX THIS STUPID SHIT (MAX_POINTS_ON_WINDING)

#define BASE_WINDING_DISTANCE 9000

enum class side {
  front = 0,
  back = 1,
  on = 2,
  cross = -2 // Why is this -2?
};

#define	SIDE_FRONT		0
#define	SIDE_ON			2
#define	SIDE_BACK		1
#define	SIDE_CROSS		-2

#ifdef HLBSP //seedee
#ifndef DOUBLEVEC_T
#error you must add -dDOUBLEVEC_T to the project!
#endif
#define dplane_t plane_t
#define g_dplanes g_mapplanes
typedef struct
{
	vec3_array			normal;
	vec3_array			unused_origin;
	vec_t			dist;
	planetype		type;
} dplane_t;
extern std::array<dplane_t, MAX_INTERNAL_MAP_PLANES> g_dplanes;
#endif


enum class one_sided_winding_division_result {
  all_in_the_back,
  all_in_the_front
};
template<class W> struct split_winding_division_result_template {
  W back{};
  W front{};
};
template<class W> using winding_division_result_template = std::variant<
  one_sided_winding_division_result,
  split_winding_division_result_template<W>
>;


class Winding final
{
public:
    // General Functions
    void Print() const;
    void getPlane(dplane_t& plane) const;
    void getPlane(vec3_array& normal, vec_t& dist) const;
    vec_t getArea() const;
    bounding_box getBounds() const;
    vec3_array getCenter() const;
    void Check(
		  vec_t epsilon = ON_EPSILON
		) const; // Developer check for validity
    bool Valid() const;  // Runtime/user/normal check for validity
    bool empty() const;
    inline operator bool() const {
      return !empty();
    }
    void clear();


    void pushPoint(const vec3_array& newpoint);
    void insertPoint(const vec3_array& newpoint, std::size_t offset);
    std::size_t size() const;

    // Specialized Functions
    void            RemoveColinearPoints(
		vec_t epsilon = ON_EPSILON
		);
    bool            Clip(const dplane_t& split, bool keepon
		, vec_t epsilon = ON_EPSILON
		); // For hlbsp
    void            Clip(const dplane_t& split, Winding& front, Winding& back
		, vec_t epsilon = ON_EPSILON
		) const;
    void            Clip(const vec3_array& normal, const vec_t dist, Winding& front, Winding& back
		, vec_t epsilon = ON_EPSILON
		) const;
    bool            Chop(const vec3_array& normal, const vec_t dist
		, vec_t epsilon = ON_EPSILON
		);
    winding_division_result_template<Winding> Divide(const dplane_t& split, vec_t epsilon = ON_EPSILON
		) const;
    side             WindingOnPlaneSide(const vec3_array& normal, const vec_t dist
		, vec_t epsilon = ON_EPSILON
		);


private:
    void            grow_capacity();

public:
    // Construction
	  Winding(); // Do nothing :)
	  Winding(vec3_array *points, std::size_t numpoints); // Create from raw points
    Winding(const dface_t& face
		, vec_t epsilon = ON_EPSILON
		);
    Winding(const dplane_t& face);
    Winding(const vec3_array& normal, const vec_t dist);
    Winding(std::uint_least32_t points);
    Winding(const Winding& other);
    Winding(Winding&& other);
    ~Winding();
    Winding& operator=(const Winding& other);
    Winding& operator=(Winding&& other);

    // Misc
private:
    void initFromPlane(const vec3_array& normal, const vec_t dist);

public:
    // Data
    std::vector<vec3_array> m_Points{};
public:

    friend inline void swap(Winding& a, Winding& b) {
	    using std::swap;
      swap(a.m_Points, b.m_Points);
    }

};


using split_winding_division_result = split_winding_division_result_template<Winding>;
using winding_division_result =  winding_division_result_template<Winding>;

