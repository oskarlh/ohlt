// Trace triangle meshes

#include "meshtrace.h"

#include "mathlib.h"

void TraceMesh ::SetupTrace(
	float3_array const & start,
	float3_array const & mins,
	float3_array const & maxs,
	float3_array const & end
) {
	m_bHitTriangle = false;

	m_vecStart = start;
	m_vecEnd = end;
	m_vecTraceDirection = vector_subtract(end, start);
	m_flTraceDistance = normalize_vector(m_vecTraceDirection);

	// build a bounding box of the entire move
	ClearBounds(m_vecAbsMins, m_vecAbsMaxs);

	m_vecStartMins = vector_add(m_vecStart, mins);
	AddPointToBounds(m_vecStartMins, m_vecAbsMins, m_vecAbsMaxs);

	m_vecStartMaxs = vector_add(m_vecStart, maxs);
	AddPointToBounds(m_vecStartMaxs, m_vecAbsMins, m_vecAbsMaxs);

	m_vecEndMins = vector_add(m_vecEnd, mins);
	AddPointToBounds(m_vecEndMins, m_vecAbsMins, m_vecAbsMaxs);

	m_vecEndMaxs = vector_add(m_vecEnd, maxs);
	AddPointToBounds(m_vecEndMaxs, m_vecAbsMins, m_vecAbsMaxs);

	// spread min\max by a pixel
	for (int i = 0; i < 3; i++) {
		m_vecAbsMins[i] -= 1.0f;
		m_vecAbsMaxs[i] += 1.0f;
	}
}

bool TraceMesh ::ClipRayToBox(
	float3_array const & mins, float3_array const & maxs
) {
	float3_array const t0 = cross_product(
		vector_subtract(mins, m_vecStart), m_vecTraceDirection
	);
	float3_array const t1 = cross_product(
		vector_subtract(maxs, m_vecStart), m_vecTraceDirection
	);

	float const d = std::max({ std::min(t0[0], t1[0]),
							   std::min(t0[1], t1[1]),
							   std::min(t0[2], t1[2]) });

	float const t = std::min({ std::max(t0[0], t1[0]),
							   std::max(t0[1], t1[1]),
							   std::max(t0[2], t1[2]) });

	return (t >= 0.0f) && (t >= d);
}

bool TraceMesh ::ClipRayToTriangle(mfacet_t const * facet) {
	float3_array w, p;

	// we have two edge directions, we can calculate the normal
	float3_array n = cross_product(facet->edge2, facet->edge1);

	if (vectors_almost_same(n, float3_array{})) {
		return false; // degenerate triangle
	}

	p = vector_subtract(m_vecEnd, m_vecStart);
	w = vector_subtract(m_vecStart, facet->triangle[0].point);

	float const d1 = -dot_product(n, w);
	float const d2 = dot_product(n, p);
	if (fabs(d2) < FRAC_EPSILON) {
		return false; // parallel with plane
	}

	// get intersect point of ray with triangle plane
	float const frac = d1 / d2;

	if (frac < 0.0f) {
		return false;
	}

	// calculate the impact point
	p[0] = m_vecStart[0] + (m_vecEnd[0] - m_vecStart[0]) * frac;
	p[1] = m_vecStart[1] + (m_vecEnd[1] - m_vecStart[1]) * frac;
	p[2] = m_vecStart[2] + (m_vecEnd[2] - m_vecStart[2]) * frac;

	// does p lie inside triangle?
	float const uu = dot_product(facet->edge1, facet->edge1);
	float const uv = dot_product(facet->edge1, facet->edge2);
	float const vv = dot_product(facet->edge2, facet->edge2);

	w = vector_subtract(p, facet->triangle[0].point);
	float const wu = dot_product(w, facet->edge1);
	float const wv = dot_product(w, facet->edge2);
	float const d = uv * uv - uu * vv;

	// get and test parametric coords
	float const s = (uv * wv - vv * wu) / d;
	if (s < 0.0f || s > 1.0f) {
		return false; // p is outside
	}

	float const t = (uv * wu - uu * wv) / d;
	if (t < 0.0 || (s + t) > 1.0) {
		return false; // p is outside
	}

	return true;
}

bool TraceMesh ::ClipRayToFace(mfacet_t const * facet) {
	// begin calculating determinant - also used to calculate u parameter
	float3_array pvec = cross_product(m_vecTraceDirection, facet->edge2);

	// if determinant is near zero, trace lies in plane of triangle
	float det = dot_product(facet->edge1, pvec);

	// the non-culling branch
	if (fabs(det) < COPLANAR_EPSILON) {
		return false;
	}

	float invDet = 1.0f / det;

	// calculate distance from first vertex to ray origin
	float3_array tvec = vector_subtract(
		m_vecStart, facet->triangle[0].point
	);

	// calculate u parameter and test bounds
	float u = dot_product(tvec, pvec) * invDet;
	if (u < -BARY_EPSILON || u > (1.0f + BARY_EPSILON)) {
		return false;
	}

	// prepare to test v parameter
	float3_array qvec = cross_product(tvec, facet->edge1);

	// calculate v parameter and test bounds
	float v = dot_product(m_vecTraceDirection, qvec) * invDet;
	if (v < -BARY_EPSILON || (u + v) > (1.0f + BARY_EPSILON)) {
		return false;
	}

	// calculate t (depth)
	float depth = dot_product(facet->edge2, qvec) * invDet;
	if (depth <= NEAR_SHADOW_EPSILON || depth >= m_flTraceDistance) {
		return false;
	}

	// most surfaces are completely opaque
	if (!facet->texture || !facet->texture->index) {
		return true;
	}

	if (facet->texture->flags & STUDIO_NF_ADDITIVE) {
		return false; // translucent
	}

	if (!(facet->texture->flags & STUDIO_NF_TRANSPARENT)) {
		return true;
	}

	// try to avoid double shadows near triangle seams
	if (u < -ASLF_EPSILON || u > (1.0f + ASLF_EPSILON) || v < -ASLF_EPSILON
		|| (u + v) > (1.0f + ASLF_EPSILON)) {
		return false;
	}

	// calculate w parameter
	float w = 1.0f - (u + v);

	// calculate st from uvw (barycentric) coordinates
	float s = w * facet->triangle[0].st[0] + u * facet->triangle[1].st[0]
		+ v * facet->triangle[2].st[0];
	float t = w * facet->triangle[0].st[1] + u * facet->triangle[1].st[1]
		+ v * facet->triangle[2].st[1];
	s = s - floor(s);
	t = t - floor(t);

	int is = s * facet->texture->width;
	int it = t * facet->texture->height;

	if (is < 0) {
		is = 0;
	}
	if (it < 0) {
		it = 0;
	}

	if (is > facet->texture->width - 1) {
		is = facet->texture->width - 1;
	}
	if (it > facet->texture->height - 1) {
		it = facet->texture->height - 1;
	}

	byte* pixels = (byte*) m_extradata + facet->texture->index;

	// test pixel
	if (pixels[it * facet->texture->width + is] == 0xFF) {
		return false; // last color in palette is indicated alpha-pixel
	}

	return true;
}

bool TraceMesh ::ClipRayToFacet(mfacet_t const * facet) {
	mplane_t *p, *clipplane;
	float enterfrac, leavefrac, distfrac;
	bool startout;
	float d, d1, d2, f;

	if (!facet->numplanes) {
		return false;
	}

	enterfrac = -1.0f;
	leavefrac = 1.0f;
	clipplane = nullptr;
	checkcount++;

	startout = false;

	for (int i = 0; i < facet->numplanes; i++) {
		p = &mesh->planes[facet->indices[i]];

		// push the plane out apropriately for mins/maxs
		if (p->type <= planetype::plane_z) {
			d1 = m_vecStartMins[std::to_underlying(p->type)] - p->dist;
			d2 = m_vecEndMins[std::to_underlying(p->type)] - p->dist;
		} else {
			switch (p->signbits) {
				case 0:
					d1 = p->normal[0] * m_vecStartMins[0]
						+ p->normal[1] * m_vecStartMins[1]
						+ p->normal[2] * m_vecStartMins[2] - p->dist;
					d2 = p->normal[0] * m_vecEndMins[0]
						+ p->normal[1] * m_vecEndMins[1]
						+ p->normal[2] * m_vecEndMins[2] - p->dist;
					break;
				case 1:
					d1 = p->normal[0] * m_vecStartMaxs[0]
						+ p->normal[1] * m_vecStartMins[1]
						+ p->normal[2] * m_vecStartMins[2] - p->dist;
					d2 = p->normal[0] * m_vecEndMaxs[0]
						+ p->normal[1] * m_vecEndMins[1]
						+ p->normal[2] * m_vecEndMins[2] - p->dist;
					break;
				case 2:
					d1 = p->normal[0] * m_vecStartMins[0]
						+ p->normal[1] * m_vecStartMaxs[1]
						+ p->normal[2] * m_vecStartMins[2] - p->dist;
					d2 = p->normal[0] * m_vecEndMins[0]
						+ p->normal[1] * m_vecEndMaxs[1]
						+ p->normal[2] * m_vecEndMins[2] - p->dist;
					break;
				case 3:
					d1 = p->normal[0] * m_vecStartMaxs[0]
						+ p->normal[1] * m_vecStartMaxs[1]
						+ p->normal[2] * m_vecStartMins[2] - p->dist;
					d2 = p->normal[0] * m_vecEndMaxs[0]
						+ p->normal[1] * m_vecEndMaxs[1]
						+ p->normal[2] * m_vecEndMins[2] - p->dist;
					break;
				case 4:
					d1 = p->normal[0] * m_vecStartMins[0]
						+ p->normal[1] * m_vecStartMins[1]
						+ p->normal[2] * m_vecStartMaxs[2] - p->dist;
					d2 = p->normal[0] * m_vecEndMins[0]
						+ p->normal[1] * m_vecEndMins[1]
						+ p->normal[2] * m_vecEndMaxs[2] - p->dist;
					break;
				case 5:
					d1 = p->normal[0] * m_vecStartMaxs[0]
						+ p->normal[1] * m_vecStartMins[1]
						+ p->normal[2] * m_vecStartMaxs[2] - p->dist;
					d2 = p->normal[0] * m_vecEndMaxs[0]
						+ p->normal[1] * m_vecEndMins[1]
						+ p->normal[2] * m_vecEndMaxs[2] - p->dist;
					break;
				case 6:
					d1 = p->normal[0] * m_vecStartMins[0]
						+ p->normal[1] * m_vecStartMaxs[1]
						+ p->normal[2] * m_vecStartMaxs[2] - p->dist;
					d2 = p->normal[0] * m_vecEndMins[0]
						+ p->normal[1] * m_vecEndMaxs[1]
						+ p->normal[2] * m_vecEndMaxs[2] - p->dist;
					break;
				case 7:
					d1 = p->normal[0] * m_vecStartMaxs[0]
						+ p->normal[1] * m_vecStartMaxs[1]
						+ p->normal[2] * m_vecStartMaxs[2] - p->dist;
					d2 = p->normal[0] * m_vecEndMaxs[0]
						+ p->normal[1] * m_vecEndMaxs[1]
						+ p->normal[2] * m_vecEndMaxs[2] - p->dist;
					break;
				default:
					d1 = d2 = 0.0f; // shut up compiler
					break;
			}
		}

		if (d1 > 0.0f) {
			startout = true;
		}

		// if completely in front of face, no intersection
		if (d1 > 0 && d2 >= d1) {
			return false;
		}

		if (d1 <= 0 && d2 <= 0) {
			continue;
		}

		// crosses face
		d = 1 / (d1 - d2);
		f = d1 * d;

		if (d > 0.0f) {
			// enter
			if (f > enterfrac) {
				distfrac = d;
				enterfrac = f;
				clipplane = p;
			}
		} else if (d < 0.0f) {
			// leave
			if (f < leavefrac) {
				leavefrac = f;
			}
		}
	}

	if (!startout) {
		// original point was inside brush
		return true;
	}

	if (((enterfrac - FRAC_EPSILON) <= leavefrac) && (enterfrac > -1.0f)
		&& (enterfrac < 1.0f)) {
		return true;
	}

	return false;
}

void TraceMesh ::ClipToLinks(areanode_t* node) {
	link_t *l, *next;
	mfacet_t* facet;

	// touch linked edicts
	for (l = node->facets.next; l != &node->facets; l = next) {
		next = l->next;

		facet = FACET_FROM_AREA(l);

		if (!BoundsIntersect(
				m_vecAbsMins, m_vecAbsMaxs, facet->mins, facet->maxs
			)) {
			continue;
		}

		if (mesh->trace_mode == trace_method::shadow_fast) {
			// ultra-fast mode, no real tracing here
			if (ClipRayToBox(facet->mins, facet->maxs)) {
				m_bHitTriangle = true;
				return;
			}
		} else if (mesh->trace_mode == trace_method::shadow_normal) {
			// does trace for each triangle
			if (ClipRayToFace(facet)) {
				m_bHitTriangle = true;
				return;
			}
		} else if (mesh->trace_mode == trace_method::shadow_slow) {
			// does trace for planes bbox for each triangle
			if (ClipRayToFacet(facet)) {
				m_bHitTriangle = true;
				return;
			}
		} else {
			// unknown mode
			return;
		}
	}

	// recurse down both sides
	if (node->axis == -1) {
		return;
	}

	if (m_vecAbsMaxs[node->axis] > node->dist) {
		ClipToLinks(node->children[0]);
	}
	if (m_vecAbsMins[node->axis] < node->dist) {
		ClipToLinks(node->children[1]);
	}
}

bool TraceMesh ::DoTrace(void) {
	if (!mesh
		|| !BoundsIntersect(
			mesh->mins, mesh->maxs, m_vecAbsMins, m_vecAbsMaxs
		)) {
		return false; // invalid mesh or no intersection
	}

	checkcount = 0;

	if (areanodes) {
		ClipToLinks(areanodes);
	} else {
		mfacet_t* facet = mesh->facets;
		for (int i = 0; i < mesh->numfacets; i++, facet++) {
			if (!BoundsIntersect(
					m_vecAbsMins, m_vecAbsMaxs, facet->mins, facet->maxs
				)) {
				continue;
			}

			if (mesh->trace_mode == trace_method::shadow_fast) {
				// ultra-fast mode, no real tracing here
				if (ClipRayToBox(facet->mins, facet->maxs)) {
					return true;
				}
			} else if (mesh->trace_mode == trace_method::shadow_normal) {
				// does trace for each triangle
				if (ClipRayToFace(facet)) {
					return true;
				}
			} else if (mesh->trace_mode == trace_method::shadow_slow) {
				// does trace for planes bbox for each triangle
				if (ClipRayToFacet(facet)) {
					return true;
				}
			} else {
				// unknown mode
				return false;
			}
		}
	}

	//	Developer( developer_level::message, "total %i checks for %s\n",
	// checkcount, areanodes ? "tree" : "brute force" );

	return m_bHitTriangle;
}
