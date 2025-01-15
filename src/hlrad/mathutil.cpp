#include "qrad.h"
#include <numbers>

// =====================================================================================
//  point_in_winding
//      returns whether the point is in the winding (including its edges)
//      the point and all the vertexes of the winding can move freely along the plane's normal without changing the result
// =====================================================================================
bool point_in_winding(const Winding& w, const dplane_t& plane, const float3_array& point, float epsilon/* = 0.0*/)
{

	const int numpoints = w.size();

	for (int x = 0; x < numpoints; x++)
	{

		float3_array delta;
		VectorSubtract(w.m_Points[(x+ 1) % numpoints], w.m_Points[x], delta);
		float3_array normal;
		CrossProduct (delta, plane.normal, normal);
		const float dist = DotProduct (point, normal) - DotProduct (w.m_Points[x], normal);

		if (dist < 0.0
			&& (epsilon == 0.0 || dist * dist > epsilon * epsilon * DotProduct (normal, normal)))
		{
			return false;
		}
	}

	return true;
}

// =====================================================================================
//  point_in_winding_noedge
//      assume a ball is created from the point, this function checks whether the ball is entirely inside the winding
//      parameter 'width' : the radius of the ball
//      the point and all the vertexes of the winding can move freely along the plane's normal without changing the result
// =====================================================================================
bool point_in_winding_noedge(const Winding& w, const dplane_t& plane, const float3_array& point, float width)
{
	int				numpoints;
	int				x;
	float3_array delta;
	float3_array normal;
	float			dist;

	numpoints = w.size();

	for (x = 0; x < numpoints; x++)
	{
		VectorSubtract(w.m_Points[(x+ 1) % numpoints], w.m_Points[x], delta);
		CrossProduct (delta, plane.normal, normal);
		dist = DotProduct (point, normal) - DotProduct (w.m_Points[x], normal);

		if (dist < 0.0 || dist * dist <= width * width * DotProduct (normal, normal))
		{
			return false;
		}
	}

	return true;
}

// =====================================================================================
//  snap_to_winding
//      moves the point to the nearest point inside the winding
//      if the point is not on the plane, the distance between the point and the plane is preserved
//      the point and all the vertexes of the winding can move freely along the plane's normal without changing the result
// =====================================================================================
void			snap_to_winding(const Winding& w, const dplane_t& plane, float* const point)
{
	int				numpoints;
	int				x;
	const float *p1, *p2;
	float3_array			delta;
	float3_array			normal;
	float			dist;
	float			dot1, dot2, dot;
	float3_array			bestpoint;
	float			bestdist;
	bool			in;

	numpoints = w.size();

	in = true;
	for (x = 0; x < numpoints; x++)
	{
		p1 = w.m_Points[x].data();
		p2 = w.m_Points[(x + 1) % numpoints].data();
		VectorSubtract(p2, p1, delta);
		CrossProduct (delta, plane.normal, normal);
		dist = DotProduct (point, normal) - DotProduct (p1, normal);

		if (dist < 0.0)
		{
			in = false;

			CrossProduct (plane.normal, normal, delta);
			dot = DotProduct (delta, point);
			dot1 = DotProduct (delta, p1);
			dot2 = DotProduct (delta, p2);
			if (dot1 < dot && dot < dot2)
			{
				dist = dist / DotProduct (normal, normal);
				VectorMA (point, -dist, normal, point);
				return;
			}
		}
	}
	if (in)
	{
		return;
	}

	for (x = 0; x < numpoints; x++)
	{
		p1 = w.m_Points[x].data();
		VectorSubtract(p1, point, delta);
		dist = DotProduct (delta, plane.normal) / DotProduct (plane.normal, plane.normal);
		VectorMA (delta, -dist, plane.normal, delta);
		dot = DotProduct (delta, delta);

		if (x == 0 || dot < bestdist)
		{
			VectorAdd (point, delta, bestpoint);
			bestdist = dot;
		}
	}
	if (numpoints > 0)
	{
		VectorCopy (bestpoint, point);
	}
	return;
}

// =====================================================================================
//  snap_to_winding_noedge
//      first snaps the point into the winding
//      then moves the point towards the inside for at most certain distance until:
//        either 1) the point is not close to any of the edges
//        or     2) the point can not be moved any more
//      returns the maximal distance that the point can be kept away from all the edges
//      in most of the cases, the maximal distance = width; in other cases, the maximal distance < width
// =====================================================================================
float snap_to_winding_noedge(const Winding& w, const dplane_t& plane, float* const point, float width, float maxmove)
{
	int pass;
	int numplanes;
	dplane_t *planes;
	int x;
	float3_array v;
	float newwidth;
	float bestwidth;
	float3_array bestpoint;

	snap_to_winding (w, plane, point);

	planes = (dplane_t *)malloc (w.size() * sizeof (dplane_t));
	hlassume (planes != nullptr, assume_NoMemory);
	numplanes = 0;
	for (x = 0; x < w.size(); x++)
	{
		VectorSubtract(w.m_Points[(x + 1) % w.size()], w.m_Points[x], v);
		CrossProduct (v, plane.normal, planes[numplanes].normal);
		if (!normalize_vector(planes[numplanes].normal))
		{
			continue;
		}
		planes[numplanes].dist = DotProduct (w.m_Points[x], planes[numplanes].normal);
		numplanes++;
	}
	
	bestwidth = 0;
	VectorCopy (point, bestpoint);
	newwidth = width;

	for (pass = 0; pass < 5; pass++) // apply binary search method for 5 iterations to find the maximal distance that the point can be kept away from all the edges
	{
		bool failed;
		float3_array newpoint;
		Winding *newwinding;

		failed = true;

		newwinding = new Winding (w);
		for (x = 0; x < numplanes && newwinding->size() > 0; x++)
		{
			dplane_t clipplane = planes[x];
			clipplane.dist += newwidth;
			newwinding->mutating_clip(clipplane.normal, clipplane.dist, false);
		}

		if (newwinding->size() > 0)
		{
			VectorCopy (point, newpoint);
			snap_to_winding (*newwinding, plane, newpoint.data());

			VectorSubtract(newpoint, point, v);
			if (vector_length(v) <= maxmove + ON_EPSILON)
			{
				failed = false;
			}
		}

		delete newwinding;

		if (!failed)
		{
			bestwidth = newwidth;
			VectorCopy (newpoint, bestpoint);
			if (pass == 0)
			{
				break;
			}
			newwidth += width * pow (0.5, pass + 1);
		}
		else
		{
			newwidth -= width * pow (0.5, pass + 1);
		}
	}

	free (planes);

	VectorCopy (bestpoint, point);
	return bestwidth;
}


bool intersect_linesegment_plane(const dplane_t& plane, const float3_array& p1, const float3_array& p2, float3_array& point) {
	const float part1 = DotProduct (p1, plane.normal) - plane.dist;
	const float part2 = DotProduct (p2, plane.normal) - plane.dist;
	if (part1 * part2 > 0 || part1 == part2) {
		return false;
	}
	for (std::size_t i = 0; i < 3; ++i) {
		point[i] = (part1 * p2[i] - part2 * p1[i]) / (part1 - part2);
	}
	return true;
}

void plane_from_points(const float3_array& p1, const float3_array& p2, const float3_array& p3, dplane_t& plane) {
    float3_array delta1;
    float3_array delta2;
    float3_array normal;

    VectorSubtract(p3, p2, delta1);
    VectorSubtract(p1, p2, delta2);
    CrossProduct(delta1, delta2, normal);
    normalize_vector(normal);
    plane.dist = DotProduct(normal, p1);
	plane.normal = normal;
}

//LineSegmentIntersectsBounds --vluzacn
bool LineSegmentIntersectsBounds_r (const float* p1, const float* p2, const float* mins, const float* maxs, int d)
{
	float lmin, lmax;
	const float* tmp;
	int i;
	d--;
	if (p2[d]<p1[d])
		tmp=p1, p1=p2, p2=tmp;
	if (p2[d]<mins[d] || p1[d]>maxs[d])
		return false;
	if (d==0)
		return true;
	lmin = p1[d]>=mins[d]? 0 : (mins[d]-p1[d])/(p2[d]-p1[d]);
	lmax = p2[d]<=maxs[d]? 1 : (p2[d]-maxs[d])/(p2[d]-p1[d]);
	float3_array x1, x2;
	for (i=0; i<d; ++i)
	{
		x1[i]=(1-lmin)*p1[i]+lmin*p2[i];
		x2[i]=(1-lmax)*p2[i]+lmax*p2[i];
	}
	return LineSegmentIntersectsBounds_r (x1.data(), x2.data(), mins, maxs, d);
}
inline bool LineSegmentIntersectsBounds (const float3_array& p1, const float3_array& p2, const float3_array& mins, const float3_array& maxs)
{
	return LineSegmentIntersectsBounds_r (p1.data(), p2.data(), mins.data(), maxs.data(), 3);
}

// =====================================================================================
//  TestSegmentAgainstOpaqueList
//      Returns true if the segment intersects an item in the opaque list
// =====================================================================================
bool            TestSegmentAgainstOpaqueList(const float3_array& p1, const float3_array& p2
					, float3_array& scaleout
					, int &opaquestyleout // light must convert to this style. -1 = no convert
					)
	{
		int x;
		VectorFill (scaleout, 1.0);
		opaquestyleout = -1;
	    for (x = 0; x < g_opaque_face_list.size(); x++)
		{
			if (!TestLineOpaque (g_opaque_face_list[x].modelnum, g_opaque_face_list[x].origin, p1, p2))
			{
				continue;
			}
			if (g_opaque_face_list[x].transparency)
			{
				VectorMultiply (scaleout, g_opaque_face_list[x].transparency_scale, scaleout);
				continue;
			}
			if (g_opaque_face_list[x].style != -1 && (opaquestyleout == -1 || g_opaque_face_list[x].style == opaquestyleout))
			{
				opaquestyleout = g_opaque_face_list[x].style;
				continue;
			}
			VectorFill (scaleout, 0.0);
			opaquestyleout = -1;
			return true;
		}
		if (TestSegmentAgainstStudioList(p1, p2)) //seedee
		{
			VectorFill(scaleout, 0.0);
			opaquestyleout = -1;
			return true;
		}
		if (scaleout[0] < 0.01 && scaleout[1] < 0.01 && scaleout[2] < 0.01)
		{
			return true; //so much shadowing that result is same as with normal opaque face
		}
		return false;
}


// =====================================================================================
//  SnapToPlane
// =====================================================================================
void            SnapToPlane(const dplane_t* const plane, float* const point, float offset)
{
	float			dist;
	dist = DotProduct (point, plane->normal) - plane->dist;
	dist -= offset;
	VectorMA (point, -dist, plane->normal, point);
}
// =====================================================================================
//  CalcSightArea
// =====================================================================================
float CalcSightArea (const float3_array& receiver_origin, const float3_array& receiver_normal, const Winding *emitter_winding, int skylevel
					, float lighting_power, float lighting_scale
					)
{
	// maybe there are faster ways in calculating the weighted area, but at least this way is not bad.
	float area = 0.0;

	int numedges = emitter_winding->size();
	float3_array *edges = (float3_array *)malloc (numedges * sizeof (float3_array));
	hlassume (edges != nullptr, assume_NoMemory);
	bool error = false;
	for (int x = 0; x < numedges; x++)
	{
		float3_array v1, v2, normal;
		VectorSubtract(emitter_winding->m_Points[x], receiver_origin, v1);
		VectorSubtract(emitter_winding->m_Points[(x + 1) % numedges], receiver_origin, v2);
		CrossProduct (v1, v2, normal); // pointing inward
		if (!normalize_vector(normal))
		{
			error = true;
		}
		VectorCopy (normal, edges[x]);
	}
	if (!error)
	{
		int i, j;
		float3_array *pnormal;
		float *psize;
		float dot;
		float3_array *pedge;
		for (i = 0, pnormal = g_skynormals[skylevel], psize = g_skynormalsizes[skylevel]; i < g_numskynormals[skylevel]; i++, pnormal++, psize++)
		{
			dot = DotProduct (*pnormal, receiver_normal);
			if (dot <= 0)
				continue;
			for (j = 0, pedge = edges; j < numedges; j++, pedge++)
			{
				if (DotProduct (*pnormal, *pedge) <= 0)
				{
					break;
				}
			}
			if (j < numedges)
			{
				continue;
			}
			if (lighting_power != 1.0)
			{
				dot = pow (dot, lighting_power);
			}
			area += dot * (*psize);
		}
		area = area * 4 * std::numbers::pi_v<double>; // convert to absolute sphere area
	}
	free (edges);
	area *= lighting_scale;
	return area;
}

float CalcSightArea_SpotLight (const float3_array& receiver_origin, const float3_array& receiver_normal, const Winding *emitter_winding, const float3_array& emitter_normal, float emitter_stopdot, float emitter_stopdot2, int skylevel
					, float lighting_power, float lighting_scale
					)
{
	// stopdot = cos (cone)
	// stopdot2 = cos (cone2)
	// stopdot >= stopdot2 >= 0
	// ratio = 1.0 , when dot2 >= stopdot
	// ratio = (dot - stopdot2) / (stopdot - stopdot2) , when stopdot > dot2 > stopdot2
	// ratio = 0.0 , when stopdot2 >= dot2
	float area = 0.0;

	int numedges = emitter_winding->size();
	float3_array *edges = (float3_array *)malloc (numedges * sizeof (float3_array));
	hlassume (edges != nullptr, assume_NoMemory);
	bool error = false;
	for (int x = 0; x < numedges; x++)
	{
		float3_array v1, v2, normal;
		VectorSubtract(emitter_winding->m_Points[x], receiver_origin, v1);
		VectorSubtract(emitter_winding->m_Points[(x + 1) % numedges], receiver_origin, v2);
		CrossProduct (v1, v2, normal); // pointing inward
		if (!normalize_vector(normal))
		{
			error = true;
		}
		VectorCopy (normal, edges[x]);
	}
	if (!error)
	{
		int i, j;
		float3_array *pnormal;
		float *psize;
		float dot;
		float dot2;
		float3_array *pedge;
		for (i = 0, pnormal = g_skynormals[skylevel], psize = g_skynormalsizes[skylevel]; i < g_numskynormals[skylevel]; i++, pnormal++, psize++)
		{
			dot = DotProduct (*pnormal, receiver_normal);
			if (dot <= 0)
				continue;
			for (j = 0, pedge = edges; j < numedges; j++, pedge++)
			{
				if (DotProduct (*pnormal, *pedge) <= 0)
				{
					break;
				}
			}
			if (j < numedges)
			{
				continue;
			}
			if (lighting_power != 1.0)
			{
				dot = pow (dot, lighting_power);
			}
			dot2 = -DotProduct (*pnormal, emitter_normal);
			if (dot2 <= emitter_stopdot2 + NORMAL_EPSILON)
			{
				dot = 0;
			}
			else if (dot2 < emitter_stopdot)
			{
				dot = dot * (dot2 - emitter_stopdot2) / (emitter_stopdot - emitter_stopdot2);
			}
			area += dot * (*psize);
		}
		area = area * 4 * std::numbers::pi_v<double>; // convert to absolute sphere area
	}
	free (edges);
	area *= lighting_scale;
	return area;
}

// =====================================================================================
//  GetAlternateOrigin
// =====================================================================================
void GetAlternateOrigin (const float3_array& pos, const float3_array& normal, const patch_t* patch, float3_array& origin)
{
	const dplane_t *faceplane;
	const float *facenormal;
	dplane_t clipplane;
	Winding w;

	faceplane = getPlaneFromFaceNumber (patch->faceNumber);
	const float3_array& faceplaneoffset = g_face_offset[patch->faceNumber];
	facenormal = faceplane->normal.data();
	VectorCopy (normal, clipplane.normal);
	clipplane.dist = DotProduct (pos, clipplane.normal);
	
	w = *patch->winding;
	if (w.WindingOnPlaneSide (clipplane.normal, clipplane.dist) != face_side::cross)
	{
		VectorCopy (patch->origin, origin);
	}
	else
	{
		w.mutating_clip(clipplane.normal, clipplane.dist, false);
		if (w.size() == 0)
		{
			VectorCopy (patch->origin, origin);
		}
		else
		{
			bool found;
			float3_array bestpoint;
			float bestdist = -1.0;
			float3_array point;
			float dist;
			float3_array v;

			float3_array center = w.getCenter();
			found = false;

			VectorMA (center, PATCH_HUNT_OFFSET, facenormal, point);
			if (HuntForWorld (point, faceplaneoffset, faceplane, 2, 1.0, PATCH_HUNT_OFFSET))
			{
				VectorSubtract(point, center, v);
				dist = vector_length(v);
				if (!found || dist < bestdist)
				{
					found = true;
					VectorCopy (point, bestpoint);
					bestdist = dist;
				}
			}
			if (!found)
			{
				for (int i = 0; i < w.size(); i++)
				{
					const float *p1;
					const float *p2;
					p1 = w.m_Points[i].data();
					p2 = w.m_Points[(i + 1) % w.size()].data();
					VectorAdd (p1, p2, point);
					VectorAdd (point, center, point);
					VectorScale (point, 1.0/3.0, point);
					VectorMA (point, PATCH_HUNT_OFFSET, facenormal, point);
					if (HuntForWorld (point, faceplaneoffset, faceplane, 1, 0.0, PATCH_HUNT_OFFSET))
					{
						VectorSubtract(point, center, v);
						dist = vector_length(v);
						if (!found || dist < bestdist)
						{
							found = true;
							VectorCopy (point, bestpoint);
							bestdist = dist;
						}
					}
				}
			}

			if (found)
			{
				VectorCopy (bestpoint, origin);
			}
			else
			{
				VectorCopy (patch->origin, origin);
			}
		}
	}
}

