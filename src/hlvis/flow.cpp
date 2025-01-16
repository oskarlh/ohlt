#include "vis.h"
#include "winding.h"

// =====================================================================================
//  AllocStackWinding
// =====================================================================================
inline static winding_t* AllocStackWinding(pstack_t* const stack) {
	for (std::size_t i = 0; i < 3; ++i) {
		if (stack->freeWindings[i]) {
			stack->freeWindings[i] = false;
			return &stack->windings[i];
		}
	}

	Error("AllocStackWinding: failed");

	return nullptr;
}

// =====================================================================================
//  FreeStackWinding
// =====================================================================================
static void
FreeStackWinding(winding_t const * const w, pstack_t* const stack) {
	if (w < stack->windings.begin() || w >= stack->windings.end()) {
		return; // not from local
	}

	std::size_t i = w - stack->windings.data();

	if (stack->freeWindings[i]) {
		Error("FreeStackWinding: allready free");
	}
	stack->freeWindings[i] = true;
}

// =====================================================================================
//  ChopWinding
// =====================================================================================
inline winding_t* ChopWinding(
	winding_t* const in, pstack_t* const stack,
	hlvis_plane_t const * const split
) {
	float dists[128];
	face_side sides[128];
	int counts[3];
	float dot;
	int i;
	float3_array mid;
	winding_t* neww;

	counts[0] = counts[1] = counts[2] = 0;

	if (in->numpoints > (sizeof(sides) / sizeof(*sides))) {
		Error("Winding with too many sides!");
	}

	// determine sides for each point
	for (i = 0; i < in->numpoints; i++) {
		dot = DotProduct(in->points[i], split->normal);
		dot -= split->dist;
		dists[i] = dot;
		if (dot > ON_EPSILON) {
			sides[i] = face_side::front;
		} else if (dot < -ON_EPSILON) {
			sides[i] = face_side::back;
		} else {
			sides[i] = face_side::on;
		}
		counts[std::size_t(sides[i])]++;
	}

	if (!counts[1]) {
		return in; // completely on front side
	}

	if (!counts[0]) {
		FreeStackWinding(in, stack);
		return nullptr;
	}

	sides[i] = sides[0];
	dists[i] = dists[0];

	neww = AllocStackWinding(stack);

	neww->numpoints = 0;

	for (i = 0; i < in->numpoints; i++) {
		float* p1 = in->points[i].data();

		if (neww->numpoints == MAX_POINTS_ON_FIXED_WINDING) {
			Warning("ChopWinding : rejected(1) due to too many points\n");
			FreeStackWinding(neww, stack);
			return in; // can't chop -- fall back to original
		}

		if (sides[i] == face_side::on) {
			VectorCopy(p1, neww->points[neww->numpoints]);
			neww->numpoints++;
			continue;
		} else if (sides[i] == face_side::front) {
			VectorCopy(p1, neww->points[neww->numpoints]);
			neww->numpoints++;
		}

		if ((sides[i + 1] == face_side::on)
			| (sides[i + 1] == sides[i]
			)) // | instead of || for branch optimization
		{
			continue;
		}

		if (neww->numpoints == MAX_POINTS_ON_FIXED_WINDING) {
			Warning("ChopWinding : rejected(2) due to too many points\n");
			FreeStackWinding(neww, stack);
			return in; // can't chop -- fall back to original
		}

		// generate a split point
		{
			unsigned tmp = i + 1;
			if (tmp >= in->numpoints) {
				tmp = 0;
			}
			float const * p2 = in->points[tmp].data();

			dot = dists[i] / (dists[i] - dists[i + 1]);

			float3_array const & normal = split->normal;
			float const dist = split->dist;
			unsigned int j;
			for (j = 0; j < 3; j++) { // avoid round off error when possible
				if (normal[j] < (1.0 - NORMAL_EPSILON)) {
					if (normal[j] > (-1.0 + NORMAL_EPSILON)) {
						mid[j] = p1[j] + dot * (p2[j] - p1[j]);
					} else {
						mid[j] = -dist;
					}
				} else {
					mid[j] = dist;
				}
			}
		}

		VectorCopy(mid, neww->points[neww->numpoints]);
		neww->numpoints++;
	}

	// free the original winding
	FreeStackWinding(in, stack);

	return neww;
}

// =====================================================================================
//  AddPlane
// =====================================================================================
inline static void
AddPlane(pstack_t* const stack, hlvis_plane_t const * const split) {
	int j;

	if (stack->clipPlaneCount) {
		for (j = 0; j < stack->clipPlaneCount; j++) {
			if (fabs((stack->clipPlane[j]).dist - split->dist)
					<= EQUAL_EPSILON
				&& vectors_almost_same(
					(stack->clipPlane[j]).normal, split->normal
				)) {
				return;
			}
		}
	}
	stack->clipPlane[stack->clipPlaneCount] = *split;
	stack->clipPlaneCount++;
}

// =====================================================================================
//  ClipToSeperators
//      Source, pass, and target are an ordering of portals.
//      Generates seperating planes canidates by taking two points from
//      source and one point from pass, and clips target by them. If the
//      target argument is NULL, then a list of clipping planes is built in
//      stack instead.
//      If target is totally clipped away, that portal can not be seen
//      through. Normal clip keeps target on the same side as pass, which is
//      correct if the order goes source, pass, target.  If the order goes
//      pass, source, target then flipclip should be set.
// =====================================================================================
inline static winding_t* ClipToSeperators(
	winding_t const * const source, winding_t const * const pass,
	winding_t* const a_target, bool const flipclip, pstack_t* const stack
) {
	int i, j, k, l;
	hlvis_plane_t plane;
	float3_array v1, v2;
	float d;
	int counts[3];
	bool fliptest;
	winding_t* target = a_target;

	unsigned int const numpoints = source->numpoints;

	// check all combinations
	for (i = 0, l = 1; i < numpoints; i++, l++) {
		if (l == numpoints) {
			l = 0;
		}

		VectorSubtract(source->points[l], source->points[i], v1);

		// fing a vertex of pass that makes a plane that puts all of the
		// vertexes of pass on the front side and all of the vertexes of
		// source on the back side
		for (j = 0; j < pass->numpoints; j++) {
			VectorSubtract(pass->points[j], source->points[i], v2);
			CrossProduct(v1, v2, plane.normal);
			if (normalize_vector(plane.normal) < ON_EPSILON) {
				continue;
			}
			plane.dist = DotProduct(pass->points[j], plane.normal);

			// find out which side of the generated seperating plane has the
			// source portal
			fliptest = false;
			for (k = 0; k < numpoints; k++) {
				if ((k == i)
					| (k == l)) // | instead of || for branch optimization
				{
					continue;
				}
				d = DotProduct(source->points[k], plane.normal)
					- plane.dist;
				if (d < -ON_EPSILON) { // source is on the negative side, so
									   // we want all
					// pass and target on the positive side
					fliptest = false;
					break;
				} else if (d > ON_EPSILON) { // source is on the positive
											 // side, so we want all
					// pass and target on the negative side
					fliptest = true;
					break;
				}
			}
			if (k == numpoints) {
				continue; // planar with source portal
			}

			// flip the normal if the source portal is backwards
			if (fliptest) {
				plane.normal = negate_vector(plane.normal);
				plane.dist = -plane.dist;
			}

			// if all of the pass portal points are now on the positive
			// side, this is the seperating plane
			counts[0] = counts[1] = counts[2] = 0;
			for (k = 0; k < pass->numpoints; k++) {
				if (k == j) {
					continue;
				}
				d = DotProduct(pass->points[k], plane.normal) - plane.dist;
				if (d < -ON_EPSILON) {
					break;
				} else if (d > ON_EPSILON) {
					counts[0]++;
				} else {
					counts[2]++;
				}
			}
			if (k != pass->numpoints) {
				continue; // points on negative side, not a seperating plane
			}

			if (!counts[0]) {
				continue; // planar with seperating plane
			}

			// flip the normal if we want the back side
			if (flipclip) {
				plane.normal = negate_vector(plane.normal);
				plane.dist = -plane.dist;
			}

			if (target != nullptr) {
				// clip target by the seperating plane
				target = ChopWinding(target, stack, &plane);
				if (!target) {
					return nullptr; // target is not visible
				}
			} else {
				AddPlane(stack, &plane);
			}

			break; /* Antony was here */
		}
	}

	return target;
}

// =====================================================================================
//  RecursiveLeafFlow
//      Flood fill through the leafs
//      If src_portal is NULL, this is the originating leaf
// =====================================================================================
inline static void RecursiveLeafFlow(
	int const leafnum, threaddata_t const * const thread,
	pstack_t const * const prevstack
) {
	pstack_t stack;
	leaf_t* leaf;

	leaf = &g_leafs[leafnum];

	{
		unsigned const offset = leafnum >> 3;
		unsigned const bit = (1 << (leafnum & 7));

		// mark the leaf as visible
		if (!(thread->leafvis[offset] & bit)) {
			thread->leafvis[offset] |= bit;
			thread->base->numcansee++;
		}
	}

	stack.head = prevstack->head;
	stack.leaf = leaf;
	stack.portal = nullptr;
	stack.clipPlaneCount = -1;
	stack.clipPlane = nullptr;

	// check all portals for flowing into other leafs
	unsigned i;
	portal_t** plist = leaf->portals;

	for (i = 0; i < leaf->numportals; i++, plist++) {
		portal_t* p = *plist;

		{
			unsigned const offset = p->leaf >> 3;
			unsigned const bit = 1 << (p->leaf & 7);

			if (!(stack.head->mightsee[offset] & bit)) {
				continue; // can't possibly see it
			}
			if (!(prevstack->mightsee[offset] & bit)) {
				continue; // can't possibly see it
			}
		}

		// if the portal can't see anything we haven't allready seen, skip
		// it
		{
			long* test;

			if (p->status == stat_done) {
				test = (long*) p->visbits;
			} else {
				test = (long*) p->mightsee;
			}

			{
				int const bitlongs = g_bitlongs;

				{
					long* prevmight = (long*) prevstack->mightsee;
					long* might = (long*) stack.mightsee;

					unsigned j;
					for (j = 0; j < bitlongs;
						 j++, test++, might++, prevmight++) {
						(*might) = (*prevmight) & (*test);
					}
				}

				{
					long* might = (long*) stack.mightsee;
					long* vis = (long*) thread->leafvis;
					unsigned j;
					for (j = 0; j < bitlongs; j++, might++, vis++) {
						if ((*might) & ~(*vis)) {
							break;
						}
					}

					if (j == g_bitlongs) { // can't see anything new
						continue;
					}
				}
			}
		}

		// get plane of portal, point normal into the neighbor leaf
		stack.portalplane = &p->plane;
		hlvis_plane_t backplane;
		backplane.normal = negate_vector(p->plane.normal);
		backplane.dist = -p->plane.dist;

		if (vectors_almost_same(
				prevstack->portalplane->normal, backplane.normal
			)) {
			continue; // can't go out a coplanar face
		}

		stack.portal = p;

		stack.freeWindings.fill(true);

		stack.pass = ChopWinding(
			p->winding, &stack, thread->pstack_head.portalplane
		);
		if (!stack.pass) {
			continue;
		}

		stack.source = ChopWinding(prevstack->source, &stack, &backplane);
		if (!stack.source) {
			continue;
		}

		if (!prevstack->pass) { // the second leaf can only be blocked if
								// coplanar
			RecursiveLeafFlow(p->leaf, thread, &stack);
			continue;
		}

		stack.pass
			= ChopWinding(stack.pass, &stack, prevstack->portalplane);
		if (!stack.pass) {
			continue;
		}

		if (stack.clipPlaneCount == -1) {
			stack.clipPlaneCount = 0;
			stack.clipPlane = (hlvis_plane_t*) alloca(
				sizeof(hlvis_plane_t) * prevstack->source->numpoints
				* prevstack->pass->numpoints
			);

			ClipToSeperators(
				prevstack->source, prevstack->pass, NULL, false, &stack
			);
			ClipToSeperators(
				prevstack->pass, prevstack->source, NULL, true, &stack
			);
		}

		if (stack.clipPlaneCount > 0) {
			unsigned j;
			for (j = 0; j < stack.clipPlaneCount && stack.pass != nullptr;
				 j++) {
				stack.pass = ChopWinding(
					stack.pass, &stack, &(stack.clipPlane[j])
				);
			}

			if (stack.pass == nullptr) {
				continue;
			}
		}

		if (g_fullvis) {
			stack.source = ClipToSeperators(
				stack.pass, prevstack->pass, stack.source, false, &stack
			);
			if (!stack.source) {
				continue;
			}

			stack.source = ClipToSeperators(
				prevstack->pass, stack.pass, stack.source, true, &stack
			);
			if (!stack.source) {
				continue;
			}
		}

		// flow through it for real
		RecursiveLeafFlow(p->leaf, thread, &stack);
	}
}

// =====================================================================================
//  PortalFlow
// =====================================================================================
void PortalFlow(portal_t* p) {
	if (p->status != stat_working) {
		Error("PortalFlow: reflowed");
	}

	p->visbits = (byte*) calloc(1, g_bitbytes);

	threaddata_t data{};
	data.leafvis = p->visbits;
	data.base = p;

	data.pstack_head.head = &data.pstack_head;
	data.pstack_head.portal = p;
	data.pstack_head.source = p->winding;
	data.pstack_head.portalplane = &p->plane;
	for (std::size_t i = 0; i < g_bitlongs; ++i) {
		((long*) data.pstack_head.mightsee)[i] = ((long*) p->mightsee)[i];
	}
	RecursiveLeafFlow(p->leaf, &data, &data.pstack_head);

	p->status = stat_done;
}

// =====================================================================================
//  SimpleFlood
//      This is a rough first-order aproximation that is used to trivially
//      reject some of the final calculations.
// =====================================================================================
static void SimpleFlood(
	byte* const srcmightsee, int const leafnum, byte* const portalsee,
	unsigned int* const c_leafsee
) {
	unsigned i;
	leaf_t* leaf;
	portal_t* p;

	{
		unsigned const offset = leafnum >> 3;
		unsigned const bit = (1 << (leafnum & 7));

		if (srcmightsee[offset] & bit) {
			return;
		} else {
			srcmightsee[offset] |= bit;
		}
	}

	(*c_leafsee)++;
	leaf = &g_leafs[leafnum];

	for (i = 0; i < leaf->numportals; i++) {
		p = leaf->portals[i];
		if (!portalsee[p - g_portals]) {
			continue;
		}
		SimpleFlood(srcmightsee, p->leaf, portalsee, c_leafsee);
	}
}

#define PORTALSEE_SIZE (MAX_PORTALS * 2)
#ifdef SYSTEM_WIN32
#pragma warning(push)
#pragma warning(disable : 4100) // unreferenced formal parameter
#endif

// =====================================================================================
//  BasePortalVis
// =====================================================================================
void BasePortalVis(int unused) {
	int i, j, k;
	portal_t* tp;
	portal_t* p;
	float d;
	winding_t* w;
	byte portalsee[PORTALSEE_SIZE];
	int const portalsize = (g_numportals * 2);

	while (1) {
		i = GetThreadWork();
		if (i == -1) {
			break;
		}
		p = g_portals + i;

		p->mightsee = (byte*) calloc(1, g_bitbytes);

		memset(portalsee, 0, portalsize);

		for (j = 0, tp = g_portals; j < portalsize; j++, tp++) {
			if (j == i) {
				continue;
			}

			w = tp->winding;
			for (k = 0; k < w->numpoints; k++) {
				d = DotProduct(w->points[k], p->plane.normal)
					- p->plane.dist;
				if (d > ON_EPSILON) {
					break;
				}
			}
			if (k == w->numpoints) {
				continue; // no points on front
			}

			w = p->winding;
			for (k = 0; k < w->numpoints; k++) {
				d = DotProduct(w->points[k], tp->plane.normal)
					- tp->plane.dist;
				if (d < -ON_EPSILON) {
					break;
				}
			}
			if (k == w->numpoints) {
				continue; // no points on front
			}

			portalsee[j] = 1;
		}

		SimpleFlood(p->mightsee, p->leaf, portalsee, &p->nummightsee);
		Verbose("portal:%4i  nummightsee:%4i \n", i, p->nummightsee);
	}
}

static bool BestNormalFromWinding(
	float3_array const * points, int numpoints, float3_array& normal_out
) {
	float3_array const *pt1, *pt2, *pt3;
	int k;
	float3_array d, normal, edge;
	float dist, maxdist;
	if (numpoints < 3) {
		return false;
	}
	pt1 = &points[0];
	maxdist = -1;
	for (k = 0; k < numpoints; k++) {
		if (&points[k] == pt1) {
			continue;
		}
		VectorSubtract(points[k], *pt1, edge);
		dist = DotProduct(edge, edge);
		if (dist > maxdist) {
			maxdist = dist;
			pt2 = &points[k];
		}
	}
	if (maxdist <= ON_EPSILON * ON_EPSILON) {
		return false;
	}
	maxdist = -1;
	VectorSubtract(*pt2, *pt1, edge);
	normalize_vector(edge);
	for (k = 0; k < numpoints; k++) {
		if (&points[k] == pt1 || &points[k] == pt2) {
			continue;
		}
		VectorSubtract(points[k], *pt1, d);
		CrossProduct(edge, d, normal);
		dist = DotProduct(normal, normal);
		if (dist > maxdist) {
			maxdist = dist;
			pt3 = &points[k];
		}
	}
	if (maxdist <= ON_EPSILON * ON_EPSILON) {
		return false;
	}
	VectorSubtract(*pt3, *pt1, d);
	CrossProduct(edge, d, normal);
	normalize_vector(normal);
	if (pt3 < pt2) {
		VectorScale(normal, -1, normal);
	}
	VectorCopy(normal, normal_out);
	return true;
}

float WindingDist(winding_t const * w[2]) {
	float minsqrdist = 99999999.0 * 99999999.0;
	float sqrdist;
	int a, b;
	// point to point
	for (a = 0; a < w[0]->numpoints; a++) {
		for (b = 0; b < w[1]->numpoints; b++) {
			float3_array v;
			VectorSubtract(w[0]->points[a], w[1]->points[b], v);
			sqrdist = DotProduct(v, v);
			if (sqrdist < minsqrdist) {
				minsqrdist = sqrdist;
			}
		}
	}
	// point to edge
	for (int side = 0; side < 2; side++) {
		for (a = 0; a < w[side]->numpoints; a++) {
			for (b = 0; b < w[!side]->numpoints; b++) {
				float3_array const & p = w[side]->points[a];
				float3_array const & p1 = w[!side]->points[b];
				float3_array const & p2
					= w[!side]->points[(b + 1) % w[!side]->numpoints];
				float3_array delta;
				float frac;
				float3_array v;
				VectorSubtract(p2, p1, delta);
				if (normalize_vector(delta) <= ON_EPSILON) {
					continue;
				}
				frac = DotProduct(p, delta) - DotProduct(p1, delta);
				if (frac <= ON_EPSILON
					|| frac
						>= (DotProduct(p2, delta) - DotProduct(p1, delta))
							- ON_EPSILON) {
					// p1 or p2 is closest to p
					continue;
				}
				VectorMA(p1, frac, delta, v);
				VectorSubtract(p, v, v);
				sqrdist = DotProduct(v, v);
				if (sqrdist < minsqrdist) {
					minsqrdist = sqrdist;
				}
			}
		}
	}
	// edge to edge
	for (a = 0; a < w[0]->numpoints; a++) {
		for (b = 0; b < w[1]->numpoints; b++) {
			float3_array const & p1 = w[0]->points[a];
			float3_array const & p2
				= w[0]->points[(a + 1) % w[0]->numpoints];
			float3_array const & p3 = w[1]->points[b];
			float3_array const & p4
				= w[1]->points[(b + 1) % w[1]->numpoints];
			float3_array delta1;
			float3_array delta2;
			float3_array normal;
			float3_array normal1;
			float3_array normal2;
			VectorSubtract(p2, p1, delta1);
			VectorSubtract(p4, p3, delta2);
			CrossProduct(delta1, delta2, normal);
			if (!normalize_vector(normal)) {
				continue;
			}
			CrossProduct(
				normal, delta1, normal1
			); // same direction as delta2
			CrossProduct(
				delta2, normal, normal2
			); // same direction as delta1
			if (normalize_vector(normal1) <= ON_EPSILON
				|| normalize_vector(normal2) <= ON_EPSILON) {
				continue;
			}
			if (DotProduct(p3, normal1)
					>= DotProduct(p1, normal1) - ON_EPSILON
				|| DotProduct(p4, normal1)
					<= DotProduct(p1, normal1) + ON_EPSILON
				|| DotProduct(p1, normal2)
					>= DotProduct(p3, normal2) - ON_EPSILON
				|| DotProduct(p2, normal2)
					<= DotProduct(p3, normal2) + ON_EPSILON) {
				// the edges are not crossing when viewed along normal
				continue;
			}
			sqrdist = DotProduct(p3, normal) - DotProduct(p1, normal);
			sqrdist = sqrdist * sqrdist;
			if (sqrdist < minsqrdist) {
				minsqrdist = sqrdist;
			}
		}
	}
	// point to face and edge to face
	for (int side = 0; side < 2; side++) {
		float3_array planenormal;
		float planedist;
		float3_array* boundnormals;
		float* bounddists;
		if (!BestNormalFromWinding(
				w[!side]->points, w[!side]->numpoints, planenormal
			)) {
			continue;
		}
		planedist = DotProduct(planenormal, w[!side]->points[0]);
		hlassume(
			boundnormals = (float3_array*)
				malloc(w[!side]->numpoints * sizeof(float3_array)),
			assume_NoMemory
		);
		hlassume(
			bounddists
			= (float*) malloc(w[!side]->numpoints * sizeof(float)),
			assume_NoMemory
		);
		// build boundaries
		for (b = 0; b < w[!side]->numpoints; b++) {
			float3_array v;
			float3_array const & p1 = w[!side]->points[b];
			float3_array const & p2
				= w[!side]->points[(b + 1) % w[!side]->numpoints];
			VectorSubtract(p2, p1, v);
			CrossProduct(v, planenormal, boundnormals[b]);
			if (!normalize_vector(boundnormals[b])) {
				bounddists[b] = 1.0;
			} else {
				bounddists[b] = DotProduct(p1, boundnormals[b]);
			}
		}
		for (a = 0; a < w[side]->numpoints; a++) {
			float3_array const & p = w[side]->points[a];
			for (b = 0; b < w[!side]->numpoints; b++) {
				if (DotProduct(p, boundnormals[b]) - bounddists[b]
					>= -ON_EPSILON) {
					break;
				}
			}
			if (b < w[!side]->numpoints) {
				continue;
			}
			sqrdist = DotProduct(p, planenormal) - planedist;
			sqrdist = sqrdist * sqrdist;
			if (sqrdist < minsqrdist) {
				minsqrdist = sqrdist;
			}
		}
		for (a = 0; a < w[side]->numpoints; a++) {
			float3_array const & p1 = w[side]->points[a];
			float3_array const & p2
				= w[side]->points[(a + 1) % w[side]->numpoints];
			float dist1 = DotProduct(p1, planenormal) - planedist;
			float dist2 = DotProduct(p2, planenormal) - planedist;
			float3_array delta;
			float frac;
			float3_array v;
			if (dist1 > ON_EPSILON && dist2 < -ON_EPSILON
				|| dist1 < -ON_EPSILON && dist2 > ON_EPSILON) {
				frac = dist1 / (dist1 - dist2);
				VectorSubtract(p2, p1, delta);
				VectorMA(p1, frac, delta, v);
				for (b = 0; b < w[!side]->numpoints; b++) {
					if (DotProduct(v, boundnormals[b]) - bounddists[b]
						>= -ON_EPSILON) {
						break;
					}
				}
				if (b < w[!side]->numpoints) {
					continue;
				}
				minsqrdist = 0;
			}
		}
		free(boundnormals);
		free(bounddists);
	}
	return sqrt(minsqrdist);
}

// AJM: MVD
// =====================================================================================
//  MaxDistVis
// =====================================================================================
void MaxDistVis(int unused) {
	int i, j, k, m;
	int a, b, c, d;
	leaf_t* l;
	leaf_t* tl;
	hlvis_plane_t* boundary = nullptr;
	float3_array delta;

	float new_dist;

	unsigned offset_l;
	unsigned bit_l;

	unsigned offset_tl;
	unsigned bit_tl;

	while (1) {
		i = GetThreadWork();
		if (i == -1) {
			break;
		}

		l = &g_leafs[i];

		for (j = i + 1, tl = g_leafs + j; j < g_portalleafs; j++, tl++) {
			offset_l = i >> 3;
			bit_l = (1 << (i & 7));

			offset_tl = j >> 3;
			bit_tl = (1 << (j & 7));

			{
				bool visible = false;
				for (k = 0; k < l->numportals; k++) {
					if (l->portals[k]->visbits[offset_tl] & bit_tl) {
						visible = true;
					}
				}
				for (m = 0; m < tl->numportals; m++) {
					if (tl->portals[m]->visbits[offset_l] & bit_l) {
						visible = true;
					}
				}
				if (!visible) {
					goto NoWork;
				}
			}

			// rough check
			{
				float3_array v;
				float dist;
				winding_t const * w;
				leaf_t const * leaf[2] = { l, tl };
				float3_array center[2];
				float radius[2];
				int count[2];
				for (int side = 0; side < 2; side++) {
					count[side] = 0;
					VectorClear(center[side]);
					for (a = 0; a < leaf[side]->numportals; a++) {
						w = leaf[side]->portals[a]->winding;
						for (b = 0; b < w->numpoints; b++) {
							VectorAdd(
								w->points[b], center[side], center[side]
							);
							count[side]++;
						}
					}
				}
				if (!count[0] && !count[1]) {
					goto Work;
				}
				for (int side = 0; side < 2; side++) {
					VectorScale(
						center[side], 1.0 / (float) count[side],
						center[side]
					);
					radius[side] = 0;
					for (a = 0; a < leaf[side]->numportals; a++) {
						w = leaf[side]->portals[a]->winding;
						for (b = 0; b < w->numpoints; b++) {
							VectorSubtract(w->points[b], center[side], v);
							dist = DotProduct(v, v);
							radius[side] = std::max(radius[side], dist);
						}
					}
					radius[side] = sqrt(radius[side]);
				}
				VectorSubtract(center[0], center[1], v);
				dist = vector_length(v);
				if (std::max(dist - radius[0] - radius[1], (float) 0)
					>= g_maxdistance - ON_EPSILON) {
					goto Work;
				}
				if (dist + radius[0] + radius[1]
					< g_maxdistance - ON_EPSILON) {
					goto NoWork;
				}
			}

			// exact check
			{
				float mindist = INFINITY;
				float dist;
				for (k = 0; k < l->numportals; k++) {
					for (m = 0; m < tl->numportals; m++) {
						winding_t const * w[2];
						w[0] = l->portals[k]->winding;
						w[1] = tl->portals[m]->winding;
						dist = WindingDist(w);
						mindist = std::min(dist, mindist);
					}
				}
				if (mindist >= g_maxdistance - ON_EPSILON) {
					goto Work;
				} else {
					goto NoWork;
				}
			}

		Work:
			ThreadLock();
			for (k = 0; k < l->numportals; k++) {
				l->portals[k]->visbits[offset_tl] &= ~bit_tl;
			}
			for (m = 0; m < tl->numportals; m++) {
				tl->portals[m]->visbits[offset_l] &= ~bit_l;
			}
			ThreadUnlock();

		NoWork:
			continue; // Hack to keep label from causing compile error
		}
	}

	// Release potential memory
	if (boundary) {
		delete[] boundary;
	}
}

#ifdef SYSTEM_WIN32
#pragma warning(pop)
#endif
