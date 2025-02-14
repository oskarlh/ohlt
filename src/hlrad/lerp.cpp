#include "qrad.h"

#include <algorithm>
#include <numbers>
#include <vector>

int g_lerp_enabled = DEFAULT_LERP_ENABLED;

struct interpolation_t final {
	struct Point final {
		int patchnum;
		float weight;
	};

	bool isbiased;
	float totalweight;
	std::vector<Point> points;
};

struct localtriangulation_t final {
	struct Wedge final {
		enum eShape {
			eTriangular,
			eConvex,
			eConcave,
			eSquareLeft,
			eSquareRight,
		};

		eShape shape;
		int leftpatchnum;
		float3_array leftspot;
		float3_array leftdirection;
		// right side equals to the left side of the next wedge

		float3_array wedgenormal; // for custom usage
	};

	struct HullPoint final {
		float3_array spot;
		float3_array direction;
	};

	dplane_t plane;
	fast_winding winding;
	float3_array center; // center is on the plane

	float3_array normal;
	int patchnum;
	std::vector<int> neighborfaces; // including the face itself

	std::vector<Wedge>
		sortedwedges; // in clockwise order (same as fast_winding)
	std::vector<HullPoint>
		sortedhullpoints; // in clockwise order (same as fast_winding)
};

struct facetriangulation_t final {
	struct Wall final {
		float3_array points[2];
		float3_array direction;
		float3_array normal;
	};

	int facenum;
	std::vector<int> neighbors; // including the face itself
	std::vector<Wall> walls;
	std::vector<localtriangulation_t*> localtriangulations;
	std::vector<int> usedpatches;
};

facetriangulation_t* g_facetriangulations[MAX_MAP_FACES];

static bool CalcAdaptedSpot(
	localtriangulation_t const * lt,
	float3_array const & position,
	int surface,
	float3_array& spot
)
// If the surface formed by the face and its neighbor faces is not flat, the
// surface should be unfolded onto the face plane CalcAdaptedSpot calculates
// the coordinate of the unfolded spot on the face plane from the original
// position on the surface CalcAdaptedSpot(center) = {0,0,0}
// CalcAdaptedSpot(position on the face plane) = position - center
// Param position: must include g_face_offset
{
	int i;
	float dot;
	float3_array surfacespot;
	float dist;
	float dist2;
	float3_array phongnormal;
	float frac;
	float3_array middle;

	for (i = 0; i < (int) lt->neighborfaces.size(); i++) {
		if (lt->neighborfaces[i] == surface) {
			break;
		}
	}
	if (i == (int) lt->neighborfaces.size()) {
		spot = {};
		return false;
	}

	surfacespot = vector_subtract(position, lt->center);
	dot = dot_product(surfacespot, lt->normal);
	spot = vector_fma(lt->normal, -dot, surfacespot);

	// Use phong normal instead of face normal, because phong normal is a
	// continuous function
	GetPhongNormal(surface, position, phongnormal);
	dot = dot_product(spot, phongnormal);
	if (fabs(dot) > ON_EPSILON) {
		frac = dot_product(surfacespot, phongnormal) / dot;
		// To correct some extreme cases
		frac = std::clamp(frac, 0.0f, 1.0f);
	} else {
		frac = 0;
	}
	middle = vector_scale(spot, frac);

	dist = vector_length(spot);
	dist2 = vector_length(middle)
		+ distance_between_points(surfacespot, middle);

	if (dist > ON_EPSILON && fabs(dist2 - dist) > ON_EPSILON) {
		spot = vector_scale(spot, dist2 / dist);
	}
	return true;
}

static float GetAngle(
	float3_array const & leftdirection,
	float3_array const & rightdirection,
	float3_array const & normal
) {
	float const angle = atan2(
		dot_product(cross_product(rightdirection, leftdirection), normal),
		dot_product(rightdirection, leftdirection)
	);

	return angle;
}

static float GetAngleDiff(float angle, float base) {
	float diff;

	diff = angle - base;
	if (diff < 0) {
		diff += 2 * std::numbers::pi_v<double>;
	}
	return diff;
}

static float GetFrac(
	float3_array const & leftspot,
	float3_array const & rightspot,
	float3_array const & direction,
	float3_array const & normal
) {
	float3_array const v = cross_product(direction, normal);
	float const dot1 = dot_product(leftspot, v);
	float const dot2 = dot_product(rightspot, v);
	float frac;
	// dot1 <= 0 < dot2
	if (dot1 >= -NORMAL_EPSILON) {
		if (g_drawlerp && dot1 > ON_EPSILON) {
			Developer(
				developer_level::spam,
				"Debug: triangulation: internal error 1.\n"
			);
		}
		frac = 0.0;
	} else if (dot2 <= NORMAL_EPSILON) {
		if (g_drawlerp && dot2 < -ON_EPSILON) {
			Developer(
				developer_level::spam,
				"Debug: triangulation: internal error 2.\n"
			);
		}
		frac = 1.0;
	} else {
		frac = dot1 / (dot1 - dot2);
		frac = std::max((float) 0, std::min(frac, (float) 1));
	}

	return frac;
}

static float GetDirection(
	float3_array const & spot,
	float3_array const & normal,
	float3_array& direction_out
) {
	float const dot = dot_product(spot, normal);
	direction_out = vector_fma(normal, -dot, spot);
	return normalize_vector(direction_out);
}

static bool CalcWeight(
	localtriangulation_t const * lt,
	float3_array const & spot,
	float& weight_out
)
// It returns true when the point is inside the hull region (with boundary),
// even if weight = 0.
{
	float3_array direction;
	localtriangulation_t::HullPoint const * hp1;
	localtriangulation_t::HullPoint const * hp2;
	bool istoofar;
	float ratio;

	int i;
	int j;
	float angle;
	std::vector<float> angles;
	float frac;
	float len;
	float dist;

	if (GetDirection(spot, lt->normal, direction) <= 2 * ON_EPSILON) {
		weight_out = 1.0;
		return true;
	}

	if ((int) lt->sortedhullpoints.size() == 0) {
		weight_out = 0.0;
		return false;
	}

	angles.resize((int) lt->sortedhullpoints.size());
	for (i = 0; i < (int) lt->sortedhullpoints.size(); i++) {
		angle = GetAngle(
			lt->sortedhullpoints[i].direction, direction, lt->normal
		);
		angles[i] = GetAngleDiff(angle, 0);
	}
	j = 0;
	for (i = 1; i < (int) lt->sortedhullpoints.size(); i++) {
		if (angles[i] < angles[j]) {
			j = i;
		}
	}
	hp1 = &lt->sortedhullpoints[j];
	hp2 = &lt->sortedhullpoints
			   [(j + 1) % (int) lt->sortedhullpoints.size()];

	frac = GetFrac(hp1->spot, hp2->spot, direction, lt->normal);

	len = (1 - frac) * dot_product(hp1->spot, direction)
		+ frac * dot_product(hp2->spot, direction);
	dist = dot_product(spot, direction);
	if (len <= ON_EPSILON / 4 || dist > len + 2 * ON_EPSILON) {
		istoofar = true;
		ratio = 1.0;
	} else if (dist >= len - ON_EPSILON) {
		istoofar = false; // if we change this "false" to "true", we will
						  // see many places turned "green" in "-drawlerp"
						  // mode
		ratio = 1.0;	  // in order to prevent excessively small weight
	} else {
		istoofar = false;
		ratio = dist / len;
		ratio = std::max((float) 0, std::min(ratio, (float) 1));
	}

	weight_out = 1 - ratio;
	return !istoofar;
}

static void CalcInterpolation_Square(
	localtriangulation_t const * lt,
	int i,
	float3_array const & spot,
	interpolation_t& interp
) {
	localtriangulation_t::Wedge const * w1;
	localtriangulation_t::Wedge const * w2;
	localtriangulation_t::Wedge const * w3;
	float weights[4];
	float dot1;
	float dot2;
	float dot;
	float frac;
	float frac_near;
	float frac_far;
	float ratio;
	float3_array mid_far;
	float3_array mid_near;
	float3_array test;

	w1 = &lt->sortedwedges[i];
	w2 = &lt->sortedwedges[(i + 1) % (int) lt->sortedwedges.size()];
	w3 = &lt->sortedwedges[(i + 2) % (int) lt->sortedwedges.size()];
	if (w1->shape != localtriangulation_t::Wedge::eSquareLeft
		|| w2->shape != localtriangulation_t::Wedge::eSquareRight) {
		Error("CalcInterpolation_Square: internal error: not square.");
	}

	weights[0] = 0.0;
	weights[1] = 0.0;
	weights[2] = 0.0;
	weights[3] = 0.0;

	// find mid_near on (o,p3), mid_far on (p1,p2), spot on
	// (mid_near,mid_far)

	float3_array normal1 = cross_product(w1->leftdirection, lt->normal);
	normalize_vector(normal1);
	float3_array normal2 = cross_product(w2->wedgenormal, lt->normal);
	normalize_vector(normal2);
	dot1 = dot_product(spot, normal1) - 0;
	dot2 = dot_product(spot, normal2) - dot_product(w3->leftspot, normal2);
	if (dot1 <= NORMAL_EPSILON) {
		frac = 0.0;
	} else if (dot2 <= NORMAL_EPSILON) {
		frac = 1.0;
	} else {
		frac = dot1 / (dot1 + dot2);
		frac = std::max((float) 0, std::min(frac, (float) 1));
	}

	dot1 = dot_product(w3->leftspot, normal1) - 0;
	dot2 = 0 - dot_product(w3->leftspot, normal2);
	if (dot1 <= NORMAL_EPSILON) {
		frac_near = 1.0;
	} else if (dot2 <= NORMAL_EPSILON) {
		frac_near = 0.0;
	} else {
		frac_near = (frac * dot2) / ((1 - frac) * dot1 + frac * dot2);
	}
	mid_near = vector_scale(w3->leftspot, frac_near);

	dot1 = dot_product(w2->leftspot, normal1) - 0;
	dot2 = dot_product(w1->leftspot, normal2)
		- dot_product(w3->leftspot, normal2);
	if (dot1 <= NORMAL_EPSILON) {
		frac_far = 1.0;
	} else if (dot2 <= NORMAL_EPSILON) {
		frac_far = 0.0;
	} else {
		frac_far = (frac * dot2) / ((1 - frac) * dot1 + frac * dot2);
	}
	mid_far = vector_fma(
		w2->leftspot, frac_far, vector_scale(w1->leftspot, 1 - frac_far)
	);

	float3_array normal = cross_product(lt->normal, w3->leftdirection);
	normalize_vector(normal);
	dot = dot_product(spot, normal) - 0;
	dot1 = (1 - frac_far) * dot_product(w1->leftspot, normal)
		+ frac_far * dot_product(w2->leftspot, normal) - 0;
	if (dot <= NORMAL_EPSILON) {
		ratio = 0.0;
	} else if (dot >= dot1) {
		ratio = 1.0;
	} else {
		ratio = dot / dot1;
		ratio = std::max((float) 0, std::min(ratio, (float) 1));
	}

	test = vector_subtract(
		vector_fma(mid_far, ratio, vector_scale(mid_near, 1 - ratio)), spot
	);
	if (g_drawlerp && vector_length(test) > 4 * ON_EPSILON) {
		Developer(
			developer_level::spam,
			"Debug: triangulation: internal error 12.\n"
		);
	}

	weights[0] += 0.5 * (1 - ratio) * (1 - frac_near);
	weights[3] += 0.5 * (1 - ratio) * frac_near;
	weights[1] += 0.5 * ratio * (1 - frac_far);
	weights[2] += 0.5 * ratio * frac_far;

	// find mid_near on (o,p1), mid_far on (p2,p3), spot on
	// (mid_near,mid_far)
	normal1 = cross_product(lt->normal, w3->leftdirection);
	normalize_vector(normal1);
	normal2 = cross_product(w1->wedgenormal, lt->normal);
	normalize_vector(normal2);
	dot1 = dot_product(spot, normal1) - 0;
	dot2 = dot_product(spot, normal2) - dot_product(w1->leftspot, normal2);
	if (dot1 <= NORMAL_EPSILON) {
		frac = 0.0;
	} else if (dot2 <= NORMAL_EPSILON) {
		frac = 1.0;
	} else {
		frac = dot1 / (dot1 + dot2);
		frac = std::max((float) 0, std::min(frac, (float) 1));
	}

	dot1 = dot_product(w1->leftspot, normal1) - 0;
	dot2 = 0 - dot_product(w1->leftspot, normal2);
	if (dot1 <= NORMAL_EPSILON) {
		frac_near = 1.0;
	} else if (dot2 <= NORMAL_EPSILON) {
		frac_near = 0.0;
	} else {
		frac_near = (frac * dot2) / ((1 - frac) * dot1 + frac * dot2);
	}
	mid_near = vector_scale(w1->leftspot, frac_near);

	dot1 = dot_product(w2->leftspot, normal1) - 0;
	dot2 = dot_product(w3->leftspot, normal2)
		- dot_product(w1->leftspot, normal2);
	if (dot1 <= NORMAL_EPSILON) {
		frac_far = 1.0;
	} else if (dot2 <= NORMAL_EPSILON) {
		frac_far = 0.0;
	} else {
		frac_far = (frac * dot2) / ((1 - frac) * dot1 + frac * dot2);
	};
	mid_far = vector_fma(
		w2->leftspot, frac_far, vector_scale(w3->leftspot, 1 - frac_far)
	);

	normal = cross_product(w1->leftdirection, lt->normal);
	normalize_vector(normal);
	dot = dot_product(spot, normal) - 0;
	dot1 = (1 - frac_far) * dot_product(w3->leftspot, normal)
		+ frac_far * dot_product(w2->leftspot, normal) - 0;
	if (dot <= NORMAL_EPSILON) {
		ratio = 0.0;
	} else if (dot >= dot1) {
		ratio = 1.0;
	} else {
		ratio = dot / dot1;
		ratio = std::max((float) 0, std::min(ratio, (float) 1));
	}

	test = vector_subtract(
		vector_fma(mid_far, ratio, vector_scale(mid_near, 1 - ratio)), spot
	);
	if (g_drawlerp && vector_length(test) > 4 * ON_EPSILON) {
		Developer(
			developer_level::spam,
			"Debug: triangulation: internal error 13.\n"
		);
	}

	weights[0] += 0.5 * (1 - ratio) * (1 - frac_near);
	weights[1] += 0.5 * (1 - ratio) * frac_near;
	weights[3] += 0.5 * ratio * (1 - frac_far);
	weights[2] += 0.5 * ratio * frac_far;

	interp.isbiased = false;
	interp.totalweight = 1.0;
	interp.points.resize(4);
	interp.points[0].patchnum = lt->patchnum;
	interp.points[0].weight = weights[0];
	interp.points[1].patchnum = w1->leftpatchnum;
	interp.points[1].weight = weights[1];
	interp.points[2].patchnum = w2->leftpatchnum;
	interp.points[2].weight = weights[2];
	interp.points[3].patchnum = w3->leftpatchnum;
	interp.points[3].weight = weights[3];
}

static void CalcInterpolation(
	localtriangulation_t const * lt,
	float3_array const & spot,
	interpolation_t& interp
)
// The interpolation function is defined over the entire plane, so
// CalcInterpolation never fails.
{
	float3_array direction;
	localtriangulation_t::Wedge const * w;
	localtriangulation_t::Wedge const * wnext;

	int i;
	int j;
	float angle;
	std::vector<float> angles;

	if (GetDirection(spot, lt->normal, direction) <= 2 * ON_EPSILON) {
		// spot happens to be at the center
		interp.isbiased = false;
		interp.totalweight = 1.0;
		interp.points.resize(1);
		interp.points[0].patchnum = lt->patchnum;
		interp.points[0].weight = 1.0;
		return;
	}

	if ((int) lt->sortedwedges.size()
		== 0) // this local triangulation only has center patch
	{
		interp.isbiased = true;
		interp.totalweight = 1.0;
		interp.points.resize(1);
		interp.points[0].patchnum = lt->patchnum;
		interp.points[0].weight = 1.0;
		return;
	}

	// Find the wedge with minimum non-negative angle (counterclockwise)
	// pass the spot
	angles.resize((int) lt->sortedwedges.size());
	for (i = 0; i < (int) lt->sortedwedges.size(); i++) {
		angle = GetAngle(
			lt->sortedwedges[i].leftdirection, direction, lt->normal
		);
		angles[i] = GetAngleDiff(angle, 0);
	}
	j = 0;
	for (i = 1; i < (int) lt->sortedwedges.size(); i++) {
		if (angles[i] < angles[j]) {
			j = i;
		}
	}
	w = &lt->sortedwedges[j];
	wnext = &lt->sortedwedges[(j + 1) % (int) lt->sortedwedges.size()];

	// Different wedge types have different interpolation methods
	switch (w->shape) {
		case localtriangulation_t::Wedge::eSquareLeft:
		case localtriangulation_t::Wedge::eSquareRight:
		case localtriangulation_t::Wedge::eTriangular:
			// w->wedgenormal is undefined
			{
				float frac;
				float len;
				float dist;
				bool istoofar;
				float ratio;

				frac = GetFrac(
					w->leftspot, wnext->leftspot, direction, lt->normal
				);

				len = (1 - frac) * dot_product(w->leftspot, direction)
					+ frac * dot_product(wnext->leftspot, direction);
				dist = dot_product(spot, direction);
				if (len <= ON_EPSILON / 4 || dist > len + 2 * ON_EPSILON) {
					istoofar = true;
					ratio = 1.0;
				} else if (dist >= len - ON_EPSILON) {
					istoofar = false;
					ratio = 1.0;
				} else {
					istoofar = false;
					ratio = dist / len;
					ratio = std::max((float) 0, std::min(ratio, (float) 1));
				}

				if (istoofar) {
					interp.isbiased = true;
					interp.totalweight = 1.0;
					interp.points.resize(2);
					interp.points[0].patchnum = w->leftpatchnum;
					interp.points[0].weight = 1 - frac;
					interp.points[1].patchnum = wnext->leftpatchnum;
					interp.points[1].weight = frac;
				} else if (w->shape
						   == localtriangulation_t::Wedge::eSquareLeft) {
					i = w - &lt->sortedwedges[0];
					CalcInterpolation_Square(lt, i, spot, interp);
				} else if (w->shape
						   == localtriangulation_t::Wedge::eSquareRight) {
					i = w - &lt->sortedwedges[0];
					i = (i - 1 + (int) lt->sortedwedges.size())
						% (int) lt->sortedwedges.size();
					CalcInterpolation_Square(lt, i, spot, interp);
				} else {
					interp.isbiased = false;
					interp.totalweight = 1.0;
					interp.points.resize(3);
					interp.points[0].patchnum = lt->patchnum;
					interp.points[0].weight = 1 - ratio;
					interp.points[1].patchnum = w->leftpatchnum;
					interp.points[1].weight = ratio * (1 - frac);
					interp.points[2].patchnum = wnext->leftpatchnum;
					interp.points[2].weight = ratio * frac;
				}
			}
			break;
		case localtriangulation_t::Wedge::eConvex:
			// w->wedgenormal is the unit vector pointing from w->leftspot
			// to wnext->leftspot
			{
				float dot;
				float dot1;
				float dot2;
				float frac;

				dot1 = dot_product(w->leftspot, w->wedgenormal)
					- dot_product(spot, w->wedgenormal);
				dot2 = dot_product(wnext->leftspot, w->wedgenormal)
					- dot_product(spot, w->wedgenormal);
				dot = 0 - dot_product(spot, w->wedgenormal);
				// for eConvex type: dot1 < dot < dot2

				if (g_drawlerp && (dot1 > dot || dot > dot2)) {
					Developer(
						developer_level::spam,
						"Debug: triangulation: internal error 3.\n"
					);
				}
				if (dot1 >= -NORMAL_EPSILON) // 0 <= dot1 < dot < dot2
				{
					interp.isbiased = true;
					interp.totalweight = 1.0;
					interp.points.resize(1);
					interp.points[0].patchnum = w->leftpatchnum;
					interp.points[0].weight = 1.0;
				} else if (dot2 <= NORMAL_EPSILON) // dot1 < dot < dot2 <= 0
				{
					interp.isbiased = true;
					interp.totalweight = 1.0;
					interp.points.resize(1);
					interp.points[0].patchnum = wnext->leftpatchnum;
					interp.points[0].weight = 1.0;
				} else if (dot > 0) // dot1 < 0 < dot < dot2
				{
					frac = dot1 / (dot1 - dot);
					frac = std::max((float) 0, std::min(frac, (float) 1));

					interp.isbiased = true;
					interp.totalweight = 1.0;
					interp.points.resize(2);
					interp.points[0].patchnum = w->leftpatchnum;
					interp.points[0].weight = 1 - frac;
					interp.points[1].patchnum = lt->patchnum;
					interp.points[1].weight = frac;
				} else // dot1 < dot <= 0 < dot2
				{
					frac = dot / (dot - dot2);
					frac = std::max((float) 0, std::min(frac, (float) 1));

					interp.isbiased = true;
					interp.totalweight = 1.0;
					interp.points.resize(2);
					interp.points[0].patchnum = lt->patchnum;
					interp.points[0].weight = 1 - frac;
					interp.points[1].patchnum = wnext->leftpatchnum;
					interp.points[1].weight = frac;
				}
			}
			break;
		case localtriangulation_t::Wedge::eConcave: {
			float len;
			float dist;
			float ratio;

			if (dot_product(spot, w->wedgenormal)
				< 0) // the spot is closer to the left edge than the right
					 // edge
			{
				len = dot_product(w->leftspot, w->leftdirection);
				dist = dot_product(spot, w->leftdirection);
				if (g_drawlerp && len <= ON_EPSILON) {
					Developer(
						developer_level::spam,
						"Debug: triangulation: internal error 4.\n"
					);
				}
				if (dist <= NORMAL_EPSILON) {
					interp.isbiased = true;
					interp.totalweight = 1.0;
					interp.points.resize(1);
					interp.points[0].patchnum = lt->patchnum;
					interp.points[0].weight = 1.0;
				} else if (dist >= len) {
					interp.isbiased = true;
					interp.totalweight = 1.0;
					interp.points.resize(1);
					interp.points[0].patchnum = w->leftpatchnum;
					interp.points[0].weight = 1.0;
				} else {
					ratio = dist / len;
					ratio = std::max((float) 0, std::min(ratio, (float) 1));

					interp.isbiased = true;
					interp.totalweight = 1.0;
					interp.points.resize(2);
					interp.points[0].patchnum = lt->patchnum;
					interp.points[0].weight = 1 - ratio;
					interp.points[1].patchnum = w->leftpatchnum;
					interp.points[1].weight = ratio;
				}
			} else // the spot is closer to the right edge than the left
				   // edge
			{
				len = dot_product(wnext->leftspot, wnext->leftdirection);
				dist = dot_product(spot, wnext->leftdirection);
				if (g_drawlerp && len <= ON_EPSILON) {
					Developer(
						developer_level::spam,
						"Debug: triangulation: internal error 5.\n"
					);
				}
				if (dist <= NORMAL_EPSILON) {
					interp.isbiased = true;
					interp.totalweight = 1.0;
					interp.points.resize(1);
					interp.points[0].patchnum = lt->patchnum;
					interp.points[0].weight = 1.0;
				} else if (dist >= len) {
					interp.isbiased = true;
					interp.totalweight = 1.0;
					interp.points.resize(1);
					interp.points[0].patchnum = wnext->leftpatchnum;
					interp.points[0].weight = 1.0;
				} else {
					ratio = dist / len;
					ratio = std::max((float) 0, std::min(ratio, (float) 1));

					interp.isbiased = true;
					interp.totalweight = 1.0;
					interp.points.resize(2);
					interp.points[0].patchnum = lt->patchnum;
					interp.points[0].weight = 1 - ratio;
					interp.points[1].patchnum = wnext->leftpatchnum;
					interp.points[1].weight = ratio;
				}
			}
		} break;
		default:
			Error("CalcInterpolation: internal error: invalid wedge type.");
			break;
	}
}

static void ApplyInterpolation(
	interpolation_t const & interp, int style, float3_array& out
) {
	out = {};
	if (interp.totalweight <= 0) {
		return;
	}
	for (auto const & point : interp.points) {
		float3_array const totalLight = get_total_light(
			g_patches[point.patchnum], style
		);
		out = vector_fma(
			totalLight, point.weight / interp.totalweight, out
		);
	}
}

// =====================================================================================
//  InterpolateSampleLight
// =====================================================================================
void InterpolateSampleLight(
	float3_array const & position, int surface, int style, float3_array& out
) {
	try {
		facetriangulation_t const * ft;
		std::vector<float> localweights;
		std::vector<interpolation_t*> localinterps;

		int i;
		int j;
		int n;
		facetriangulation_t const * ft2;
		localtriangulation_t const * lt;
		float3_array spot;
		float weight;
		interpolation_t* interp;
		localtriangulation_t const * best;
		float bestdist;
		float dot;

		if (surface < 0 || surface >= g_numfaces) {
			Error(
				"InterpolateSampleLight: internal error: surface number out of range."
			);
		}
		ft = g_facetriangulations[surface];
		interpolation_t maininterp{};
		maininterp.points.reserve(64);

		// Calculate local interpolations and their weights
		localweights.resize(0);
		localinterps.resize(0);
		if (g_lerp_enabled) {
			for (i = 0; i < (int) ft->neighbors.size();
				 i++) // for this face and each of its neighbors
			{
				ft2 = g_facetriangulations[ft->neighbors[i]];
				for (j = 0; j < (int) ft2->localtriangulations.size();
					 j++) // for each patch on that face
				{
					lt = ft2->localtriangulations[j];
					if (!CalcAdaptedSpot(lt, position, surface, spot)) {
						if (g_drawlerp && ft2 == ft) {
							Developer(
								developer_level::spam,
								"Debug: triangulation: internal error 6.\n"
							);
						}
						continue;
					}
					if (!CalcWeight(lt, spot, weight)) {
						continue;
					}
					interp = new interpolation_t;
					interp->points.reserve(4);
					CalcInterpolation(lt, spot, *interp);

					localweights.push_back(weight);
					localinterps.push_back(interp);
				}
			}
		}

		// Combine into one interpolation
		maininterp.isbiased = false;
		maininterp.totalweight = 0;
		maininterp.points.resize(0);
		for (i = 0; i < (int) localinterps.size(); i++) {
			if (localinterps[i]->isbiased) {
				maininterp.isbiased = true;
			}
			for (j = 0; j < (int) localinterps[i]->points.size(); j++) {
				weight = localinterps[i]->points[j].weight
					* localweights[i];
				if (g_patches[localinterps[i]->points[j].patchnum].flags
					== ePatchFlagOutside) {
					weight *= 0.01;
				}
				n = (int) maininterp.points.size();
				maininterp.points.resize(n + 1);
				maininterp.points[n].patchnum
					= localinterps[i]->points[j].patchnum;
				maininterp.points[n].weight = weight;
				maininterp.totalweight += weight;
			}
		}
		if (maininterp.totalweight > 0) {
			ApplyInterpolation(maininterp, style, out);
			if (g_drawlerp) {
				// white or yellow
				out[0] = 100;
				out[1] = 100;
				out[2] = (maininterp.isbiased ? 0 : 100);
			}
		} else {
			// try again, don't multiply localweights[i] (which equals to 0)
			maininterp.isbiased = false;
			maininterp.totalweight = 0;
			maininterp.points.resize(0);
			for (i = 0; i < (int) localinterps.size(); i++) {
				if (localinterps[i]->isbiased) {
					maininterp.isbiased = true;
				}
				for (j = 0; j < (int) localinterps[i]->points.size(); j++) {
					weight = localinterps[i]->points[j].weight;
					if (g_patches[localinterps[i]->points[j].patchnum].flags
						== ePatchFlagOutside) {
						weight *= 0.01;
					}
					n = (int) maininterp.points.size();
					maininterp.points.resize(n + 1);
					maininterp.points[n].patchnum
						= localinterps[i]->points[j].patchnum;
					maininterp.points[n].weight = weight;
					maininterp.totalweight += weight;
				}
			}
			if (maininterp.totalweight > 0) {
				ApplyInterpolation(maininterp, style, out);
				if (g_drawlerp) {
					// red
					out[0] = 100;
					out[1] = 0;
					out[2] = (maininterp.isbiased ? 0 : 100);
				}
			} else {
				// worst case, simply use the nearest patch

				best = nullptr;
				for (i = 0; i < (int) ft->localtriangulations.size(); i++) {
					lt = ft->localtriangulations[i];
					float3_array v = position;
					snap_to_winding(lt->winding, lt->plane, v);
					float const dist = distance_between_points(v, position);
					if (best == nullptr || dist < bestdist - ON_EPSILON) {
						best = lt;
						bestdist = dist;
					}
				}

				if (best) {
					lt = best;
					spot = vector_subtract(position, lt->center);
					dot = dot_product(spot, lt->normal);
					spot = vector_fma(lt->normal, -dot, spot);
					CalcInterpolation(lt, spot, maininterp);

					maininterp.totalweight = 0;
					for (j = 0; j < (int) maininterp.points.size(); j++) {
						if (g_patches[maininterp.points[j].patchnum].flags
							== ePatchFlagOutside) {
							maininterp.points[j].weight *= 0.01;
						}
						maininterp.totalweight
							+= maininterp.points[j].weight;
					}
					ApplyInterpolation(maininterp, style, out);
					if (g_drawlerp) {
						// green
						out[0] = 0;
						out[1] = 100;
						out[2] = (maininterp.isbiased ? 0 : 100);
					}
				} else {
					maininterp.isbiased = true;
					maininterp.totalweight = 0;
					maininterp.points.resize(0);
					ApplyInterpolation(maininterp, style, out);
					if (g_drawlerp) {
						// black
						out[0] = 0;
						out[1] = 0;
						out[2] = 0;
					}
				}
			}
		}

		for (i = 0; i < (int) localinterps.size(); i++) {
			delete localinterps[i];
		}

	} catch (std::bad_alloc const &) {
		hlassume(false, assume_NoMemory);
	}
}

static bool TestLineSegmentIntersectWall(
	facetriangulation_t const * facetrian,
	float3_array const & p1,
	float3_array const & p2
) {
	int i;
	facetriangulation_t::Wall const * wall;
	float front;
	float back;
	float dot1;
	float dot2;
	float dot;
	float bottom;
	float top;
	float frac;

	for (i = 0; i < (int) facetrian->walls.size(); i++) {
		wall = &facetrian->walls[i];
		bottom = dot_product(wall->points[0], wall->direction);
		top = dot_product(wall->points[1], wall->direction);
		front = dot_product(p1, wall->normal)
			- dot_product(wall->points[0], wall->normal);
		back = dot_product(p2, wall->normal)
			- dot_product(wall->points[0], wall->normal);
		if (front > ON_EPSILON && back > ON_EPSILON
			|| front < -ON_EPSILON && back < -ON_EPSILON) {
			continue;
		}
		dot1 = dot_product(p1, wall->direction);
		dot2 = dot_product(p2, wall->direction);
		if (fabs(front) <= 2 * ON_EPSILON && fabs(back) <= 2 * ON_EPSILON) {
			top = std::min(top, std::max(dot1, dot2));
			bottom = std::max(bottom, std::min(dot1, dot2));
		} else {
			frac = front / (front - back);
			frac = std::max((float) 0, std::min(frac, (float) 1));
			dot = dot1 + frac * (dot2 - dot1);
			top = std::min(top, dot);
			bottom = std::max(bottom, dot);
		}
		if (top - bottom >= -ON_EPSILON) {
			return true;
		}
	}

	return false;
}

static bool TestFarPatch(
	localtriangulation_t const * lt,
	float3_array const & p2,
	fast_winding const & p2winding
) {
	int i;
	float size1;
	float size2;

	size1 = 0;
	for (i = 0; i < lt->winding.size(); i++) {
		float const dist = distance_between_points(
			lt->winding.point(i), lt->center
		);
		if (dist > size1) {
			size1 = dist;
		}
	}

	size2 = 0;
	for (i = 0; i < p2winding.size(); i++) {
		float const dist = distance_between_points(p2winding.point(i), p2);
		if (dist > size2) {
			size2 = dist;
		}
	}

	float const dist = distance_between_points(p2, lt->center);

	return dist > 1.4 * (size1 + size2);
}

constexpr float TRIANGLE_SHAPE_THRESHOLD = 115.0f
	* std::numbers::pi_v<float> / 180;

// If one of the angles in a triangle exceeds this threshold, the most
// distant point will be removed or the triangle will break into a
// convex-type wedge.

static void GatherPatches(
	localtriangulation_t* lt, facetriangulation_t const * facetrian
) {
	int i;
	int facenum2;
	dplane_t const * dp2;
	patch_t const * patch2;
	int patchnum2;
	float3_array v;
	localtriangulation_t::Wedge point;
	std::vector<localtriangulation_t::Wedge> points;
	std::vector<std::pair<float, int>> angles;
	float angle;

	if (!g_lerp_enabled) {
		lt->sortedwedges.resize(0);
		return;
	}

	points.resize(0);
	for (i = 0; i < (int) lt->neighborfaces.size(); i++) {
		facenum2 = lt->neighborfaces[i];
		dp2 = getPlaneFromFaceNumber(facenum2);
		for (patch2 = g_face_patches[facenum2]; patch2;
			 patch2 = patch2->next) {
			patchnum2 = patch2 - g_patches;

			point.leftpatchnum = patchnum2;
			v = vector_fma(dp2->normal, -PATCH_HUNT_OFFSET, patch2->origin);

			// Do permission tests using the original position of the patch
			if (patchnum2 == lt->patchnum
				|| point_in_winding(lt->winding, lt->plane, v)) {
				continue;
			}
			if (facenum2 != facetrian->facenum
				&& TestLineSegmentIntersectWall(facetrian, lt->center, v)) {
				continue;
			}
			if (TestFarPatch(lt, v, *patch2->winding)) {
				continue;
			}

			// Store the adapted position of the patch
			if (!CalcAdaptedSpot(lt, v, facenum2, point.leftspot)) {
				continue;
			}
			if (GetDirection(
					point.leftspot, lt->normal, point.leftdirection
				)
				<= 2 * ON_EPSILON) {
				continue;
			}
			points.push_back(point);
		}
	}

	// Sort the patches into clockwise order
	angles.resize((int) points.size());
	for (i = 0; i < (int) points.size(); i++) {
		angle = GetAngle(
			points[0].leftdirection, points[i].leftdirection, lt->normal
		);
		if (i == 0) {
			if (g_drawlerp && fabs(angle) > NORMAL_EPSILON) {
				Developer(
					developer_level::spam,
					"Debug: triangulation: internal error 7.\n"
				);
			}
			angle = 0.0;
		}
		angles[i].first = GetAngleDiff(angle, 0);
		angles[i].second = i;
	}
	std::sort(angles.begin(), angles.end());

	lt->sortedwedges.resize((int) points.size());
	for (i = 0; i < (int) points.size(); i++) {
		lt->sortedwedges[i] = points[angles[i].second];
	}
}

static void PurgePatches(localtriangulation_t* lt) {
	std::vector<localtriangulation_t::Wedge> points;
	int i;
	int cur;
	std::vector<int> next;
	std::vector<int> prev;
	std::vector<int> valid;
	std::vector<std::pair<float, int>> dists;
	float angle;
	float3_array normal;
	float3_array v;

	points.swap(lt->sortedwedges);
	lt->sortedwedges.resize(0);

	next.resize((int) points.size());
	prev.resize((int) points.size());
	valid.resize((int) points.size());
	dists.resize((int) points.size());
	for (i = 0; i < (int) points.size(); i++) {
		next[i] = (i + 1) % (int) points.size();
		prev[i] = (i - 1 + (int) points.size()) % (int) points.size();
		valid[i] = 1;
		dists[i].first = dot_product(
			points[i].leftspot, points[i].leftdirection
		);
		dists[i].second = i;
	}
	std::sort(dists.begin(), dists.end());

	for (i = 0; i < (int) points.size(); i++) {
		cur = dists[i].second;
		if (valid[cur] == 0) {
			continue;
		}
		valid[cur] = 2; // mark current patch as final

		normal = cross_product(points[cur].leftdirection, lt->normal);
		normalize_vector(normal);
		// TODO: Do we really need `double` intermediate values here? Might
		// be an accident
		v = to_float3(vector_fma(
			points[cur].leftdirection,
			std::sin(TRIANGLE_SHAPE_THRESHOLD),
			vector_scale(normal, std::cos(TRIANGLE_SHAPE_THRESHOLD))
		));
		while (next[cur] != cur && valid[next[cur]] != 2) {
			angle = GetAngle(
				points[cur].leftdirection,
				points[next[cur]].leftdirection,
				lt->normal
			);
			if (fabs(angle) <= (1.0 * std::numbers::pi_v<float> / 180)
				|| GetAngleDiff(angle, 0) <= std::numbers::pi_v<double>
							+ NORMAL_EPSILON
					&& dot_product(points[next[cur]].leftspot, v)
						>= dot_product(points[cur].leftspot, v)
							- ON_EPSILON / 2) {
				// remove next patch
				valid[next[cur]] = 0;
				next[cur] = next[next[cur]];
				prev[next[cur]] = cur;
				continue;
			}
			// the triangle is good
			break;
		}

		normal = cross_product(lt->normal, points[cur].leftdirection);
		normalize_vector(normal);
		// TODO: Do we really need `double` intermediate values here? Might
		// be an accident
		v = to_float3(vector_fma(
			points[cur].leftdirection,
			std::sin(TRIANGLE_SHAPE_THRESHOLD),
			vector_scale(normal, std::cos(TRIANGLE_SHAPE_THRESHOLD))
		));
		while (prev[cur] != cur && valid[prev[cur]] != 2) {
			angle = GetAngle(
				points[prev[cur]].leftdirection,
				points[cur].leftdirection,
				lt->normal
			);
			if (fabs(angle) <= (1.0 * std::numbers::pi_v<float> / 180)
				|| GetAngleDiff(angle, 0) <= std::numbers::pi_v<double>
							+ NORMAL_EPSILON
					&& dot_product(points[prev[cur]].leftspot, v)
						>= dot_product(points[cur].leftspot, v)
							- ON_EPSILON / 2) {
				// remove previous patch
				valid[prev[cur]] = 0;
				prev[cur] = prev[prev[cur]];
				next[prev[cur]] = cur;
				continue;
			}
			// the triangle is good
			break;
		}
	}

	for (i = 0; i < (int) points.size(); i++) {
		if (valid[i] == 2) {
			lt->sortedwedges.push_back(points[i]);
		}
	}
}

static void PlaceHullPoints(localtriangulation_t* lt) {
	int i;
	int j;
	int n;
	float dot;
	float angle;
	localtriangulation_t::HullPoint hp;
	std::vector<localtriangulation_t::HullPoint> spots;
	std::vector<std::pair<float, int>> angles;
	localtriangulation_t::Wedge const * w;
	localtriangulation_t::Wedge const * wnext;
	std::vector<localtriangulation_t::HullPoint> arc_spots;
	std::vector<float> arc_angles;
	std::vector<int> next;
	std::vector<int> prev;
	float frac;
	float len;
	float dist;

	spots.reserve(lt->winding.size());
	spots.resize(0);
	for (i = 0; i < (int) lt->winding.size(); i++) {
		float3_array const v = vector_subtract(
			lt->winding.point(i), lt->center
		);
		dot = dot_product(v, lt->normal);
		hp.spot = vector_fma(lt->normal, -dot, v);
		if (!GetDirection(hp.spot, lt->normal, hp.direction)) {
			continue;
		}
		spots.push_back(hp);
	}

	if ((int) lt->sortedwedges.size() == 0) {
		angles.resize((int) spots.size());
		for (i = 0; i < (int) spots.size(); i++) {
			angle = GetAngle(
				spots[0].direction, spots[i].direction, lt->normal
			);
			if (i == 0) {
				angle = 0.0;
			}
			angles[i].first = GetAngleDiff(angle, 0);
			angles[i].second = i;
		}
		std::sort(angles.begin(), angles.end());
		lt->sortedhullpoints.resize(0);
		for (i = 0; i < (int) spots.size(); i++) {
			if (g_drawlerp && angles[i].second != i) {
				Developer(
					developer_level::spam,
					"Debug: triangulation: internal error 8.\n"
				);
			}
			lt->sortedhullpoints.push_back(spots[angles[i].second]);
		}
		return;
	}

	lt->sortedhullpoints.resize(0);
	for (i = 0; i < (int) lt->sortedwedges.size(); i++) {
		w = &lt->sortedwedges[i];
		wnext = &lt->sortedwedges[(i + 1) % (int) lt->sortedwedges.size()];

		angles.resize((int) spots.size());
		for (j = 0; j < (int) spots.size(); j++) {
			angle = GetAngle(
				w->leftdirection, spots[j].direction, lt->normal
			);
			angles[j].first = GetAngleDiff(angle, 0);
			angles[j].second = j;
		}
		std::sort(angles.begin(), angles.end());
		angle = GetAngle(
			w->leftdirection, wnext->leftdirection, lt->normal
		);
		if ((int) lt->sortedwedges.size() == 1) {
			angle = 2 * std::numbers::pi_v<double>;
		} else {
			angle = GetAngleDiff(angle, 0);
		}

		arc_spots.resize((int) spots.size() + 2);
		arc_angles.resize((int) spots.size() + 2);
		next.resize((int) spots.size() + 2);
		prev.resize((int) spots.size() + 2);

		arc_spots[0].spot = w->leftspot;
		arc_spots[0].direction = w->leftdirection;
		arc_angles[0] = 0;
		next[0] = 1;
		prev[0] = -1;
		n = 1;
		for (j = 0; j < (int) spots.size(); j++) {
			if (NORMAL_EPSILON <= angles[j].first
				&& angles[j].first <= angle - NORMAL_EPSILON) {
				arc_spots[n] = spots[angles[j].second];
				arc_angles[n] = angles[j].first;
				next[n] = n + 1;
				prev[n] = n - 1;
				n++;
			}
		}
		arc_spots[n].spot = wnext->leftspot;
		arc_spots[n].direction = wnext->leftdirection;
		arc_angles[n] = angle;
		next[n] = -1;
		prev[n] = n - 1;
		n++;

		for (j = 1; next[j] != -1; j = next[j]) {
			while (prev[j] != -1) {
				if (arc_angles[next[j]] - arc_angles[prev[j]]
					<= std::numbers::pi_v<double> + NORMAL_EPSILON) {
					frac = GetFrac(
						arc_spots[prev[j]].spot,
						arc_spots[next[j]].spot,
						arc_spots[j].direction,
						lt->normal
					);
					len = (1 - frac)
							* dot_product(
								arc_spots[prev[j]].spot,
								arc_spots[j].direction
							)
						+ frac
							* dot_product(
								arc_spots[next[j]].spot,
								arc_spots[j].direction
							);
					dist = dot_product(
						arc_spots[j].spot, arc_spots[j].direction
					);
					if (dist <= len + NORMAL_EPSILON) {
						j = prev[j];
						next[j] = next[next[j]];
						prev[next[j]] = j;
						continue;
					}
				}
				break;
			}
		}

		for (j = 0; next[j] != -1; j = next[j]) {
			lt->sortedhullpoints.push_back(arc_spots[j]);
		}
	}
}

static bool TryMakeSquare(localtriangulation_t* lt, int i) {
	localtriangulation_t::Wedge* w1;
	localtriangulation_t::Wedge* w2;
	localtriangulation_t::Wedge* w3;
	float3_array v;
	float3_array dir1;
	float3_array dir2;
	float angle;

	w1 = &lt->sortedwedges[i];
	w2 = &lt->sortedwedges[(i + 1) % (int) lt->sortedwedges.size()];
	w3 = &lt->sortedwedges[(i + 2) % (int) lt->sortedwedges.size()];

	// (o, p1, p2) and (o, p2, p3) must be triangles and not in a square
	if (w1->shape != localtriangulation_t::Wedge::eTriangular
		|| w2->shape != localtriangulation_t::Wedge::eTriangular) {
		return false;
	}

	// (o, p1, p3) must be a triangle
	angle = GetAngle(w1->leftdirection, w3->leftdirection, lt->normal);
	angle = GetAngleDiff(angle, 0);
	if (angle >= TRIANGLE_SHAPE_THRESHOLD) {
		return false;
	}

	// (p2, p1, p3) must be a triangle
	VectorSubtract(w1->leftspot, w2->leftspot, v);
	if (!GetDirection(v, lt->normal, dir1)) {
		return false;
	}
	VectorSubtract(w3->leftspot, w2->leftspot, v);
	if (!GetDirection(v, lt->normal, dir2)) {
		return false;
	}
	angle = GetAngle(dir2, dir1, lt->normal);
	angle = GetAngleDiff(angle, 0);
	if (angle >= TRIANGLE_SHAPE_THRESHOLD) {
		return false;
	}

	w1->shape = localtriangulation_t::Wedge::eSquareLeft;
	w1->wedgenormal = negate_vector(dir1);
	w2->shape = localtriangulation_t::Wedge::eSquareRight;
	w2->wedgenormal = dir2;
	return true;
}

static void FindSquares(localtriangulation_t* lt) {
	int i;
	localtriangulation_t::Wedge* w;
	std::vector<std::pair<float, int>> dists;

	if ((int) lt->sortedwedges.size() <= 2) {
		return;
	}

	dists.resize((int) lt->sortedwedges.size());
	for (i = 0; i < (int) lt->sortedwedges.size(); i++) {
		w = &lt->sortedwedges[i];
		dists[i].first = dot_product(w->leftspot, w->leftdirection);
		dists[i].second = i;
	}
	std::sort(dists.begin(), dists.end());

	for (i = 0; i < (int) lt->sortedwedges.size(); i++) {
		TryMakeSquare(lt, dists[i].second);
		TryMakeSquare(
			lt,
			(dists[i].second - 2 + (int) lt->sortedwedges.size())
				% (int) lt->sortedwedges.size()
		);
	}
}

static localtriangulation_t* CreateLocalTriangulation(
	facetriangulation_t const * facetrian, int patchnum
) {
	localtriangulation_t* lt;
	int i;
	patch_t const * patch;
	float dot;
	int facenum;
	localtriangulation_t::Wedge* w;
	localtriangulation_t::Wedge* wnext;
	float angle;
	float total;
	float3_array normal;

	facenum = facetrian->facenum;
	patch = &g_patches[patchnum];
	lt = new localtriangulation_t;

	// Fill basic information for this local triangulation
	lt->plane = *getPlaneFromFaceNumber(facenum);
	lt->plane.dist += dot_product(g_face_offset[facenum], lt->plane.normal);
	lt->winding = *patch->winding;
	lt->center = vector_fma(
		lt->plane.normal, -PATCH_HUNT_OFFSET, patch->origin
	);
	dot = dot_product(lt->center, lt->plane.normal) - lt->plane.dist;
	lt->center = vector_fma(lt->plane.normal, -dot, lt->center);
	if (!point_in_winding_noedge(
			lt->winding, lt->plane, lt->center, DEFAULT_EDGE_WIDTH
		)) {
		snap_to_winding_noedge(
			lt->winding,
			lt->plane,
			lt->center,
			DEFAULT_EDGE_WIDTH,
			4 * DEFAULT_EDGE_WIDTH
		);
	}
	lt->normal = lt->plane.normal;
	lt->patchnum = patchnum;
	lt->neighborfaces = facetrian->neighbors;

	// Gather all patches from nearby faces
	GatherPatches(lt, facetrian);

	// Remove distant patches
	PurgePatches(lt);

	// Calculate wedge types
	total = 0.0;
	for (i = 0; i < (int) lt->sortedwedges.size(); i++) {
		w = &lt->sortedwedges[i];
		wnext = &lt->sortedwedges[(i + 1) % (int) lt->sortedwedges.size()];

		angle = GetAngle(
			w->leftdirection, wnext->leftdirection, lt->normal
		);
		if (g_drawlerp
			&& ((int) lt->sortedwedges.size() >= 2
				&& fabs(angle) <= (0.9f * std::numbers::pi_v<float> / 180)
			)) {
			Developer(
				developer_level::spam,
				"Debug: triangulation: internal error 9.\n"
			);
		}
		angle = GetAngleDiff(angle, 0);
		if ((int) lt->sortedwedges.size() == 1) {
			angle = 2 * std::numbers::pi_v<double>;
		}
		total += angle;

		if (angle <= std::numbers::pi_v<double> + NORMAL_EPSILON) {
			if (angle < TRIANGLE_SHAPE_THRESHOLD) {
				w->shape = localtriangulation_t::Wedge::eTriangular;
				w->wedgenormal = {};
			} else {
				w->shape = localtriangulation_t::Wedge::eConvex;
				float3_array const v{
					vector_subtract(wnext->leftspot, w->leftspot)
				};
				GetDirection(v, lt->normal, w->wedgenormal);
			}
		} else {
			w->shape = localtriangulation_t::Wedge::eConcave;
			float3_array const v{
				vector_add(wnext->leftdirection, w->leftdirection)
			};
			normal = cross_product(lt->normal, v);
			normal = vector_add(
				normal,
				vector_subtract(wnext->leftdirection, w->leftdirection)
			);
			GetDirection(normal, lt->normal, w->wedgenormal);
			if (g_drawlerp && vector_length(w->wedgenormal) == 0) {
				Developer(
					developer_level::spam,
					"Debug: triangulation: internal error 10.\n"
				);
			}
		}
	}
	if (g_drawlerp
		&& ((int) lt->sortedwedges.size() > 0
			&& fabs(total - 2 * std::numbers::pi_v<double>)
				> 10 * NORMAL_EPSILON)) {
		Developer(
			developer_level::spam,
			"Debug: triangulation: internal error 11.\n"
		);
	}
	FindSquares(lt);

	// Calculate hull points
	PlaceHullPoints(lt);

	return lt;
}

static void FreeLocalTriangulation(localtriangulation_t* lt) {
	delete lt;
}

static void FindNeighbors(facetriangulation_t* facetrian) {
	int i;
	int j;
	int e;
	edgeshare_t const * es;
	int side;
	facelist_t const * fl;
	int facenum;
	int facenum2;
	dface_t const * f;
	dface_t const * f2;
	dplane_t const * dp;
	dplane_t const * dp2;

	facenum = facetrian->facenum;
	f = &g_dfaces[facenum];
	dp = getPlaneFromFace(f);

	facetrian->neighbors.resize(0);

	facetrian->neighbors.push_back(facenum);

	for (i = 0; i < f->numedges; i++) {
		e = g_dsurfedges[f->firstedge + i];
		es = &g_edgeshare[abs(e)];
		if (!es->smooth) {
			continue;
		}
		f2 = es->faces[e > 0 ? 1 : 0];
		facenum2 = f2 - g_dfaces.data();
		dp2 = getPlaneFromFace(f2);
		if (dot_product(dp->normal, dp2->normal) < -NORMAL_EPSILON) {
			continue;
		}
		for (j = 0; j < (int) facetrian->neighbors.size(); j++) {
			if (facetrian->neighbors[j] == facenum2) {
				break;
			}
		}
		if (j == (int) facetrian->neighbors.size()) {
			facetrian->neighbors.push_back(facenum2);
		}
	}

	for (i = 0; i < f->numedges; i++) {
		e = g_dsurfedges[f->firstedge + i];
		es = &g_edgeshare[abs(e)];
		if (!es->smooth) {
			continue;
		}
		for (side = 0; side < 2; side++) {
			for (fl = es->vertex_facelist[side]; fl; fl = fl->next) {
				f2 = fl->face;
				facenum2 = f2 - g_dfaces.data();
				dp2 = getPlaneFromFace(f2);
				if (dot_product(dp->normal, dp2->normal)
					< -NORMAL_EPSILON) {
					continue;
				}
				for (j = 0; j < (int) facetrian->neighbors.size(); j++) {
					if (facetrian->neighbors[j] == facenum2) {
						break;
					}
				}
				if (j == (int) facetrian->neighbors.size()) {
					facetrian->neighbors.push_back(facenum2);
				}
			}
		}
	}
}

static void BuildWalls(facetriangulation_t* facetrian) {
	int i;
	int j;
	int facenum;
	int facenum2;
	dface_t const * f;
	dface_t const * f2;
	dplane_t const * dp;
	dplane_t const * dp2;
	int e;
	edgeshare_t const * es;
	float dot;

	facenum = facetrian->facenum;
	f = &g_dfaces[facenum];
	dp = getPlaneFromFace(f);

	facetrian->walls.resize(0);

	for (i = 0; i < (int) facetrian->neighbors.size(); i++) {
		facenum2 = facetrian->neighbors[i];
		f2 = &g_dfaces[facenum2];
		dp2 = getPlaneFromFace(f2);
		if (dot_product(dp->normal, dp2->normal) <= 0.1f) {
			continue;
		}
		for (j = 0; j < f2->numedges; j++) {
			e = g_dsurfedges[f2->firstedge + j];
			es = &g_edgeshare[abs(e)];
			if (!es->smooth) {
				facetriangulation_t::Wall wall;

				wall.points[0] = vector_add(
					g_dvertexes[g_dedges[abs(e)].v[0]].point,
					g_face_offset[facenum]
				);
				wall.points[1] = vector_add(
					g_dvertexes[g_dedges[abs(e)].v[1]].point,
					g_face_offset[facenum]
				);
				wall.direction = vector_subtract(
					wall.points[1], wall.points[0]
				);
				dot = dot_product(wall.direction, dp->normal);
				wall.direction = vector_fma(
					dp->normal, -dot, wall.direction
				);
				if (normalize_vector(wall.direction)) {
					wall.normal = cross_product(wall.direction, dp->normal);
					normalize_vector(wall.normal);
					facetrian->walls.push_back(wall);
				}
			}
		}
	}
}

static void CollectUsedPatches(facetriangulation_t* facetrian) {
	int i;
	int j;
	int k;
	int patchnum;
	localtriangulation_t const * lt;
	localtriangulation_t::Wedge const * w;

	facetrian->usedpatches.resize(0);
	for (i = 0; i < (int) facetrian->localtriangulations.size(); i++) {
		lt = facetrian->localtriangulations[i];

		patchnum = lt->patchnum;
		for (k = 0; k < (int) facetrian->usedpatches.size(); k++) {
			if (facetrian->usedpatches[k] == patchnum) {
				break;
			}
		}
		if (k == (int) facetrian->usedpatches.size()) {
			facetrian->usedpatches.push_back(patchnum);
		}

		for (j = 0; j < (int) lt->sortedwedges.size(); j++) {
			w = &lt->sortedwedges[j];

			patchnum = w->leftpatchnum;
			for (k = 0; k < (int) facetrian->usedpatches.size(); k++) {
				if (facetrian->usedpatches[k] == patchnum) {
					break;
				}
			}
			if (k == (int) facetrian->usedpatches.size()) {
				facetrian->usedpatches.push_back(patchnum);
			}
		}
	}
}

// =====================================================================================
//  CreateTriangulations
// =====================================================================================
void CreateTriangulations(int facenum) {
	try {
		facetriangulation_t* facetrian;
		int patchnum;
		patch_t const * patch;
		localtriangulation_t* lt;

		g_facetriangulations[facenum] = new facetriangulation_t;
		facetrian = g_facetriangulations[facenum];

		facetrian->facenum = facenum;

		// Find neighbors
		FindNeighbors(facetrian);

		// Build walls
		BuildWalls(facetrian);

		// Create local triangulation around each patch
		facetrian->localtriangulations.resize(0);
		for (patch = g_face_patches[facenum]; patch; patch = patch->next) {
			patchnum = patch - g_patches;
			lt = CreateLocalTriangulation(facetrian, patchnum);
			facetrian->localtriangulations.push_back(lt);
		}

		// Collect used patches
		CollectUsedPatches(facetrian);

	} catch (std::bad_alloc const &) {
		hlassume(false, assume_NoMemory);
	}
}

// =====================================================================================
//  GetTriangulationPatches
// =====================================================================================
void GetTriangulationPatches(
	int facenum, int* numpatches, int const ** patches
) {
	facetriangulation_t const * facetrian;

	facetrian = g_facetriangulations[facenum];
	*numpatches = (int) facetrian->usedpatches.size();
	*patches = facetrian->usedpatches.data();
}

// =====================================================================================
//  FreeTriangulations
// =====================================================================================
void FreeTriangulations() {
	try {
		int i;
		int j;
		facetriangulation_t* facetrian;

		for (i = 0; i < g_numfaces; i++) {
			facetrian = g_facetriangulations[i];

			for (j = 0; j < (int) facetrian->localtriangulations.size();
				 j++) {
				FreeLocalTriangulation(facetrian->localtriangulations[j]);
			}

			delete facetrian;
			g_facetriangulations[i] = nullptr;
		}

	} catch (std::bad_alloc const &) {
		hlassume(false, assume_NoMemory);
	}
}
