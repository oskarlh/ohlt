#include "csg.h"

double g_BrushUnionThreshold = DEFAULT_BRUSH_UNION_THRESHOLD;

static accurate_winding
NewWindingFromPlane(brushhull_t const * const hull, int const planenum) {
	mapplane_t* plane = &g_mapplanes[planenum];
	accurate_winding winding{ plane->normal, plane->dist };

	accurate_winding back;
	accurate_winding front;

	for (bface_t const & face : hull->faces) {
		plane = &g_mapplanes[face.planenum];
		winding.Clip(plane->normal, plane->dist, front, back);

		using std::swap;
		swap(winding, back);

		if (!winding) {
			Developer(
				developer_level::error, "NewFaceFromPlane returning NULL"
			);
			break;
		}
	}

	return winding;
}

// Returns false if union of brushes is obviously zero
static void AddPlaneToUnion(brushhull_t* hull, int const planenum) {
	bool need_new_face = false;

	bface_t* new_face_list;

	bface_t* face;

	mapplane_t* split;
	new_face_list = nullptr;
	hlassert(hull);

	if (hull->faces.empty()) {
		return;
	}

	std::vector<bface_t> newFaceList;
	for (bface_t& face : hull->faces) {
		// Duplicate plane, ignore
		if (face.planenum == planenum) {
			newFaceList.emplace_back(CopyFace(face));
			continue;
		}

		split = &g_mapplanes[planenum];
		accurate_winding front;
		accurate_winding back;
		face.w.Clip(split->normal, split->dist, front, back);

		if (front) {
			front.clear();
			need_new_face = true;

			if (back) { // Intersected the face
				face.w = std::move(back);
				newFaceList.emplace_back(CopyFace(face));
			}
		} else {
			// Completely missed it, back is identical to face->w so it is
			// destroyed
			if (back) {
				newFaceList.emplace_back(CopyFace(face));
			}
		}
	}
	hull->faces = std::move(newFaceList);

	if (need_new_face && hull->faces.size() > 2) {
		accurate_winding new_winding{ NewWindingFromPlane(hull, planenum) };

		if (new_winding) {
			bface_t newFace{};
			newFace.planenum = planenum;
			newFace.w = std::move(new_winding);
			hull->faces.emplace_back(std::move(newFace));
		}
	}
}

static double CalculateSolidVolume(brushhull_t const * const hull) {
	// calculate polyhedron origin
	// subdivide face winding into triangles

	// for each face
	// calculate volume of triangle of face to origin
	// add subidivided volume chunk to total

	double volume = 0.0;
	double inverse;
	double3_array midpoint = { 0.0, 0.0, 0.0 };

	int x = 0;
	for (bface_t const & face : hull->faces) {
		double3_array facemid = face.w.getCenter();
		VectorAdd(midpoint, facemid, midpoint);
		Developer(
			developer_level::message, "Midpoint for face %d is %f %f %f\n",
			x, facemid[0], facemid[1], facemid[2]
		);
		++x;
	}

	inverse = 1.0 / x;

	midpoint = vector_scale(midpoint, inverse);

	Developer(
		developer_level::message, "Midpoint for hull is %f %f %f\n",
		midpoint[0], midpoint[1], midpoint[2]
	);

	for (bface_t const & face : hull->faces) {
		mapplane_t* plane = &g_mapplanes[face.planenum];
		double area = face.w.getArea();
		double dist = DotProduct(plane->normal, midpoint);

		dist -= plane->dist;
		dist = fabs(dist);

		volume += area * dist / 3.0;
	}

	Developer(developer_level::message, "Volume for brush is %f\n", volume);

	return volume;
}

static void DumpHullWindings(brushhull_t const * const hull) {
	int x = 0;
	bface_t* face;

	for (bface_t const & face : hull->faces) {
		Developer(developer_level::megaspam, "accurate_winding %d\n", x++);
		face.w.Print();
		Developer(developer_level::megaspam, "\n");
	}
}

static bool isInvalidHull(brushhull_t const * hull) {
	double3_array mins{ 99999.0, 99999.0, 99999.0 };
	double3_array maxs{ -99999.0, -99999.0, -99999.0 };

	for (bface_t const & face : hull->faces) {
		for (double3_array const & windingPoint : face.w.m_Points) {
			mins = vector_minimums(mins, windingPoint);
			maxs = vector_maximums(maxs, windingPoint);
		}
	}

	for (std::size_t x = 0; x < 3; ++x) {
		if ((mins[x] < (-g_iWorldExtent / 2))
			|| (maxs[x] > (g_iWorldExtent / 2))) {
			return true;
		}
	}
	return false;
}

void CalculateBrushUnions(int const brushnum) {
	int bn, hull;
	brush_t* b1;
	brush_t* b2;
	brushhull_t* bh1;
	brushhull_t* bh2;
	entity_t* e;

	b1 = &g_mapbrushes[brushnum];
	e = &g_entities[b1->entitynum];

	for (hull = 0; hull < 1 /* NUM_HULLS */; hull++) {
		bh1 = &b1->hulls[hull];
		if (bh1->faces.empty()) // Skip it if it is not in this hull
		{
			continue;
		}

		for (bn = brushnum + 1; bn < e->numbrushes;
			 bn++) { // Only compare if b2 > b1, tests are communitive
			b2 = &g_mapbrushes[e->firstbrush + bn];
			bh2 = &b2->hulls[hull];

			if (bh2->faces.empty()) // Skip it if it is not in this hull
			{
				continue;
			}
			if (b1->contents != b2->contents) {
				continue; // different contents, ignore
			}

			Developer(
				developer_level::spam,
				"Processing hull %d brush %d and brush %d\n", hull,
				brushnum, bn
			);

			{
				brushhull_t union_hull;

				union_hull.bounds = bh1->bounds;

				union_hull.faces = CopyFaceList(bh1->faces);

				for (bface_t const & face : bh2->faces) {
					AddPlaneToUnion(&union_hull, face.planenum);
				}

				// union was clipped away (no intersection)
				if (union_hull.faces.empty()) {
					continue;
				}

				if (g_developer >= developer_level::message) {
					Log("\nUnion windings\n");
					DumpHullWindings(&union_hull);

					Log("\nBrush %d windings\n", brushnum);
					DumpHullWindings(bh1);

					Log("\nBrush %d windings\n", bn);
					DumpHullWindings(bh2);
				}

				{
					double volume_brush_1;
					double volume_brush_2;
					double volume_brush_union;
					double volume_ratio_1;
					double volume_ratio_2;

					if (isInvalidHull(&union_hull)) {
						union_hull.faces.clear();
						continue;
					}

					volume_brush_union = CalculateSolidVolume(&union_hull);
					volume_brush_1 = CalculateSolidVolume(bh1);
					volume_brush_2 = CalculateSolidVolume(bh2);

					volume_ratio_1 = volume_brush_union / volume_brush_1;
					volume_ratio_2 = volume_brush_union / volume_brush_2;

					if ((volume_ratio_1 > g_BrushUnionThreshold)
						|| (g_developer >= developer_level::message)) {
						volume_ratio_1 *= 100.0;
						Warning(
							"Entity %d : Brush %d intersects with brush %d by %2.3f percent",
							b1->originalentitynum, b1->originalbrushnum,
							b2->originalbrushnum, volume_ratio_1
						);
					}
					if ((volume_ratio_2 > g_BrushUnionThreshold)
						|| (g_developer >= developer_level::message)) {
						volume_ratio_2 *= 100.0;
						Warning(
							"Entity %d : Brush %d intersects with brush %d by %2.3f percent",
							b1->originalentitynum, b2->originalbrushnum,
							b1->originalbrushnum, volume_ratio_2
						);
					}
				}

				union_hull.faces.clear();
			}
		}
	}
}
