#include "hlvis.h"
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
	winding_t* const in,
	pstack_t* const stack,
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
		dot = dot_product(in->points[i], split->normal);
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
		float3_array& p1 = in->points[i];

		if (neww->numpoints == MAX_POINTS_ON_FIXED_WINDING) {
			Warning("ChopWinding : rejected(1) due to too many points\n");
			FreeStackWinding(neww, stack);
			return in; // can't chop -- fall back to original
		}

		if (sides[i] == face_side::on) {
			neww->points[neww->numpoints] = p1;
			neww->numpoints++;
			continue;
		} else if (sides[i] == face_side::front) {
			neww->points[neww->numpoints] = p1;
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
			float3_array const & p2 = in->points[tmp];

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

		neww->points[neww->numpoints] = mid;
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
	winding_t const * const source,
	winding_t const * const pass,
	winding_t* const a_target,
	bool const flipclip,
	pstack_t* const stack
) {
	hlvis_plane_t plane;
	float3_array v1, v2;
	int counts[3];
	bool fliptest;
	winding_t* target = a_target;

	unsigned int const numpoints = source->numpoints;

	// check all combinations
	for (std::size_t i = 0, l = 1; i < numpoints; ++i, ++l) {
		if (l == numpoints) {
			l = 0;
		}

		v1 = vector_subtract(source->points[l], source->points[i]);

		// fing a vertex of pass that makes a plane that puts all of the
		// vertexes of pass on the front side and all of the vertexes of
		// source on the back side
		for (std::size_t j = 0; j < pass->numpoints; ++j) {
			v2 = vector_subtract(pass->points[j], source->points[i]);
			plane.normal = cross_product(v1, v2);
			if (normalize_vector(plane.normal) < ON_EPSILON) {
				continue;
			}
			plane.dist = dot_product(pass->points[j], plane.normal);

			// find out which side of the generated seperating plane has the
			// source portal
			fliptest = false;
			std::size_t k;
			for (k = 0; k < numpoints; ++k) {
				if ((k == i)
					| (k == l)) // | instead of || for branch optimization
				{
					continue;
				}
				float const d = dot_product(source->points[k], plane.normal)
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
				float const d = dot_product(pass->points[k], plane.normal)
					- plane.dist;
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
	int const leafnum,
	threaddata_t const * const thread,
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

		stack.pass = ChopWinding(
			stack.pass, &stack, prevstack->portalplane
		);
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
	byte* const srcmightsee,
	int const leafnum,
	byte* const portalsee,
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

constexpr std::size_t PORTALSEE_SIZE = MAX_PORTALS * 2;

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

		std::fill_n(portalsee, portalsize, 0);

		for (j = 0, tp = g_portals; j < portalsize; j++, tp++) {
			if (j == i) {
				continue;
			}

			w = tp->winding;
			for (k = 0; k < w->numpoints; k++) {
				d = dot_product(w->points[k], p->plane.normal)
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
				d = dot_product(w->points[k], tp->plane.normal)
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
	float3_array d, normal, edge;
	float dist, maxdist;
	if (numpoints < 3) {
		return false;
	}
	pt1 = &points[0];
	maxdist = -1;
	for (int k = 0; k < numpoints; k++) {
		if (&points[k] == pt1) {
			continue;
		}
		edge = vector_subtract(points[k], *pt1);
		dist = dot_product(edge, edge);
		if (dist > maxdist) {
			maxdist = dist;
			pt2 = &points[k];
		}
	}
	if (maxdist <= ON_EPSILON * ON_EPSILON) {
		return false;
	}
	maxdist = -1;
	edge = vector_subtract(*pt2, *pt1);
	normalize_vector(edge);
	for (int k = 0; k < numpoints; k++) {
		if (&points[k] == pt1 || &points[k] == pt2) {
			continue;
		}
		d = vector_subtract(points[k], *pt1);
		normal = cross_product(edge, d);
		dist = dot_product(normal, normal);
		if (dist > maxdist) {
			maxdist = dist;
			pt3 = &points[k];
		}
	}
	if (maxdist <= ON_EPSILON * ON_EPSILON) {
		return false;
	}
	d = vector_subtract(*pt3, *pt1);
	normal = cross_product(edge, d);
	normalize_vector(normal);
	if (pt3 < pt2) {
		normal = vector_scale(normal, -1.0f);
	}
	normal_out = normal;
	return true;
}

float WindingDist(winding_t const * w[2]) {
	float minsqrdist = 99999999.0 * 99999999.0;
	int a, b;
	// point to point
	for (a = 0; a < w[0]->numpoints; a++) {
		for (b = 0; b < w[1]->numpoints; b++) {
			float3_array v = vector_subtract(
				w[0]->points[a], w[1]->points[b]
			);
			float sqrdist = dot_product(v, v);
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
				float frac;
				float3_array delta = vector_subtract(p2, p1);
				if (normalize_vector(delta) <= ON_EPSILON) {
					continue;
				}
				frac = dot_product(p, delta) - dot_product(p1, delta);
				if (frac <= ON_EPSILON
					|| frac
						>= (dot_product(p2, delta) - dot_product(p1, delta))
							- ON_EPSILON) {
					// p1 or p2 is closest to p
					continue;
				}
				float3_array v = vector_fma(delta, frac, p1);
				v = vector_subtract(p, v);
				float sqrdist = dot_product(v, v);
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
			delta1 = vector_subtract(p2, p1);
			delta2 = vector_subtract(p4, p3);
			normal = cross_product(delta1, delta2);
			if (!normalize_vector(normal)) {
				continue;
			}
			normal1 = cross_product(
				normal, delta1
			); // same direction as delta2
			normal2 = cross_product(
				delta2, normal
			); // same direction as delta1
			if (normalize_vector(normal1) <= ON_EPSILON
				|| normalize_vector(normal2) <= ON_EPSILON) {
				continue;
			}
			if (dot_product(p3, normal1)
					>= dot_product(p1, normal1) - ON_EPSILON
				|| dot_product(p4, normal1)
					<= dot_product(p1, normal1) + ON_EPSILON
				|| dot_product(p1, normal2)
					>= dot_product(p3, normal2) - ON_EPSILON
				|| dot_product(p2, normal2)
					<= dot_product(p3, normal2) + ON_EPSILON) {
				// the edges are not crossing when viewed along normal
				continue;
			}
			float sqrdist = dot_product(p3, normal)
				- dot_product(p1, normal);
			sqrdist = sqrdist * sqrdist;
			if (sqrdist < minsqrdist) {
				minsqrdist = sqrdist;
			}
		}
	}
	// point to face and edge to face
	for (int side = 0; side < 2; side++) {
		float3_array planenormal;
		if (!BestNormalFromWinding(
				w[!side]->points, w[!side]->numpoints, planenormal
			)) {
			continue;
		}
		float const planedist = dot_product(
			planenormal, w[!side]->points[0]
		);

		if (w[!side]->numpoints > 11) {
			Error("######e%zu", std::size_t(w[!side]->numpoints));
		}

		usually_inplace_vector<float3_array, 20> boundnormals;
		usually_inplace_vector<float, 20> bounddists;
		boundnormals.reserve(w[!side]->numpoints);
		bounddists.reserve(w[!side]->numpoints);
		// build boundaries
		for (b = 0; b < w[!side]->numpoints; b++) {
			float3_array const & p1 = w[!side]->points[b];
			float3_array const & p2
				= w[!side]->points[(b + 1) % w[!side]->numpoints];

			boundnormals.emplace_back(
				cross_product(vector_subtract(p2, p1), planenormal)
			);

			float bd{ 1.0 };
			if (normalize_vector(boundnormals[b])) {
				bd = dot_product(p1, boundnormals[b]);
			}
			bounddists.emplace_back(bd);
		}
		for (a = 0; a < w[side]->numpoints; a++) {
			float3_array const & p = w[side]->points[a];
			for (b = 0; b < w[!side]->numpoints; b++) {
				if (dot_product(p, boundnormals[b]) - bounddists[b]
					>= -ON_EPSILON) {
					break;
				}
			}
			if (b < w[!side]->numpoints) {
				continue;
			}
			float sqrdist = dot_product(p, planenormal) - planedist;
			sqrdist = sqrdist * sqrdist;
			if (sqrdist < minsqrdist) {
				minsqrdist = sqrdist;
			}
		}
		for (a = 0; a < w[side]->numpoints; a++) {
			float3_array const & p1 = w[side]->points[a];
			float3_array const & p2
				= w[side]->points[(a + 1) % w[side]->numpoints];
			float dist1 = dot_product(p1, planenormal) - planedist;
			float dist2 = dot_product(p2, planenormal) - planedist;
			if (dist1 > ON_EPSILON && dist2 < -ON_EPSILON
				|| dist1 < -ON_EPSILON && dist2 > ON_EPSILON) {
				float frac = dist1 / (dist1 - dist2);
				float3_array delta = vector_subtract(p2, p1);
				float3_array v = vector_fma(delta, frac, p1);
				for (b = 0; b < w[!side]->numpoints; b++) {
					if (dot_product(v, boundnormals[b]) - bounddists[b]
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
	}
	return std::sqrt(minsqrdist);
}

// =====================================================================================
//  MaxDistVis
// =====================================================================================
void MaxDistVis(int unused_threadnum) {
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
				winding_t const * w;
				leaf_t const * leaf[2] = { l, tl };
				std::array<float3_array, 2> center{};
				std::array<int, 2> count{};
				for (std::size_t side = 0; side < 2; ++side) {
					for (a = 0; a < leaf[side]->numportals; a++) {
						w = leaf[side]->portals[a]->winding;
						for (b = 0; b < w->numpoints; b++) {
							center[side] = vector_add(
								center[side], w->points[b]
							);
							count[side]++;
						}
					}
				}
				if (!count[0] && !count[1]) {
					goto Work;
				}
				std::array<float, 2> radius{};
				for (int side = 0; side < 2; side++) {
					center[side] = vector_scale(
						center[side], 1.0f / float(count[side])
					);
					for (a = 0; a < leaf[side]->numportals; a++) {
						w = leaf[side]->portals[a]->winding;
						for (b = 0; b < w->numpoints; b++) {
							float3_array const v = vector_subtract(
								w->points[b], center[side]
							);
							float const dist = dot_product(v, v);
							radius[side] = std::max(radius[side], dist);
						}
					}
					radius[side] = std::sqrt(radius[side]);
				}

				float const dist = distance_between_points(
					center[0], center[1]
				);
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
