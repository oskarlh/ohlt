#pragma once

#include "cmdlib.h" //--vluzacn


#include "mathtypes.h"
#include "win32fix.h"
#include "mathlib.h"
#include "bspfile.h"
#include "bounding_box.h"

#define MAX_POINTS_ON_WINDING 128
// TODO: FIX THIS STUPID SHIT (MAX_POINTS_ON_WINDING)

#define BASE_WINDING_DISTANCE 9000

#define	SIDE_FRONT		0
#define	SIDE_ON			2
#define	SIDE_BACK		1
#define	SIDE_CROSS		-2

#ifdef SDHLBSP //seedee
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
	planetypes		type;
} dplane_t;
extern std::array<dplane_t, MAX_INTERNAL_MAP_PLANES> g_dplanes;
#endif
class Winding final
{
public:
    // General Functions
    void            Print() const;
    void getPlane(dplane_t& plane) const;
    void getPlane(vec3_array& normal, vec_t& dist) const;
    vec_t           getArea() const;
    void            getBounds(bounding_box& bounds) const;
    void            getBounds(vec3_array& mins, vec3_array& maxs) const;
    vec3_array      getCenter() const;
    Winding*        Copy() const;
    void            Check(
		vec_t epsilon = ON_EPSILON
		) const;  // Developer check for validity
    bool            Valid() const;  // Runtime/user/normal check for validity
    void            addPoint(const vec3_array& newpoint);
    void            insertPoint(const vec3_array& newpoint, const unsigned int offset);

    // Specialized Functions
    void            RemoveColinearPoints(
		vec_t epsilon = ON_EPSILON
		);
    bool            Clip(const dplane_t& split, bool keepon
		, vec_t epsilon = ON_EPSILON
		); // For hlbsp
    void            Clip(const dplane_t& split, Winding** front, Winding** back
		, vec_t epsilon = ON_EPSILON
		);
    void            Clip(const vec3_array& normal, const vec_t dist, Winding** front, Winding** back
		, vec_t epsilon = ON_EPSILON
		);
    bool            Chop(const vec3_array& normal, const vec_t dist
		, vec_t epsilon = ON_EPSILON
		);
    void            Divide(const dplane_t& split, Winding** front, Winding** back
		, vec_t epsilon = ON_EPSILON
		);
    int             WindingOnPlaneSide(const vec3_array& normal, const vec_t dist
		, vec_t epsilon = ON_EPSILON
		);

protected:
    void            grow_size();

public:
    // Construction
	Winding();										// Do nothing :)
	Winding(vec3_array *points, std::uint_least32_t numpoints);		// Create from raw points
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
    std::uint_least32_t  m_NumPoints{0};
    std::vector<vec3_array> m_Points{};
protected:
    std::uint_least32_t  m_MaxPoints{0};
};
