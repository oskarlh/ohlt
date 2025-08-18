#include "external_types/external_types.h"
#include "hlcsg.h"
#include "internal_types/various.h"
#include "log.h"
#include "mathlib.h"
#include "mathtypes.h"
#include "threads.h"
#include "util.h"
#include "vector_inplace.h"

vector_inplace<mapplane_t, MAX_INTERNAL_MAP_PLANES> g_mapPlanes;

std::array<hullshape_t, NUM_HULLS> g_defaulthulls{};
std::vector<hullshape_t> g_hullshapes{};

void init_default_hulls() {
	for (hullshape_t& dh : g_defaulthulls) {
		dh.disabled = true;
	}
}

constexpr float FLOOR_Z = 0.7f; // Quake default

// =====================================================================================
//  find_int_plane
// This process could be optimized by placing the planes in a (non hash-)
// set and using half of the inner loop check below as the comparator;
// I'd expect the speed gain to be very large given the change from O(N^2)
// to O(NlogN) to build the set of planes.
// =====================================================================================

static int
find_int_plane(double3_array const & normal, double3_array const & origin) {
	int returnval = 0;

	bool mapPlanesWasUpdated;
	do {
		// Can I use the "Always put axial planes facing positive first"
		// rule to my advantage?? And only check every other???

		for (; returnval < g_mapPlanes.size(); returnval++) {
			// BUG: there might be some multithread issue --vluzacn

			// Instead of comparing like this here........I can round
			// the values and see if the values are equal????
			// Pros: MUCH easier logic for reducing duplicates
			// Cons MAYBE: Very close points not matching sometimes,
			// preventing deduplication round_to_dir_epsilon
			double3_array const absDiffs = vector_abs(
				vector_subtract(normal, g_mapPlanes[returnval].normal)
			);
			if (vector_max_element(absDiffs) < PLANE_NORMAL_EPSILON
				&& std::abs(
					   dot_product(origin, g_mapPlanes[returnval].normal)
					   - g_mapPlanes[returnval].dist
				   ) < PLANE_DIST_EPSILON) {
				return returnval;
			}
		}

		ThreadLock(); // make sure we don't race
		mapPlanesWasUpdated = returnval
			!= g_mapPlanes.size(
			); // check to see if other thread added plane we need
		ThreadUnlock();
	} while (mapPlanesWasUpdated);

	// create new planes - double check that we have room for 2 planes
	hlassume(
		g_mapPlanes.size() + 1 < MAX_INTERNAL_MAP_PLANES,
		assume_MAX_INTERNAL_MAP_PLANES
	);

	std::size_t const pAIndex = g_mapPlanes.size();
	mapplane_t& pA = g_mapPlanes.emplace_back();

	pA.origin = origin;
	pA.normal = normal;
	normalize_vector(pA.normal);
	pA.type = plane_type_for_normal(pA.normal);
	//	planetype ot =
	// plane_type_for_normal(to_double3(to_float3(p->normal))); 	if
	// (p->type
	//!= ot) { 		Log("AAAA %.12f %.12f %.12f %i\n", p->normal[0],
	//			p->normal[1],
	//			p->normal[2],
	//			(int) p->type);
	//		Log("AAAB %.12f %.12f %.12f %i\n",
	//			to_float3(p->normal)[0],
	//			to_float3(p->normal)[1],
	//			to_float3(p->normal)[2],
	//			(int) ot);
	//		Error("w");
	//	}
	if (pA.type <= last_axial) {
		for (std::size_t i{ std::size_t(first_axial) };
			 i <= std::size_t(last_axial);
			 ++i) {
			if (i == std::size_t(pA.type)) {
				pA.normal[i] = pA.normal[i] > 0 ? 1 : -1;
			} else {
				pA.normal[i] = 0;
			}
		}
	}
	pA.dist = dot_product(origin, pA.normal);

	std::size_t const pBIndex = g_mapPlanes.size();
	mapplane_t& pB = g_mapPlanes.emplace_back();
	pB.origin = origin;

	pB.normal = negate_vector(pA.normal);
	pB.type = pA.type;
	pB.dist = -pA.dist;

	// Always put axial planes facing positive first
	if (normal[std::size_t(pA.type) % 3] < 0) {
		using std::swap;
		swap(pA, pB);
		returnval = pBIndex;
	} else {
		returnval = pAIndex;
	}

	ThreadUnlock();
	return returnval;
}

static int PlaneFromPoints(std::array<double3_array, 3> const & points) {
	double3_array v1 = vector_subtract(points[0], points[1]);
	double3_array v2 = vector_subtract(points[2], points[1]);
	double3_array normal = cross_product(v1, v2);

	if (normalize_vector(normal)) {
		return find_int_plane(normal, points[0]);
	}
	return -1;
}

char const ClipTypeStrings[3][11] = { { "precise" },
									  { "normalized" },
									  { "legacy" } };

char const * GetClipTypeString(cliptype ct) {
	return ClipTypeStrings[ct];
}

// =====================================================================================
//  AddHullPlane (subroutine for replacement of expand_brush)
//  Called to add any and all clip hull planes by the new expand_brush.
// =====================================================================================

static void AddHullPlane(
	brushhull_t* hull,
	double3_array const & normal,
	double3_array const & origin,
	bool const check_planenum
) {
	int planenum = find_int_plane(normal, origin);
	// check to see if this plane is already in the brush (optional to speed
	// up cases where we know the plane hasn't been added yet, like axial
	// case)
	if (check_planenum) {
		for (bface_t const & current_face : hull->faces) {
			if (current_face.planenum == planenum) {
				return;
			} // don't add a plane twice
		}
	}
	bface_t new_face{};
	new_face.planenum = planenum;
	new_face.plane = &g_mapPlanes[planenum];
	new_face.contents = contents_t::EMPTY;
	new_face.texinfo = no_texinfo;
	hull->faces.emplace_back(std::move(new_face));
}

// =====================================================================================
//  expand_brush
//  Since the six bounding box planes were always added anyway, they've been
//  moved to an explicit separate step eliminating the need to check for
//  duplicate planes (which should be using plane numbers instead of the
//  full definition anyway).
//
//  The core of the new function adds additional bevels to brushes
//  containing faces that have 3 nonzero normal components -- this is
//  necessary to finish the beveling process, but is turned off by default
//  for backward compatability and because the number of clipnodes and faces
//  will go up with the extra beveling.  The advantage of the extra
//  precision comes from the absense of "sticky" outside corners on ackward
//  geometry.
//
//  Another source of "sticky" walls has been the inconsistant offset along
//  each axis (variant with plane normal in the old code).  The normal
//  component of the offset has been scrapped (it made a ~29% difference in
//  the worst case of 45 degrees, or about 10 height units for a standard
//  Half-Life player hull).  The new offsets generate fewer clipping nodes
//  and won't cause players to stick when moving across 2 brushes that flip
//  sign along an axis (this wasn't noticible on floors because the engine
//  took care of the invisible 0-3 unit steps it generated, but was
//  noticible with walls).
//
//  To prevent players from floating 10 units above the floor, the "precise"
//  hull generation option still uses the plane normal when the Z component
//  is high enough for the plane to be considered a floor.
//
//  Bevel planes might be added twice (once from each side of the edge), so
//  a planenum based check is used to see if each has been added before.
// =====================================================================================
// Correction: //--vluzacn
//   Clipnode size depends on complexity of the surface of expanded brushes
//   as a whole, not number of brush sides. Data from a sample map:
//     cliptype          precise     legacy     normalized
//     clipnodecount        1089       1202           1232

static void expand_brush_with_hullbrush(
	csg_brush const * brush,
	brushhull_t const * hull0,
	hullbrush_t const & hb,
	brushhull_t* hull
) {
	auto axialbevel = std::make_unique<bool[]>(hb.faces.size());

	// check for collisions of face-vertex type. face-edge type is also
	// permitted. face-face type is excluded.
	for (bface_t const & f : hull0->faces) {
		hullbrushface_t brushface{};
		brushface.normal = f.plane->normal;
		brushface.point = f.plane->origin;

		// Check for coplanar hull brush face
		hullbrushface_t const * hbf;
		bool doContinue = false;
		for (hullbrushface_t const & hbf : hb.faces) {
			if (-dot_product(hbf.normal, brushface.normal)
				< 1 - ON_EPSILON) {
				continue;
			}
			// Now test precisely
			double dotmin = g_iWorldExtent;
			double dotmax = -g_iWorldExtent;
			hlassume(hbf.vertices.size() >= 1, assume_first);
			for (double3_array const & v : hbf.vertices) {
				double dot = dot_product(v, brushface.normal);
				dotmin = std::min(dotmin, dot);
				dotmax = std::max(dotmax, dot);
			}
			if (dotmax - dotmin <= EQUAL_EPSILON) {
				if (f.bevel) {
					std::size_t hbfIndex = &hbf - &hb.faces.front();
					axialbevel[hbfIndex] = true;
				}
				// The same plane will be added in the last stage
				doContinue = true;
				break;
			}
		}
		if (doContinue) {
			continue;
		}

		// find the impact point
		double3_array bestvertex{};
		double bestdist = g_iWorldExtent;
		hlassume(hb.vertices.size() >= 1, assume_first);
		bool first = true;
		for (hullbrushvertex_t const & hbv : hb.vertices) {
			if (first
				|| dot_product(hbv.point, brushface.normal)
					< bestdist - NORMAL_EPSILON) {
				bestdist = dot_product(hbv.point, brushface.normal);
				bestvertex = hbv.point;
			}
			first = false;
		}

		// Add hull plane for this face
		double3_array normal = brushface.normal;
		double3_array origin = brushface.point;
		if (!f.bevel) {
			origin = vector_subtract(origin, bestvertex);
		}
		AddHullPlane(hull, normal, origin, true);
	}

	bool warned = false;

	// check for edge-edge type. edge-face type and face-edge type are
	// excluded.
	for (bface_t const & f : hull0->faces) {
		for (int i = 0; i < f.w.size(); i++) // for each edge in f
		{
			hullbrushedge_t brushedge;
			brushedge.normals[0] = f.plane->normal;
			brushedge.vertexes[0] = f.w.point((i + 1) % f.w.size());
			brushedge.vertexes[1] = f.w.point(i);
			brushedge.point = brushedge.vertexes[0];
			brushedge.delta = vector_subtract(
				brushedge.vertexes[1], brushedge.vertexes[0]
			);

			// fill brushedge.normals[1]
			int found{ 0 };
			for (bface_t const & f2 : hull0->faces) {
				for (int j = 0; j < f2.w.size(); j++) {
					if (vectors_almost_same(
							f2.w.point((j + 1) % f2.w.size()),
							brushedge.vertexes[1]
						)
						&& vectors_almost_same(
							f2.w.point(j), brushedge.vertexes[0]
						)) {
						brushedge.normals[1] = f2.plane->normal;
						found++;
					}
				}
			}
			if (found != 1) {
				if (!warned) {
					Warning(
						"expand_brush_with_hullbrush: Illegal Brush (edge without opposite face): Entity %i, Brush %i\n",
						brush->originalentitynum,
						brush->originalbrushnum
					);
					warned = true;
				}
				continue;
			}

			// make sure the math is accurate
			double len = vector_length(brushedge.delta);
			brushedge.delta = cross_product(
				brushedge.normals[0], brushedge.normals[1]
			);
			if (!normalize_vector(brushedge.delta)) {
				continue;
			}
			brushedge.delta = vector_scale(brushedge.delta, len);

			// check for each edge in the hullbrush
			for (hullbrushedge_t const & hbe : hb.edges) {
				double dot[4];
				dot[0] = dot_product(hbe.delta, brushedge.normals[0]);
				dot[1] = dot_product(hbe.delta, brushedge.normals[1]);
				dot[2] = dot_product(brushedge.delta, hbe.normals[0]);
				dot[3] = dot_product(brushedge.delta, hbe.normals[1]);
				if (dot[0] <= ON_EPSILON || dot[1] >= -ON_EPSILON
					|| dot[2] <= ON_EPSILON || dot[3] >= -ON_EPSILON) {
					continue;
				}

				// in the outer loop, each edge in the brush will be
				// iterated twice (once from f and once from the
				// corresponding f2) but since brushedge.delta are exactly
				// the opposite between the two iterations only one of them
				// can reach here
				double3_array e1 = brushedge.delta;
				normalize_vector(e1);
				double3_array e2 = hbe.delta;
				normalize_vector(e2);
				double3_array normal = cross_product(e1, e2);
				if (!normalize_vector(normal)) {
					continue;
				}
				double3_array origin = vector_subtract(
					brushedge.point, hbe.point
				);
				AddHullPlane(hull, normal, origin, true);
			}
		}
	}

	// check for vertex-face type. edge-face type and face-face type are
	// permitted.
	for (hullbrushface_t const & hbf : hb.faces) {
		// find the impact point

		if (hull0->faces.empty()) {
			continue;
		}
		double3_array bestvertex;
		double bestdist = g_iWorldExtent;
		for (bface_t const & f : hull0->faces) {
			for (double3_array const & v : f.w.points()) {
				if (dot_product(v, hbf.normal)
					< bestdist - NORMAL_EPSILON) {
					bestdist = dot_product(v, hbf.normal);
					bestvertex = v;
				}
			}
		}

		// add hull plane for this face
		double3_array normal = negate_vector(hbf.normal);
		double3_array origin;
		std::size_t hbfIndex = &hbf - &hb.faces.front();
		if (axialbevel[hbfIndex]) {
			origin = bestvertex;
		} else {
			origin = vector_subtract(bestvertex, hbf.point);
		}
		AddHullPlane(hull, normal, origin, true);
	}
}

void expand_brush(csg_brush* brush, int const hullnum) {
	hullshape_t const * hs = &g_defaulthulls[hullnum];
	{ // look up the name of its hull shape in g_hullshapes[]
		std::u8string const & name = brush->hullshapes[hullnum];
		if (!name.empty()) {
			bool found = false;
			for (hullshape_t const & s : g_hullshapes) {
				if (name == s.id) {
					if (found) {
						Warning(
							"Entity %i, Brush %i: Found several info_hullshape entities with the same name '%s'.",
							brush->originalentitynum,
							brush->originalbrushnum,
							(char const *) name.c_str()
						);
					}
					hs = &s;
					found = true;
				}
			}
			if (!found) {
				Error(
					"Entity %i, Brush %i: Couldn't find info_hullshape entity '%s'.",
					brush->originalentitynum,
					brush->originalbrushnum,
					(char const *) name.c_str()
				);
			}
		}
	}

	if (!hs->disabled) {
		if (!hs->hullBrush.has_value()) {
			return; // leave this hull of this brush empty (noclip)
		}
		expand_brush_with_hullbrush(
			brush,
			&brush->hulls[0],
			hs->hullBrush.value(),
			&brush->hulls[hullnum]
		);

		return;
	}

	// Hull construction

	// Non-axial bevel testing results
	bool axialbevel[std::size_t(last_axial) + 1][2] = { { false, false },
														{ false, false },
														{ false, false } };

	brushhull_t* hull = &brush->hulls[hullnum];

	// step 1: for collision between player vertex and brush face. --vluzacn
	for (bface_t& current_face : brush->hulls[0].faces) {
		mapplane_t* current_plane = current_face.plane;

		// don't bother adding axial planes,
		// they're defined by adding the bounding box anyway
		if (current_plane->type <= last_axial) {
			// flag case where bounding box shouldn't expand
			if (current_face.bevel) {
				axialbevel[std::size_t(current_plane->type
				)][(current_plane->normal[std::size_t(current_plane->type)]
							> 0
						? 1
						: 0)]
					= true;
			}
			continue;
		}

		// add the offset non-axial plane to the expanded hull
		double3_array origin{ current_plane->origin };
		double3_array normal{ current_plane->normal };

		// old code multiplied offset by normal -- this led to post-csg
		// "sticky" walls where a slope met an axial plane from the next
		// brush since the offset from the slope would be less than the full
		// offset for the axial plane -- the discontinuity also contributes
		// to increased clipnodes.  If the normal is zero along an axis,
		// shifting the origin in that direction won't change the plane
		// number, so I don't explicitly test that case.  The old method is
		// still used if preciseclip is turned off to allow backward
		// compatability -- some of the improperly beveled edges grow using
		// the new origins, and might cause additional problems.

		if (current_face.bevel) {
			// don't adjust origin - we'll correct g_texinfo's flags in a
			// later step
		}
		// The old offset will generate an extremely small gap when the
		// normal is close to axis, causing epsilon errors (ambiguous
		// leafnode content, player falling into ground, etc.). For example:
		// with the old shifting method, slopes with angle arctan(1/8) and
		// arctan(1/64) will result in gaps of 0.0299 unit and 0.000488 unit
		// respectively, which are smaller than ON_EPSILON, while in both
		// 'simple' cliptype and the new method, the gaps are 2.0 units and
		// 0.25 unit, which are good. This also reduce the number of
		// clipnodes used for cliptype precise. The maximum difference in
		// height between the old offset and the new offset is 0.86 unit for
		// standing player and 6.9 units for ducking player. (when FLOOR_Z =
		// 0.7) And another reason not to use the old offset is that the old
		// offset is quite wierd. It might appears at first glance that it
		// regards the player as an ellipse, but in fact it isn't, and the
		// player's feet may even sink slightly into the ground
		// theoretically for slopes of certain angles.
		else if (g_cliptype == clip_precise && normal[2] > FLOOR_Z) {
			origin[0] += 0;
			origin[1] += 0;
			origin[2] += g_hull_size[hullnum][1][2];
		} else if (g_cliptype == clip_legacy
				   || g_cliptype == clip_normalized) {
			if (normal[0]) {
				origin[0] += normal[0]
					* (normal[0] > 0 ? g_hull_size[hullnum][1][0]
									 : -g_hull_size[hullnum][0][0]);
			}
			if (normal[1]) {
				origin[1] += normal[1]
					* (normal[1] > 0 ? g_hull_size[hullnum][1][1]
									 : -g_hull_size[hullnum][0][1]);
			}
			if (normal[2]) {
				origin[2] += normal[2]
					* (normal[2] > 0 ? g_hull_size[hullnum][1][2]
									 : -g_hull_size[hullnum][0][2]);
			}
		} else {
			origin[0] += g_hull_size[hullnum][(normal[0] > 0 ? 1 : 0)][0];
			origin[1] += g_hull_size[hullnum][(normal[1] > 0 ? 1 : 0)][1];
			origin[2] += g_hull_size[hullnum][(normal[2] > 0 ? 1 : 0)][2];
		}

		AddHullPlane(hull, normal, origin, false);
	} // end for loop over all faces

	// step 2: for collision between player edge and brush edge. --vluzacn

	// split bevel check into a second pass so we don't have to check for
	// duplicate planes when adding offset planes in step above -- otherwise
	// a bevel plane might duplicate an offset plane, causing problems later
	// on.

	// only executes if cliptype is simple, normalized or precise
	if (g_cliptype == clip_precise || g_cliptype == clip_normalized) {
		for (bface_t& current_face : brush->hulls[0].faces) {
			mapplane_t* current_plane = current_face.plane;

			// test to see if the plane is completely non-axial (if it is,
			// need to add bevels to any existing "inflection edges" where
			// there's a sign change with a neighboring plane's normal for
			// a given axis)

			// move along winding and find plane on other side of each edge.
			// If normals change sign, add a new plane by offsetting the
			// points of the winding to bevel the edge in that direction.
			// It's possible to have inflection in multiple directions -- in
			// this case, a new plane must be added for each sign change in
			// the edge.

			// For non-axial bevel testing
			mapplane_t* other_plane;
			unsigned int counter, counter2, dir;
			bool warned = false;

			accurate_winding const & winding = current_face.w;
			for (counter = 0; counter < (winding.size());
				 counter++) // for each edge
			{
				double3_array const edge_start{ winding.point(counter) };
				double3_array const edge_end{
					winding.point((counter + 1) % winding.size())
				};

				// Grab the edge (find relative length)
				double3_array edge = vector_subtract(edge_end, edge_start);
				double3_array bevel_edge;

				bface_t const * foundOtherFace{};
				// brute force - need to check every other winding for
				// common points -- if the points match, the other face is
				// the one we need to look at.
				for (bface_t const & other_face : brush->hulls[0].faces) {
					if (&other_face == &current_face) {
						continue;
					}
					bool start_found{ false };
					bool end_found{ false };
					accurate_winding const & other_winding{ other_face.w };
					for (counter2 = 0; counter2 < other_winding.size();
						 counter2++) {
						if (!start_found
							&& vectors_almost_same(
								other_winding.point(counter2), edge_start
							)) {
							start_found = true;
						}
						if (!end_found
							&& vectors_almost_same(
								other_winding.point(counter2), edge_end
							)) {
							end_found = true;
						}
						if (start_found && end_found) {
							break;
						} // we've found the face we want, move on to planar
						  // comparison
					} // for each point in other winding
					if (start_found && end_found) {
						foundOtherFace = &other_face;
						break;
					} // we've found the face we want, move on to planar
					  // comparison
				} // for each face

				if (!foundOtherFace) {
					if (hullnum == 1 && !warned) {
						Warning(
							"expand_brush: Illegal Brush (edge without opposite face): Entity %i, Brush %i\n",
							brush->originalentitynum,
							brush->originalbrushnum
						);
						warned = true;
					}
					continue;
				}

				other_plane = foundOtherFace->plane;

				// check each direction for sign change in normal -- zero
				// can be safely ignored
				for (dir = 0; dir < 3; dir++) {
					if (current_plane->normal[dir]
							* other_plane->normal[dir]
						< -NORMAL_EPSILON) // sign changed, add bevel
					{
						// pick direction of bevel edge by looking at normal
						// of existing planes
						bevel_edge = {};
						bevel_edge[dir] = (current_plane->normal[dir] > 0)
							? -1
							: 1;

						// find normal by taking normalized cross of the
						// edge vector and the bevel edge
						double3_array normal = cross_product(
							edge, bevel_edge
						);

						// normalize to length 1
						normalize_vector(normal);
						if (fabs(normal[(dir + 1) % 3]) <= NORMAL_EPSILON
							|| fabs(normal[(dir + 2) % 3])
								<= NORMAL_EPSILON) { // coincide with axial
													 // plane
							continue;
						}

						// get the origin
						double3_array origin = edge_start;

						// unrolled loop - legacy never hits this point, so
						// don't test for it
						if (g_cliptype == clip_precise
							&& normal[2] > FLOOR_Z) {
							origin[0] += 0;
							origin[1] += 0;
							origin[2] += g_hull_size[hullnum][1][2];
						} else if (g_cliptype == clip_normalized) {
							if (normal[0]) {
								origin[0] += normal[0]
									* (normal[0] > 0
										   ? g_hull_size[hullnum][1][0]
										   : -g_hull_size[hullnum][0][0]);
							}
							if (normal[1]) {
								origin[1] += normal[1]
									* (normal[1] > 0
										   ? g_hull_size[hullnum][1][1]
										   : -g_hull_size[hullnum][0][1]);
							}
							if (normal[2]) {
								origin[2] += normal[2]
									* (normal[2] > 0
										   ? g_hull_size[hullnum][1][2]
										   : -g_hull_size[hullnum][0][2]);
							}
						} else // simple or precise for non-floors
						{
							// note: if normal == 0 in direction indicated,
							// shifting origin doesn't change plane #
							origin[0]
								+= g_hull_size[hullnum]
											  [(normal[0] > 0 ? 1 : 0)][0];
							origin[1]
								+= g_hull_size[hullnum]
											  [(normal[1] > 0 ? 1 : 0)][1];
							origin[2]
								+= g_hull_size[hullnum]
											  [(normal[2] > 0 ? 1 : 0)][2];
						}

						// add the bevel plane to the expanded hull
						AddHullPlane(
							hull, normal, origin, true
						); // double check that this edge hasn't been added
						   // yet
					}
				} // end for loop (check for each direction)
			} // end for loop (over all edges in face)
		} // end for loop (over all faces in hull 0)
	} // end if completely non-axial

	// step 3: for collision between player face and brush vertex. --vluzacn

	// add the bounding box to the expanded hull -- for a
	// completely axial brush, this is the only necessary step

	// add mins
	double3_array origin{
		vector_add(brush->hulls[0].bounds.mins, g_hull_size[hullnum][0])
	};
	double3_array normal{ -1, 0, 0 };
	AddHullPlane(
		hull,
		normal,
		(axialbevel[std::size_t(planetype::plane_x)][0]
			 ? brush->hulls[0].bounds.mins
			 : origin),
		false
	);
	normal[0] = 0;
	normal[1] = -1;
	AddHullPlane(
		hull,
		normal,
		(axialbevel[std::size_t(planetype::plane_y)][0]
			 ? brush->hulls[0].bounds.mins
			 : origin),
		false
	);
	normal[1] = 0;
	normal[2] = -1;
	AddHullPlane(
		hull,
		normal,
		(axialbevel[std::size_t(planetype::plane_z)][0]
			 ? brush->hulls[0].bounds.mins
			 : origin),
		false
	);

	normal[2] = 0;

	// add maxes
	origin = vector_add(
		brush->hulls[0].bounds.maxs, g_hull_size[hullnum][1]
	);
	normal[0] = 1;
	AddHullPlane(
		hull,
		normal,
		(axialbevel[std::size_t(planetype::plane_x)][1]
			 ? brush->hulls[0].bounds.maxs
			 : origin),
		false
	);
	normal[0] = 0;
	normal[1] = 1;
	AddHullPlane(
		hull,
		normal,
		(axialbevel[std::size_t(planetype::plane_y)][1]
			 ? brush->hulls[0].bounds.maxs
			 : origin),
		false
	);
	normal[1] = 0;
	normal[2] = 1;
	AddHullPlane(
		hull,
		normal,
		(axialbevel[std::size_t(planetype::plane_z)][1]
			 ? brush->hulls[0].bounds.maxs
			 : origin),
		false
	);
}

// =====================================================================================
//  make_hullfaces
// =====================================================================================
void SortSides(brushhull_t* h) {
	// The only reason it's a stable sort is so we get identical .bsp files
	// no matter which implementation of the C++ standard library is used
	std::stable_sort(
		h->faces.begin(),
		h->faces.end(),
		[](bface_t const & a, bface_t const & b) {
			double3_array const normalsA = g_mapPlanes[a.planenum].normal;
			int axialA = (fabs(normalsA[0]) < NORMAL_EPSILON)
				+ (fabs(normalsA[1]) < NORMAL_EPSILON)
				+ (fabs(normalsA[2]) < NORMAL_EPSILON);
			double3_array const normalsB = g_mapPlanes[b.planenum].normal;
			int axialB = (fabs(normalsB[0]) < NORMAL_EPSILON)
				+ (fabs(normalsB[1]) < NORMAL_EPSILON)
				+ (fabs(normalsB[2]) < NORMAL_EPSILON);
			return axialA > axialB;
		}
	);
}

static void make_hullfaces(csg_brush const & b, brushhull_t& h) {
	SortSides(&h);

restart:
	h.bounds = empty_bounding_box;

	// for each face in this brushes hull
	for (bface_t& f : h.faces) {
		accurate_winding w{ f.plane->normal, f.plane->dist };
		for (bface_t const & f2 : h.faces) {
			if (&f == &f2) {
				continue;
			}
			mapplane_t const * p = &g_mapPlanes[f2.planenum ^ 1];
			if (!w.Chop(
					p->normal,
					p->dist,
					NORMAL_EPSILON // fix "invalid brush" in expand_brush
				)) // Nothing left to chop (getArea will return 0 for us in
				   // this case for below)
			{
				break;
			}
		}
		w.RemoveColinearPoints(ON_EPSILON);
		if (w.getArea() < 0.1) {
			std::size_t const index{ std::size_t(&f - &(h.faces.front())) };
			h.faces.erase(h.faces.begin() + index);
			goto restart;
		} else {
			f.w = w;
			f.contents = contents_t::EMPTY;
			for (std::size_t i = 0; i < w.size(); ++i) {
				add_to_bounding_box(h.bounds, w.point(i));
			}
		}
	}

	for (std::size_t i = 0; i < 3; ++i) {
		if (h.bounds.mins[i] < -g_iWorldExtent / 2
			|| h.bounds.maxs[i] > g_iWorldExtent / 2) {
			Fatal(
				assume_BRUSH_OUTSIDE_WORLD,
				"Entity %i, Brush %i: outside world(+/-%d): (%.0f,%.0f,%.0f)-(%.0f,%.0f,%.0f)",
				b.originalentitynum,
				b.originalbrushnum,
				g_iWorldExtent / 2,
				h.bounds.mins[0],
				h.bounds.mins[1],
				h.bounds.mins[2],
				h.bounds.maxs[0],
				h.bounds.maxs[1],
				h.bounds.maxs[2]
			);
		}
	}
}

// =====================================================================================
//  MakeBrushPlanes
// =====================================================================================
static bool MakeBrushPlanes(csg_brush& b, csg_entity const & entity) {
	int j;
	int planenum;

	//
	// if the origin key is set (by an origin brush), offset all of the
	// values
	//
	double3_array const origin = get_double3_for_key(entity, u8"origin");

	//
	// convert to mapplanes
	//
	// for each side in this brush
	for (side_count i = 0; i < b.numSides; ++i) {
		side_t* s = &g_brushsides[b.firstSide + i];
		for (std::size_t j = 0; j < 3; ++j) {
			s->planepts[j] = vector_subtract(s->planepts[j], origin);
		}
		planenum = PlaneFromPoints(s->planepts);
		if (planenum == -1) {
			Fatal(
				assume_PLANE_WITH_NO_NORMAL,
				"Entity %i, Brush %i, Side %i: plane with no normal",
				b.originalentitynum,
				b.originalbrushnum,
				i
			);
		}

		//
		// see if the plane has been used already
		//
		for (bface_t const & f : b.hulls[0].faces) {
			if (f.planenum == planenum || f.planenum == (planenum ^ 1)) {
				Fatal(
					assume_BRUSH_WITH_COPLANAR_FACES,
					"Entity %i, Brush %i, Side %i: has a coplanar plane at (%.0f, %.0f, %.0f), texture %s",
					b.originalentitynum,
					b.originalbrushnum,
					i,
					s->planepts[0][0] + origin[0],
					s->planepts[0][1] + origin[1],
					s->planepts[0][2] + origin[2],
					s->td.name.c_str()
				);
			}
		}

		bface_t new_face{};
		new_face.planenum = planenum;
		new_face.plane = &g_mapPlanes[planenum];
		new_face.texinfo = g_onlyents
			? 0
			: TexinfoForBrushTexture(new_face.plane, &s->td, origin);
		new_face.bevel = b.bevel || s->bevel;
		b.hulls[0].faces.emplace_back(std::move(new_face));
	}

	return true;
}

// =====================================================================================
//  TextureContents
// =====================================================================================
static contents_t TextureContents(wad_texture_name name) {
	if (name.is_any_content_type()) {
		if (name.is_contentsolid()) {
			return contents_t::SOLID;
		}
		if (name.is_contentwater()) {
			return contents_t::WATER;
		}
		if (name.is_contentempty()) {
			return contents_t::TOEMPTY;
		}
		if (name.is_contentsky()) {
			return contents_t::SKY;
		}
	}
	if (name.is_ordinary_sky() || name.is_env_sky()) {
		return contents_t::SKY;
	}

	if (name.is_any_liquid()) {
		if (name.is_lava()) {
			return contents_t::LAVA;
		}
		if (name.is_slime()) {
			return contents_t::SLIME;
		}

		if (name.is_water_with_current_0()) {
			return contents_t::CURRENT_0;
		}
		if (name.is_water_with_current_90()) {
			return contents_t::CURRENT_90;
		}
		if (name.is_water_with_current_180()) {
			return contents_t::CURRENT_180;
		}
		if (name.is_water_with_current_270()) {
			return contents_t::CURRENT_270;
		}
		if (name.is_water_with_current_down()) {
			return contents_t::CURRENT_DOWN;
		}
		if (name.is_water_with_current_up()) {
			return contents_t::CURRENT_UP;
		}
		return contents_t::WATER;
	}

	if (name.is_origin()) {
		return contents_t::ORIGIN;
	}
	if (name.is_bounding_box()) {
		return contents_t::BOUNDINGBOX;
	}

	if (name.is_solid_hint() || name.is_bevel_hint()
		|| name.is_ordinary_null() || name.is_ordinary_bevel()) {
		return contents_t::CONTENTS_NULL;
	}
	if (name.is_splitface()) {
		return contents_t::HINT;
	}
	if (name.is_ordinary_hint()) {
		return contents_t::TOEMPTY;
	}
	if (name.is_skip()) {
		return contents_t::TOEMPTY;
	}

	if (name.is_transculent()) {
		return contents_t::TRANSLUCENT;
	}

	return contents_t::SOLID;
}

// =====================================================================================
//  CheckBrushContents
//      Perfoms abitrary checking on brush surfaces and states to try and
//      catch errors
// =====================================================================================
contents_t CheckBrushContents(csg_brush const * const b) {
	contents_t best_contents;
	contents_t contents;
	side_t* s;
	bool assigned = false;

	s = &g_brushsides[b->firstSide];

	// cycle though the sides of the brush and attempt to get our best side
	// contents for
	//  determining overall brush contents
	if (b->numSides == 0) {
		Error(
			"Entity %i, Brush %i: Brush with no sides.\n",
			b->originalentitynum,
			b->originalbrushnum
		);
	}
	wad_texture_name const textureNameA{ s->td.name };
	best_contents = TextureContents(textureNameA);
	// Difference between SKIP, ContentEmpty:
	// SKIP doesn't split space in bsp process, ContentEmpty splits space
	// normally.
	if (textureNameA.is_any_content_type() || textureNameA.is_skip()) {
		assigned = true;
	}
	s++;
	side_count best_i{ 0 };
	for (side_count i = 1; i < b->numSides; i++, s++) {
		wad_texture_name const textureNameB{ s->td.name };
		contents_t contents_consider = TextureContents(textureNameB);
		if (assigned) {
			continue;
		}
		if (textureNameB.is_any_content_type() || textureNameB.is_skip()) {
			best_i = i;
			best_contents = contents_consider;
			assigned = true;
		}
		if (contents_consider > best_contents) {
			best_i = i;
			// if our current surface contents is better (larger) than our
			// best, make it our best.
			best_contents = contents_consider;
		}
	}
	contents = best_contents;

	// attempt to pick up on mixed_face_contents errors
	s = &g_brushsides[b->firstSide];
	for (side_count i = 0; i < b->numSides; i++, s++) {
		wad_texture_name const textureNameB{ s->td.name };
		contents_t const contents2 = TextureContents(textureNameB);
		if (assigned && !textureNameB.is_any_content_type()
			&& !textureNameB.is_skip() && contents2 != contents_t::ORIGIN
			&& contents2 != contents_t::HINT
			&& contents2 != contents_t::BOUNDINGBOX) {
			continue; // overwrite content for this texture
		}

		// Sky and null types are not to cause mixed face contents
		if (contents2 == contents_t::SKY) {
			continue;
		}

		if (contents2 == contents_t::CONTENTS_NULL) {
			continue;
		}

		if (contents2 != best_contents) {
			Fatal(
				assume_MIXED_FACE_CONTENTS,
				"Entity %i, Brush %i: mixed face contents\n    Texture %s and %s",
				b->originalentitynum,
				b->originalbrushnum,
				g_brushsides[b->firstSide + best_i].td.name.c_str(),
				s->td.name.c_str()
			);
		}
	}
	if (contents == contents_t::CONTENTS_NULL) {
		contents = contents_t::SOLID;
	}

	// check to make sure we dont have an origin brush as part of worldspawn
	if ((b->entitynum == 0)
		|| classname_is(&g_entities[b->entitynum], u8"func_group")) {
		if (contents == contents_t::ORIGIN && b->entitynum == 0
			|| contents == contents_t::BOUNDINGBOX) {
			Fatal(
				assume_BRUSH_NOT_ALLOWED_IN_WORLD,
				"Entity %i, Brush %i: %s brushes not allowed in world\n(did you forget to tie this origin brush to a rotating entity?)",
				b->originalentitynum,
				b->originalbrushnum,
				(char const *) ContentsToString(contents).data()
			);
		}
	} else {
		// otherwise its not worldspawn, therefore its an entity. check to
		// make sure this brush is allowed
		//  to be an entity.
		switch (contents) {
			case contents_t::SOLID:
			case contents_t::WATER:
			case contents_t::SLIME:
			case contents_t::LAVA:
			case contents_t::ORIGIN:
			case contents_t::BOUNDINGBOX:
			case contents_t::HINT:
			case contents_t::TOEMPTY:
				break;
			default:
				Fatal(
					assume_BRUSH_NOT_ALLOWED_IN_ENTITY,
					"Entity %i, Brush %i: %s brushes not allowed in entity",
					b->originalentitynum,
					b->originalbrushnum,
					(char const *) ContentsToString(contents).data()
				);
				break;
		}
	}

	return contents;
}

void create_brush(csg_brush& b, csg_entity const & ent) {
	contents_t contents = b.contents;

	if (contents == contents_t::ORIGIN) {
		return;
	}
	if (contents == contents_t::BOUNDINGBOX) {
		return;
	}

	// HULL 0
	MakeBrushPlanes(b, ent);
	make_hullfaces(b, b.hulls[0]);

	if (contents == contents_t::HINT) {
		return;
	}
	if (contents == contents_t::TOEMPTY) {
		return;
	}

	if (g_noclip) {
		if (b.cliphull) {
			// Is this necessary?
			b.hulls[0].faces.clear();
		}
		return;
	}

	if (b.cliphull) {
		for (std::size_t h = 1; h < NUM_HULLS; ++h) {
			if (b.cliphull & (1 << h)) {
				expand_brush(&b, h);
				make_hullfaces(b, b.hulls[h]);
			}
		}
		b.contents = contents_t::SOLID;
		// Is this necessary?
		b.hulls[0].faces.clear();
	} else if (!b.noclip) {
		for (std::size_t h = 1; h < NUM_HULLS; ++h) {
			expand_brush(&b, h);
			make_hullfaces(b, b.hulls[h]);
		}
	}
}

hullbrush_t CreateHullBrush(csg_brush const & b) {
	constexpr std::size_t MAXSIZE = 256;

	bool failed = false;

	// Planes
	int numplanes = 0;
	mapplane_t planes[MAXSIZE];
	double3_array origin = get_double3_for_key(
		g_entities[b.entitynum], u8"origin"
	);

	for (side_count i = 0; i < b.numSides; ++i) {
		std::array<double3_array, 3> p;
		planetype axial;

		side_t* s = &g_brushsides[b.firstSide + i];
		for (std::size_t j = 0; j < 3; ++j) {
			p[j] = vector_subtract(s->planepts[j], origin);
			for (std::size_t k = 0; k < 3; ++k) {
				if (fabs(p[j][k] - floor(p[j][k] + 0.5)) <= ON_EPSILON
					&& p[j][k] != floor(p[j][k] + 0.5)) {
					Warning(
						"Entity %i, Brush %i: vertex (%4.8f %4.8f %4.8f) of an info_hullshape entity is slightly off-grid.",
						b.originalentitynum,
						b.originalbrushnum,
						p[j][0],
						p[j][1],
						p[j][2]
					);
				}
			}
		}
		double3_array normal = cross_product(
			vector_subtract(p[0], p[1]), vector_subtract(p[2], p[1])
		);
		if (!normalize_vector(normal)) {
			failed = true;
			continue;
		}
		for (std::size_t k = 0; k < 3; ++k) {
			if (fabs(normal[k]) < NORMAL_EPSILON) {
				normal[k] = 0.0;
				normalize_vector(normal);
			}
		}
		axial = plane_type_for_normal(normal);
		if (axial <= last_axial) {
			double const sign = normal[std::size_t(axial)] > 0 ? 1 : -1;
			normal = double3_array{};
			normal[std::size_t(axial)] = sign;
		}

		if (numplanes >= MAXSIZE) {
			failed = true;
			continue;
		}
		planes[numplanes].normal = normal;
		planes[numplanes].dist = dot_product(p[1], normal);
		numplanes++;
	}

	// windings
	std::vector<accurate_winding> windings;
	for (std::size_t i = 0; i < numplanes; i++) {
		windings.emplace_back(planes[i].normal, planes[i].dist);
		for (std::size_t j = 0; j < numplanes; j++) {
			if (j == i) {
				continue;
			}
			double dist;
			double3_array const normal = negate_vector(planes[j].normal);
			dist = -planes[j].dist;
			if (!windings[i].Chop(normal, dist)) {
				failed = true;
				break;
			}
		}
	}

	// Edges
	int numedges = 0;
	hullbrushedge_t edges[MAXSIZE];
	for (std::size_t i = 0; i < numplanes; i++) {
		for (int e = 0; e < windings[i].size(); e++) {
			hullbrushedge_t* edge;
			if (numedges >= MAXSIZE) {
				failed = true;
				continue;
			}
			edge = &edges[numedges];
			edge->vertexes[0] = windings[i].point(
				(e + 1) % windings[i].size()
			);
			edge->vertexes[1] = windings[i].point(e);
			edge->point = edge->vertexes[0];
			edge->delta = vector_subtract(
				edge->vertexes[1], edge->vertexes[0]
			);
			if (vector_length(edge->delta) < 1 - ON_EPSILON) {
				failed = true;
				continue;
			}
			edge->normals[0] = planes[i].normal;
			std::size_t found{ 0 };
			std::size_t j;
			for (std::size_t k = 0; k < numplanes; ++k) {
				for (int e2 = 0; e2 < windings[k].size(); e2++) {
					if (vectors_almost_same(
							windings[k].point(
								(e2 + 1) % windings[k].size()
							),
							edge->vertexes[1]
						)
						&& vectors_almost_same(
							windings[k].point(e2), edge->vertexes[0]
						)) {
						++found;
						edge->normals[1] = planes[k].normal;
						j = k;
					}
				}
			}
			if (found != 1) {
				failed = true;
				continue;
			}
			if (fabs(
					dot_product(edge->vertexes[0], edge->normals[0])
					- planes[i].dist
				) > NORMAL_EPSILON
				|| fabs(
					   dot_product(edge->vertexes[1], edge->normals[0])
					   - planes[i].dist
				   ) > NORMAL_EPSILON
				|| fabs(
					   dot_product(edge->vertexes[0], edge->normals[1])
					   - planes[j].dist
				   ) > NORMAL_EPSILON
				|| fabs(
					   dot_product(edge->vertexes[1], edge->normals[1])
					   - planes[j].dist
				   ) > NORMAL_EPSILON) {
				failed = true;
				continue;
			}
			if (j > i) {
				numedges++;
			}
		}
	}

	// Vertices
	int numvertices = 0;
	hullbrushvertex_t vertices[MAXSIZE];
	for (std::size_t i = 0; i < numplanes; ++i) {
		for (int e = 0; e < windings[i].size(); e++) {
			double3_array const & v = windings[i].point(e);
			std::size_t j;
			for (j = 0; j < numvertices; j++) {
				if (vectors_almost_same(vertices[j].point, v)) {
					break;
				}
			}
			if (j < numvertices) {
				continue;
			}
			if (numvertices > MAXSIZE) {
				failed = true;
				continue;
			}

			vertices[numvertices].point = v;
			numvertices++;

			for (std::size_t k = 0; k < numplanes; k++) {
				if (fabs(dot_product(v, planes[k].normal) - planes[k].dist)
					< ON_EPSILON) {
					if (fabs(
							dot_product(v, planes[k].normal)
							- planes[k].dist
						)
						> NORMAL_EPSILON) {
						failed = true;
					}
				}
			}
		}
	}

	if (failed) {
		Error(
			"Entity %i, Brush %i: invalid brush. This brush cannot be used for info_hullshape.",
			b.originalentitynum,
			b.originalbrushnum
		);
	}

	// Copy to hull brush
	hullbrush_t hb{};
	hb.faces.reserve(numplanes);
	for (std::size_t i = 0; i < numplanes; i++) {
		hullbrushface_t* f = &hb.faces.emplace_back();
		f->normal = planes[i].normal;
		f->point = windings[i].point(0);
		f->vertices.assign_range(windings[i].points()
		); // TODO: Should we just make f->vertices's type
		   // accurate_winding?? then we can just move the winding there
	}

	hb.edges.assign_range(std::span{ edges, edges + numedges });

	hb.vertices.assign_range(std::span{ vertices, vertices + numvertices });

	Developer(
		developer_level::message,
		"info_hullshape @ (%.0f,%.0f,%.0f): %zu faces, %zu edges, %zu vertexes.\n",
		origin[0],
		origin[1],
		origin[2],
		hb.faces.size(),
		hb.edges.size(),
		hb.vertices.size()
	);

	return hb;
}

void create_hullshape(csg_entity const & fromInfoHullshapeEntity) {
	bool const disabled = bool_key_value(
		fromInfoHullshapeEntity, u8"disabled"
	);
	std::u8string_view const id = get_targetname(fromInfoHullshapeEntity);
	hull_bitset defaulthulls
		= IntForKey(&fromInfoHullshapeEntity, u8"defaulthulls")
		& all_hulls_bitset;

	if (!has_key_value(&fromInfoHullshapeEntity, u8"origin")) {
		Warning("info_hullshape with no ORIGIN brush.");
	}
	hullshape_t& hs = g_hullshapes.emplace_back(hullshape_t{
		.id = std::u8string{ id },
		.hullBrush = std::nullopt,
		.disabled = disabled,
	});

	for (entity_local_brush_count i = 0;
		 i < fromInfoHullshapeEntity.numbrushes;
		 i++) {
		csg_brush const & b
			= g_mapbrushes[fromInfoHullshapeEntity.firstBrush + i];
		if (b.contents == contents_t::ORIGIN) {
			continue;
		}

		if (hs.hullBrush.has_value()) {
			csg_brush* b
				= &g_mapbrushes[fromInfoHullshapeEntity.firstBrush];
			Error(
				"Entity %i, Brush %i: Too many brushes in info_hullshape.",
				b->originalentitynum,
				b->originalbrushnum
			);
		}

		hs.hullBrush = CreateHullBrush(b);
	}

	if (!hs.hullBrush.has_value()) {
		Error(
			"info_hullshape without a brush. Every info_hullshape needs ONE origin brush and ONE regular brush defining the shape."
		);
	}

	for (hull_count hullNumber : indicies_of_set_bits(defaulthulls)) {
		g_defaulthulls[hullNumber] = hs;
	}
}
