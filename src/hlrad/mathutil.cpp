#include "qrad.h"

#include <numbers>

// =====================================================================================
//  point_in_winding
//      returns whether the point is in the winding (including its edges)
//      the point and all the vertexes of the winding can move freely along
//      the plane's normal without changing the result
// =====================================================================================
bool point_in_winding(
	fast_winding const & w,
	dplane_t const & plane,
	float3_array const & point,
	float epsilon /* = 0.0*/
) {
	int const numpoints = w.size();

	for (int x = 0; x < numpoints; x++) {
		float3_array const delta = vector_subtract(
			w.point((x + 1) % numpoints), w.point(x)
		);
		float3_array const normal = cross_product(delta, plane.normal);
		float const dist = dot_product(point, normal)
			- dot_product(w.point(x), normal);

		if (dist < 0.0
			&& (epsilon == 0.0
				|| dist * dist
					> epsilon * epsilon * dot_product(normal, normal))) {
			return false;
		}
	}

	return true;
}

// =====================================================================================
//  point_in_winding_noedge
//      assume a ball is created from the point, this function checks
//      whether the ball is entirely inside the winding parameter 'width' :
//      the radius of the ball the point and all the vertexes of the winding
//      can move freely along the plane's normal without changing the result
// =====================================================================================
bool point_in_winding_noedge(
	fast_winding const & w,
	dplane_t const & plane,
	float3_array const & point,
	float width
) {
	int numpoints;
	int x;

	numpoints = w.size();

	for (x = 0; x < numpoints; x++) {
		float3_array const delta = vector_subtract(
			w.point_after(x, 1), w.point(x)
		);
		float3_array const normal = cross_product(delta, plane.normal);
		float const dist = dot_product(point, normal)
			- dot_product(w.point(x), normal);

		if (dist < 0.0
			|| dist * dist <= width * width * dot_product(normal, normal)) {
			return false;
		}
	}

	return true;
}

// =====================================================================================
//  snap_to_winding
//      moves the point to the nearest point inside the winding
//      if the point is not on the plane, the distance between the point and
//      the plane is preserved the point and all the vertexes of the winding
//      can move freely along the plane's normal without changing the result
// =====================================================================================
void snap_to_winding(
	fast_winding const & w, dplane_t const & plane, float3_array& point
) {
	int numpoints;
	int x;

	float dist;
	float dot1, dot2, dot;
	float3_array bestpoint;
	float bestdist;
	bool in;

	numpoints = w.size();

	in = true;
	for (x = 0; x < numpoints; x++) {
		float3_array const & p1 = w.point(x);
		float3_array const & p2 = w.point((x + 1) % numpoints);
		float3_array delta = vector_subtract(p2, p1);
		float3_array const normal = cross_product(delta, plane.normal);
		dist = dot_product(point, normal) - dot_product(p1, normal);

		if (dist < 0.0) {
			in = false;

			delta = cross_product(plane.normal, normal);
			dot = dot_product(delta, point);
			dot1 = dot_product(delta, p1);
			dot2 = dot_product(delta, p2);
			if (dot1 < dot && dot < dot2) {
				dist = dist / dot_product(normal, normal);
				point = vector_fma(normal, -dist, point);
				return;
			}
		}
	}
	if (in) {
		return;
	}
	for (x = 0; x < numpoints; x++) {
		float3_array const & p1 = w.point(x);
		float3_array delta = vector_subtract(p1, point);
		dist = dot_product(delta, plane.normal)
			/ dot_product(plane.normal, plane.normal);
		delta = vector_fma(plane.normal, -dist, delta);
		dot = dot_product(delta, delta);

		if (x == 0 || dot < bestdist) {
			bestpoint = vector_add(point, delta);
			bestdist = dot;
		}
	}
	if (numpoints > 0) {
		point = bestpoint;
	}
	return;
}

// =====================================================================================
//  snap_to_winding_noedge
//      first snaps the point into the winding
//      then moves the point towards the inside for at most certain distance
//      until:
//        either 1) the point is not close to any of the edges
//        or     2) the point can not be moved any more
//      returns the maximal distance that the point can be kept away from
//      all the edges in most of the cases, the maximal distance = width; in
//      other cases, the maximal distance < width
// =====================================================================================
float snap_to_winding_noedge(
	fast_winding const & w,
	dplane_t const & plane,
	float3_array& point,
	float width,
	float maxmove
) {
	int pass;
	int numplanes;
	dplane_t* planes;
	int x;
	float newwidth;
	float bestwidth;
	float3_array bestpoint;

	snap_to_winding(w, plane, point);

	planes = (dplane_t*) malloc(w.size() * sizeof(dplane_t));
	hlassume(planes != nullptr, assume_NoMemory);
	numplanes = 0;
	for (x = 0; x < w.size(); x++) {
		float3_array const v = vector_subtract(
			w.point_after(x, 1), w.point(x)
		);
		planes[numplanes].normal = cross_product(v, plane.normal);
		if (!normalize_vector(planes[numplanes].normal)) {
			continue;
		}
		planes[numplanes].dist = dot_product(
			w.point(x), planes[numplanes].normal
		);
		numplanes++;
	}

	bestwidth = 0;
	bestpoint = point;
	newwidth = width;

	for (pass = 0; pass < 5;
		 pass++) // apply binary search method for 5 iterations to find the
				 // maximal distance that the point can be kept away from
				 // all the edges
	{
		float3_array newpoint;

		bool failed = true;

		fast_winding newwinding{ w };
		for (x = 0; x < numplanes && !newwinding.empty(); x++) {
			dplane_t clipplane = planes[x];
			clipplane.dist += newwidth;
			newwinding.mutating_clip(
				clipplane.normal, clipplane.dist, false
			);
		}

		if (!newwinding.empty()) {
			newpoint = point;
			snap_to_winding(newwinding, plane, newpoint);

			if (distance_between_points(newpoint, point)
				<= maxmove + ON_EPSILON) {
				failed = false;
			}
		}

		if (!failed) {
			bestwidth = newwidth;
			bestpoint = newpoint;
			if (pass == 0) {
				break;
			}
			newwidth += width * pow(0.5, pass + 1);
		} else {
			newwidth -= width * pow(0.5, pass + 1);
		}
	}

	free(planes);

	point = bestpoint;
	return bestwidth;
}

bool intersect_linesegment_plane(
	dplane_t const & plane,
	float3_array const & p1,
	float3_array const & p2,
	float3_array& point
) {
	float const part1 = dot_product(p1, plane.normal) - plane.dist;
	float const part2 = dot_product(p2, plane.normal) - plane.dist;
	if (part1 * part2 > 0 || part1 == part2) {
		return false;
	}
	for (std::size_t i = 0; i < 3; ++i) {
		point[i] = (part1 * p2[i] - part2 * p1[i]) / (part1 - part2);
	}
	return true;
}

void plane_from_points(
	float3_array const & p1,
	float3_array const & p2,
	float3_array const & p3,
	dplane_t& plane
) {
	float3_array const delta1 = vector_subtract(p3, p2);
	float3_array const delta2 = vector_subtract(p1, p2);
	float3_array normal = cross_product(delta1, delta2);
	normalize_vector(normal);
	plane.dist = dot_product(normal, p1);
	plane.normal = normal;
}

// LineSegmentIntersectsBounds --vluzacn
bool LineSegmentIntersectsBounds_r(
	float const * p1,
	float const * p2,
	float const * mins,
	float const * maxs,
	int d
) {
	float lmin, lmax;
	float const * tmp;
	int i;
	d--;
	if (p2[d] < p1[d]) {
		tmp = p1, p1 = p2, p2 = tmp;
	}
	if (p2[d] < mins[d] || p1[d] > maxs[d]) {
		return false;
	}
	if (d == 0) {
		return true;
	}
	lmin = p1[d] >= mins[d] ? 0 : (mins[d] - p1[d]) / (p2[d] - p1[d]);
	lmax = p2[d] <= maxs[d] ? 1 : (p2[d] - maxs[d]) / (p2[d] - p1[d]);
	float3_array x1, x2;
	for (i = 0; i < d; ++i) {
		x1[i] = (1 - lmin) * p1[i] + lmin * p2[i];
		x2[i] = (1 - lmax) * p2[i] + lmax * p2[i];
	}
	return LineSegmentIntersectsBounds_r(
		x1.data(), x2.data(), mins, maxs, d
	);
}

inline bool LineSegmentIntersectsBounds(
	float3_array const & p1,
	float3_array const & p2,
	float3_array const & mins,
	float3_array const & maxs
) {
	return LineSegmentIntersectsBounds_r(
		p1.data(), p2.data(), mins.data(), maxs.data(), 3
	);
}

// =====================================================================================
//  TestSegmentAgainstOpaqueList
//      Returns true if the segment intersects an item in the opaque list
// =====================================================================================
bool TestSegmentAgainstOpaqueList(
	float3_array const & p1,
	float3_array const & p2,
	float3_array& scaleout,
	int& opaquestyleout // light must convert to this style. -1 = no convert
) {
	scaleout.fill(1.0);
	opaquestyleout = -1;
	for (int x = 0; x < g_opaque_face_list.size(); x++) {
		if (!TestLineOpaque(
				g_opaque_face_list[x].modelnum,
				g_opaque_face_list[x].origin,
				p1,
				p2
			)) {
			continue;
		}
		if (g_opaque_face_list[x].transparency) {
			scaleout = vector_multiply(
				scaleout, g_opaque_face_list[x].transparency_scale
			);
			continue;
		}
		if (g_opaque_face_list[x].style != -1
			&& (opaquestyleout == -1
				|| g_opaque_face_list[x].style == opaquestyleout)) {
			opaquestyleout = g_opaque_face_list[x].style;
			continue;
		}
		scaleout = {};
		opaquestyleout = -1;
		return true;
	}
	if (TestSegmentAgainstStudioList(p1, p2)) // seedee
	{
		scaleout = {};
		opaquestyleout = -1;
		return true;
	}
	if (scaleout[0] < 0.01 && scaleout[1] < 0.01 && scaleout[2] < 0.01) {
		return true; // so much shadowing that result is same as with normal
					 // opaque face
	}
	return false;
}

float3_array snap_point_to_plane(
	dplane_t const * const plane, float3_array const & point, float offset
) noexcept {
	float dist = dot_product(point, plane->normal) - plane->dist;
	dist -= offset;
	return vector_fma(plane->normal, -dist, point);
}

// =====================================================================================
//  CalcSightArea
// =====================================================================================
float CalcSightArea(
	float3_array const & receiver_origin,
	float3_array const & receiver_normal,
	fast_winding const * emitter_winding,
	int skylevel,
	float lighting_power,
	float lighting_scale
) {
	// maybe there are faster ways in calculating the weighted area, but at
	// least this way is not bad.
	float area = 0.0;

	std::size_t numedges = emitter_winding->size();
	float3_array* edges = (float3_array*) malloc(
		numedges * sizeof(float3_array)
	);
	hlassume(edges != nullptr, assume_NoMemory);
	bool error = false;
	for (std::size_t x = 0; x < numedges; x++) {
		float3_array const v1 = vector_subtract(
			emitter_winding->point(x), receiver_origin
		);
		float3_array const v2 = vector_subtract(
			emitter_winding->point_after(x, 1), receiver_origin
		);
		float3_array normal = cross_product(v1, v2); // pointing inward
		if (!normalize_vector(normal)) {
			error = true;
		}
		edges[x] = normal;
	}
	if (!error) {
		int i, j;
		float3_array* pnormal;
		float* psize;
		float dot;
		float3_array* pedge;
		for (i = 0,
			pnormal = g_skynormals[skylevel],
			psize = g_skynormalsizes[skylevel];
			 i < g_numskynormals[skylevel];
			 i++, pnormal++, psize++) {
			dot = dot_product(*pnormal, receiver_normal);
			if (dot <= 0) {
				continue;
			}
			for (j = 0, pedge = edges; j < numedges; j++, pedge++) {
				if (dot_product(*pnormal, *pedge) <= 0) {
					break;
				}
			}
			if (j < numedges) {
				continue;
			}
			if (lighting_power != 1.0) {
				dot = pow(dot, lighting_power);
			}
			area += dot * (*psize);
		}
		area = area * 4
			* std::numbers::pi_v<float>; // Convert to absolute sphere area
	}
	free(edges);
	area *= lighting_scale;
	return area;
}

float CalcSightArea_SpotLight(
	float3_array const & receiver_origin,
	float3_array const & receiver_normal,
	fast_winding const * emitter_winding,
	float3_array const & emitter_normal,
	float emitter_stopdot,
	float emitter_stopdot2,
	int skylevel,
	float lighting_power,
	float lighting_scale
) {
	// stopdot = cos (cone)
	// stopdot2 = cos (cone2)
	// stopdot >= stopdot2 >= 0
	// ratio = 1.0 , when dot2 >= stopdot
	// ratio = (dot - stopdot2) / (stopdot - stopdot2) , when stopdot > dot2
	// > stopdot2 ratio = 0.0 , when stopdot2 >= dot2
	float area = 0.0;

	int numedges = emitter_winding->size();
	float3_array* edges = (float3_array*) malloc(
		numedges * sizeof(float3_array)
	);
	hlassume(edges != nullptr, assume_NoMemory);
	bool error = false;
	for (int x = 0; x < numedges; x++) {
		float3_array const v1 = vector_subtract(
			emitter_winding->point(x), receiver_origin
		);
		float3_array const v2 = vector_subtract(
			emitter_winding->point_after(x, 1), receiver_origin
		);
		float3_array normal = cross_product(v1, v2); // Pointing inward
		if (!normalize_vector(normal)) {
			error = true;
		}
		edges[x] = normal;
	}
	if (!error) {
		int i, j;
		float3_array* pnormal;
		float* psize;
		float dot;
		float dot2;
		float3_array* pedge;
		for (i = 0,
			pnormal = g_skynormals[skylevel],
			psize = g_skynormalsizes[skylevel];
			 i < g_numskynormals[skylevel];
			 i++, pnormal++, psize++) {
			dot = dot_product(*pnormal, receiver_normal);
			if (dot <= 0) {
				continue;
			}
			for (j = 0, pedge = edges; j < numedges; j++, pedge++) {
				if (dot_product(*pnormal, *pedge) <= 0) {
					break;
				}
			}
			if (j < numedges) {
				continue;
			}
			if (lighting_power != 1.0) {
				dot = pow(dot, lighting_power);
			}
			dot2 = -dot_product(*pnormal, emitter_normal);
			if (dot2 <= emitter_stopdot2 + NORMAL_EPSILON) {
				dot = 0;
			} else if (dot2 < emitter_stopdot) {
				dot = dot * (dot2 - emitter_stopdot2)
					/ (emitter_stopdot - emitter_stopdot2);
			}
			area += dot * (*psize);
		}
		area = area * 4
			* std::numbers::pi_v<float>; // Convert to absolute sphere area
	}
	free(edges);
	area *= lighting_scale;
	return area;
}

// =====================================================================================
//  GetAlternateOrigin
// =====================================================================================
void GetAlternateOrigin(
	float3_array const & pos,
	float3_array const & normal,
	patch_t const * patch,
	float3_array& origin
) {
	dplane_t const * faceplane;
	dplane_t clipplane;
	fast_winding w;

	faceplane = getPlaneFromFaceNumber(patch->faceNumber);
	float3_array const & faceplaneoffset = g_face_offset[patch->faceNumber];
	float3_array const & facenormal = faceplane->normal;
	clipplane.normal = normal;
	clipplane.dist = dot_product(pos, clipplane.normal);

	w = *patch->winding;
	if (w.WindingOnPlaneSide(clipplane.normal, clipplane.dist)
		!= face_side::cross) {
		origin = patch->origin;
	} else {
		w.mutating_clip(clipplane.normal, clipplane.dist, false);
		if (w.size() == 0) {
			origin = patch->origin;
		} else {
			bool found;
			float3_array bestpoint;
			float bestdist = -1.0;
			float3_array point;

			float3_array center = w.getCenter();
			found = false;

			point = vector_fma(facenormal, PATCH_HUNT_OFFSET, center);
			if (HuntForWorld(
					point,
					faceplaneoffset,
					faceplane,
					2,
					1.0,
					PATCH_HUNT_OFFSET
				)) {
				float const dist = distance_between_points(point, center);
				if (!found || dist < bestdist) {
					found = true;
					bestpoint = point;
					bestdist = dist;
				}
			}
			if (!found) {
				for (int i = 0; i < w.size(); i++) {
					point = vector_add(
						vector_add(w.point(i), w.point_after(i, 1)), center
					);

					point = to_float3(vector_scale(point, 1.0 / 3.0));
					point = vector_fma(
						facenormal, PATCH_HUNT_OFFSET, point
					);
					if (HuntForWorld(
							point,
							faceplaneoffset,
							faceplane,
							1,
							0.0,
							PATCH_HUNT_OFFSET
						)) {
						float const dist = distance_between_points(
							point, center
						);
						if (!found || dist < bestdist) {
							found = true;
							bestpoint = point;
							bestdist = dist;
						}
					}
				}
			}

			if (found) {
				origin = bestpoint;
			} else {
				origin = patch->origin;
			}
		}
	}
}
