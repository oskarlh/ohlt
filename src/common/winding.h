#pragma once

#include "bounding_box.h"
#include "bspfile.h"
#include "cmdlib.h"
#include "mathlib.h"
#include "mathtypes.h"
#include "planes.h"
#include "win32fix.h"

#include <variant>


#define MAX_POINTS_ON_WINDING 128
// TODO: FIX THIS STUPID SHIT (MAX_POINTS_ON_WINDING)

#define BASE_WINDING_DISTANCE 9000

enum class face_side {
  front = 0,
  back = 1,
  on = 2,
  cross = 3
};


enum class one_sided_winding_division_result {
  all_in_the_back,
  all_in_the_front
};

template<class W> struct split_winding_division_result_template {
  W back{};
  W front{};
};

template<any_vec_element VecElement>
class winding_base {
public:
    using vec_element = VecElement;
    using vec3 = std::array<vec_element, 3>;
  
    // General Functions
    void Print() const;
    void getPlane(dplane_t& plane) const;
    void getPlane(mapplane_t& plane) const;
    vec_element getArea() const;
    bounding_box getBounds() const;
    vec3 getCenter() const noexcept;
    void Check(
		  vec_element epsilon = ON_EPSILON
		) const; // Developer check for validity
    bool Valid() const;  // Runtime/user/normal check for validity
    bool empty() const;
    inline operator bool() const {
      return !empty();
    }
    void clear(bool shrinkToFit = false);


    void pushPoint(const vec3& newpoint);
    void insertPoint(const vec3& newpoint, std::size_t offset);
    std::size_t size() const;

    // Specialized Functions
    void RemoveColinearPoints(vec_element epsilon = ON_EPSILON);
    bool mutating_clip(const vec3& planeNormal, vec_element planeDist, bool keepon, vec_element epsilon = ON_EPSILON);
    void Clip(const dplane_t& split, winding_base& front, winding_base& back
		, vec_element epsilon = ON_EPSILON
		) const;
    void Clip(const vec3& normal, vec_element dist, winding_base& front, winding_base& back
		, vec_element epsilon = ON_EPSILON
		) const;
    bool            Chop(const vec3& normal, const vec_element dist
		, vec_element epsilon = ON_EPSILON
		);


    using one_sided_division_result =one_sided_winding_division_result;
    using split_division_result = split_winding_division_result_template<winding_base>;
    using division_result = std::variant<
      one_sided_division_result,
      split_division_result
    >;
    division_result Divide(const mapplane_t& split, vec_element epsilon = ON_EPSILON) const;

    face_side WindingOnPlaneSide(
      const vec3& normal,
      const vec_element dist,
      vec_element epsilon = ON_EPSILON
    );


private:
    void getPlane(vec3& normal, vec_element& dist) const;
    void grow_capacity();

public:
    // Construction
	  winding_base(); // Do nothing :)
	  winding_base(vec3 *points, std::size_t numpoints); // Create from raw points
    winding_base(const dface_t& face
		, vec_element epsilon = ON_EPSILON
		);
    winding_base(const dplane_t& face);
    winding_base(const mapplane_t& face);
    winding_base(const vec3& normal, const vec_element dist);
    winding_base(std::uint_least32_t points);
    winding_base(const winding_base& other);
    winding_base(winding_base&& other);
    ~winding_base();
    winding_base& operator=(const winding_base& other);
    winding_base& operator=(winding_base&& other);

    // Misc
private:
    void initFromPlane(const vec3& normal, const vec_element dist);

public:
    // Data
    std::vector<vec3> m_Points{};
public:

    friend inline void swap(winding_base& a, winding_base& b) {
	    using std::swap;
      swap(a.m_Points, b.m_Points);
    }
};

using accurate_winding = winding_base<double>;
using fast_winding = winding_base<float>;
