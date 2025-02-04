#include "qrad.h"

static dplane_t backplanes[MAX_MAP_PLANES];

dleaf_t* PointInLeaf_Worst_r(int nodenum, float3_array const & point) {
	float dist;
	dnode_t* node;
	dplane_t* plane;

	while (nodenum >= 0) {
		node = &g_dnodes[nodenum];
		plane = &g_dplanes[node->planenum];
		dist = dot_product(point, plane->normal) - plane->dist;
		if (dist > HUNT_WALL_EPSILON) {
			nodenum = node->children[0];
		} else if (dist < -HUNT_WALL_EPSILON) {
			nodenum = node->children[1];
		} else {
			dleaf_t* result[2];
			result[0] = PointInLeaf_Worst_r(node->children[0], point);
			result[1] = PointInLeaf_Worst_r(node->children[1], point);
			if (result[0] == g_dleafs.data()
				|| result[0]->contents == contents_t::SOLID) {
				return result[0];
			}
			if (result[1] == g_dleafs.data()
				|| result[1]->contents == contents_t::SOLID) {
				return result[1];
			}
			if (result[0]->contents == contents_t::SKY) {
				return result[0];
			}
			if (result[1]->contents == contents_t::SKY) {
				return result[1];
			}
			if (result[0]->contents == result[1]->contents) {
				return result[0];
			}
			return g_dleafs.data();
		}
	}

	return &g_dleafs[-nodenum - 1];
}

dleaf_t* PointInLeaf_Worst(float3_array const & point) {
	return PointInLeaf_Worst_r(0, point);
}

dleaf_t* PointInLeaf(float3_array const & point) {
	int nodenum;
	float dist;
	dnode_t* node;
	dplane_t* plane;

	nodenum = 0;
	while (nodenum >= 0) {
		node = &g_dnodes[nodenum];
		plane = &g_dplanes[node->planenum];
		dist = dot_product(point, plane->normal) - plane->dist;
		if (dist >= 0.0) {
			nodenum = node->children[0];
		} else {
			nodenum = node->children[1];
		}
	}

	return &g_dleafs[-nodenum - 1];
}

/*
 * ==============
 * PatchPlaneDist
 * Fixes up patch planes for brush models with an origin brush
 * ==============
 */
float PatchPlaneDist(patch_t const * const patch) {
	dplane_t const * plane = getPlaneFromFaceNumber(patch->faceNumber);

	return plane->dist
		+ dot_product(g_face_offset[patch->faceNumber], plane->normal);
}

void MakeBackplanes() {
	int i;

	for (i = 0; i < g_numplanes; i++) {
		backplanes[i].dist = -g_dplanes[i].dist;
		backplanes[i].normal = negate_vector(g_dplanes[i].normal);
	}
}

dplane_t const * getPlaneFromFace(dface_t const * const face) {
	if (!face) {
		Error("getPlaneFromFace() face was NULL\n");
	}

	if (face->side) {
		return &backplanes[face->planenum];
	} else {
		return &g_dplanes[face->planenum];
	}
}

dplane_t const * getPlaneFromFaceNumber(unsigned int const faceNumber) {
	dface_t* face = &g_dfaces[faceNumber];

	if (face->side) {
		return &backplanes[face->planenum];
	} else {
		return &g_dplanes[face->planenum];
	}
}

// Returns plane adjusted for face offset (for origin brushes, primarily
// used in the opaque code)
void getAdjustedPlaneFromFaceNumber(
	unsigned int faceNumber, dplane_t* plane
) {
	dface_t* face = &g_dfaces[faceNumber];
	float3_array const & face_offset = g_face_offset[faceNumber];

	plane->type = (planetype) 0;

	if (face->side) {
		float dist;

		plane->normal = backplanes[face->planenum].normal;
		dist = dot_product(plane->normal, face_offset);
		plane->dist = backplanes[face->planenum].dist + dist;
	} else {
		float dist;

		plane->normal = g_dplanes[face->planenum].normal;
		dist = dot_product(plane->normal, face_offset);
		plane->dist = g_dplanes[face->planenum].dist + dist;
	}
}

// Will modify the plane with the new dist
static void TranslatePlane(dplane_t* plane, float3_array const & delta) {
	plane->dist += dot_product(plane->normal, delta);
}

// HuntForWorld will never return contents_t::SKY or contents_t::SOLID leafs
dleaf_t* HuntForWorld(
	float3_array& point,
	float3_array const & plane_offset,
	dplane_t const * plane,
	int hunt_size,
	float hunt_scale,
	float hunt_offset
) {
	dleaf_t* leaf;

	float3_array current_point;
	float3_array original_point{ point };

	float3_array best_point{ point };
	dleaf_t* best_leaf = nullptr;
	float best_dist = 99999999.0;

	dplane_t new_plane = *plane;

	float3_array scales{ 0.0, -hunt_scale, hunt_scale };

	TranslatePlane(&new_plane, plane_offset);

	for (int a = 0; a < hunt_size; ++a) {
		for (int x = 0; x < 3; ++x) {
			current_point[0] = original_point[0] + (scales[x % 3] * a);
			for (int y = 0; y < 3; ++y) {
				current_point[1] = original_point[1] + (scales[y % 3] * a);
				for (int z = 0; z < 3; ++z) {
					if (a == 0) {
						if (x || y || z) {
							continue;
						}
					}

					current_point[2] = original_point[2]
						+ (scales[z % 3] * a);

					current_point = snap_point_to_plane(
						&new_plane, current_point, hunt_offset
					);

					float3_array const delta = vector_subtract(
						current_point, original_point
					);
					float const dist = dot_product(delta, delta);

					if (std::ranges::any_of(
							g_opaque_face_list,
							[&current_point](opaqueList_t const & of) {
								return TestPointOpaque(
									of.modelnum,
									of.origin,
									of.block,
									current_point
								);
							}
						)) {
						continue;
					}
					if (dist < best_dist) {
						if ((leaf = PointInLeaf_Worst(current_point))
							!= g_dleafs.data()) {
							if ((leaf->contents != contents_t::SKY)
								&& (leaf->contents != contents_t::SOLID)) {
								if (x || y || z) {
									// dist = best_dist;
									best_dist = dist;
									best_leaf = leaf;
									best_point = current_point;
									continue;
								}
								point = current_point;
								return leaf;
							}
						}
					}
				}
			}
		}
		if (best_leaf) {
			break;
		}
	}

	point = best_point;
	return best_leaf;
}

// ApplyMatrixOnPlane: (x y z -dist) -> (x y z -dist) * matrix
void ApplyMatrixOnPlane(
	matrix_t const & m_inverse,
	float3_array const & in_normal,
	float in_dist,
	float3_array& out_normal,
	float& out_dist
)
// out_normal is not normalized
{
	hlassume(&in_normal[0] != &out_normal[0], assume_first);
	for (std::size_t i = 0; i < 3; ++i) {
		out_normal[i] = dot_product(in_normal, m_inverse.v[i]);
	}
	out_dist = -(dot_product(in_normal, m_inverse.v[3]) - in_dist);
}

matrix_t
MultiplyMatrix(matrix_t const & m_left, matrix_t const & m_right) noexcept {
	// The following two processes are equivalent:
	// A) v_temp = apply_matrix(m1, v_in); v_out = apply_matrix(m2, v_temp);
	// B) v_temp = MultiplyMatrix(m2, m1); v_out = apply_matrix(v_temp,
	// v_in);

	matrix_t m;
	std::array<float, 4> const lastrow = { 0, 0, 0, 1 };
	hlassume(&m != &m_left && &m != &m_right, assume_first);
	for (std::size_t i = 0; i < 3; i++) {
		for (std::size_t j = 0; j < 4; j++) {
			m.v[j][i] = m_left.v[0][i] * m_right.v[j][0]
				+ m_left.v[1][i] * m_right.v[j][1]
				+ m_left.v[2][i] * m_right.v[j][2]
				+ m_left.v[3][i] * lastrow[j];
		}
	}
	return m;
}

matrix_t MatrixForScale(float3_array const & center, float scale) noexcept {
	matrix_t result{};
	for (std::size_t i = 0; i < 3; ++i) {
		result.v[i][i] = scale;
	}
	result.v[3] = vector_scale(center, 1 - scale);
	return result;
}

float CalcMatrixSign(matrix_t const & m) {
	return dot_product(cross_product(m.v[0], m.v[1]), m.v[2]);
}

void TranslateWorldToTex(int facenum, matrix_t& m)
// without g_face_offset
{
	dface_t* f;
	texinfo_t* ti;
	dplane_t const * fp;

	f = &g_dfaces[facenum];
	ti = &g_texinfo[f->texinfo];
	fp = getPlaneFromFace(f);
	for (std::size_t i = 0; i < 3; ++i) {
		m.v[i][0] = ti->vecs[0].xyz[i];
		m.v[i][1] = ti->vecs[1].xyz[i];
		m.v[i][2] = fp->normal[i];
	}
	m.v[3][0] = ti->vecs[0].offset;
	m.v[3][1] = ti->vecs[1].offset;
	m.v[3][2] = -fp->dist;
}

bool InvertMatrix(matrix_t const & m, matrix_t& m_inverse) {
	std::array<std::array<double, 4>, 2> texplanes;
	std::array<double, 4> faceplane;
	std::array<std::array<double, 3>, 2> texaxis;
	double3_array normalaxis;
	double det, sqrlen1, sqrlen2, sqrlen3;
	double3_array texorg;

	for (std::size_t i = 0; i < 4; ++i) {
		texplanes[0][i] = m.v[i][0];
		texplanes[1][i] = m.v[i][1];
		faceplane[i] = m.v[i][2];
	}

	sqrlen1 = DotProduct(texplanes[0], texplanes[0]);
	sqrlen2 = DotProduct(texplanes[1], texplanes[1]);
	sqrlen3 = DotProduct(faceplane, faceplane);
	if (sqrlen1 <= NORMAL_EPSILON * NORMAL_EPSILON
		|| sqrlen2 <= NORMAL_EPSILON * NORMAL_EPSILON
		|| sqrlen3 <= NORMAL_EPSILON * NORMAL_EPSILON)
	// s gradient, t gradient or face normal is too close to 0
	{
		return false;
	}

	CrossProduct(texplanes[0], texplanes[1], normalaxis);
	det = DotProduct(normalaxis, faceplane);
	if (det * det
		<= sqrlen1 * sqrlen2 * sqrlen3 * NORMAL_EPSILON * NORMAL_EPSILON)
	// s gradient, t gradient and face normal are coplanar
	{
		return false;
	}
	normalaxis = vector_scale(normalaxis, 1 / det);

	CrossProduct(texplanes[1], faceplane, texaxis[0]);
	texaxis[0] = vector_scale(texaxis[0], 1 / det);

	CrossProduct(faceplane, texplanes[0], texaxis[1]);
	texaxis[1] = vector_scale(texaxis[1], 1 / det);

	texorg = vector_scale(normalaxis, -faceplane[3]);
	texorg = vector_fma(texaxis[0], -texplanes[0][3], texorg);
	texorg = vector_fma(texaxis[1], -texplanes[1][3], texorg);

	m_inverse.v[0] = to_float3(texaxis[0]);
	m_inverse.v[1] = to_float3(texaxis[1]);
	m_inverse.v[2] = to_float3(normalaxis);
	m_inverse.v[3] = to_float3(texorg);
	return true;
}

struct position_t final {
	bool valid;
	bool nudged;
	float best_s; // FindNearestPosition will return this value
	float best_t;
	float3_array pos; // with DEFAULT_HUNT_OFFSET
};

// Size of potision_t (21) * positions per sample (9) * max number of
// samples (max AllocBlock (64) * 128 * 128)
//   = 200MB of RAM
// But they are freed before BuildVisLeafs, so it's not a problem.

struct positionmap_t final {
	bool valid;
	int facenum;
	int w; // Number of s
	int h; // Number of t
	fast_winding* facewinding;
	fast_winding* facewindingwithoffset;
	fast_winding* texwinding;
	position_t* grid; // [h][w]
	dplane_t faceplane;
	dplane_t faceplanewithoffset;
	dplane_t texplane; // (0, 0, 1, 0) or (0, 0, -1, 0)
	matrix_t worldtotex;
	matrix_t textoworld;
	float3_array face_offset;
	float3_array face_centroid;
	float3_array texcentroid;
	float3_array start; // s_start, t_start, 0
	float3_array step;	// s_step, t_step, 0
};

static positionmap_t g_face_positions[MAX_MAP_FACES];

static bool IsPositionValid(
	positionmap_t* map,
	float3_array const & pos_st,
	float3_array& pos_out,
	bool usephongnormal = true,
	bool doedgetest = true,
	int hunt_size = 2,
	float hunt_scale = 0.2
) {
	float3_array pos;
	float3_array pos_normal;
	float hunt_offset;

	pos = apply_matrix(map->textoworld, pos_st);
	VectorAdd(pos, map->face_offset, pos);
	if (usephongnormal) {
		GetPhongNormal(map->facenum, pos, pos_normal);
	} else {
		pos_normal = map->faceplanewithoffset.normal;
	}
	pos = vector_fma(pos_normal, DEFAULT_HUNT_OFFSET, pos);

	hunt_offset = dot_product(pos, map->faceplanewithoffset.normal)
		- map->faceplanewithoffset
			  .dist; // might be smaller than DEFAULT_HUNT_OFFSET

	// push the point 0.2 units around to avoid walls
	if (!HuntForWorld(
			pos,
			float3_array{},
			&map->faceplanewithoffset,
			hunt_size,
			hunt_scale,
			hunt_offset
		)) {
		return false;
	}

	if (doedgetest
		&& !point_in_winding_noedge(
			*map->facewindingwithoffset,
			map->faceplanewithoffset,
			pos,
			DEFAULT_EDGE_WIDTH
		)) {
		// if the sample has gone beyond face boundaries, be careful that it
		// hasn't passed a wall
		float3_array test;
		float3_array transparency;
		int opaquestyle;

		test = pos;
		snap_to_winding_noedge(
			*map->facewindingwithoffset,
			map->faceplanewithoffset,
			test,
			DEFAULT_EDGE_WIDTH,
			4 * DEFAULT_EDGE_WIDTH
		);

		if (!HuntForWorld(
				test,
				float3_array{},
				&map->faceplanewithoffset,
				hunt_size,
				hunt_scale,
				hunt_offset
			)) {
			return false;
		}

		if (TestLine(pos, test) != contents_t::EMPTY) {
			return false;
		}

		if (TestSegmentAgainstOpaqueList(
				pos, test, transparency, opaquestyle
			) == true
			|| opaquestyle != -1) {
			return false;
		}
	}

	pos_out = pos;
	return true;
}

static void CalcSinglePosition(positionmap_t* map, int is, int it) {
	position_t& p = map->grid[is + map->w * it];
	float const smin = map->start[0] + is * map->step[0];
	float const smax = map->start[0] + (is + 1) * map->step[0];
	float const tmin = map->start[1] + it * map->step[1];
	float const tmax = map->start[1] + (it + 1) * map->step[1];

	std::array<dplane_t, 4> clipplanes{};
	constexpr float3_array v_s = { 1, 0, 0 };
	constexpr float3_array v_t = { 0, 1, 0 };
	clipplanes[0].normal = v_s;
	clipplanes[0].dist = smin;
	clipplanes[1].normal = negate_vector(v_s);
	clipplanes[1].dist = -smax;
	clipplanes[2].normal = v_t;
	clipplanes[2].dist = tmin;
	clipplanes[3].normal = negate_vector(v_t);
	clipplanes[3].dist = -tmax;

	p.nudged = true; // it's nudged unless it can get its position directly
					 // from its s,t
	fast_winding zone{ *map->texwinding };
	for (int x = 0; x < 4 && zone.size() > 0; x++) {
		zone.mutating_clip(clipplanes[x].normal, clipplanes[x].dist, false);
	}
	if (zone.size() == 0) {
		p.valid = false;
		return;
	}

	float3_array original_st;

	original_st[0] = map->start[0] + (is + 0.5) * map->step[0];
	original_st[1] = map->start[1] + (it + 0.5) * map->step[1];
	original_st[2] = 0.0;

	float3_array test_st;

	test_st = original_st;
	snap_to_winding(zone, map->texplane, test_st);

	if (IsPositionValid(map, test_st, p.pos)) {
		p.nudged = false;
		p.best_s = test_st[0];
		p.best_t = test_st[1];
		p.valid = true;
		return;
	}

	test_st = zone.getCenter();
	if (IsPositionValid(map, test_st, p.pos)) {
		p.best_s = test_st[0];
		p.best_t = test_st[1];
		p.valid = true;
		return;
	}

	if (g_fastmode) {
		p.valid = false;
		return;
	}

	constexpr std::size_t numNudges = 12;
	std::array<float3_array, 12> const nudgeList{
		float3_array{ 0.1, 0, 0 },	   float3_array{ -0.1, 0, 0 },
		float3_array{ 0, 0.1, 0 },	   float3_array{ 0, -0.1, 0 },
		float3_array{ 0.3, 0, 0 },	   float3_array{ -0.3, 0, 0 },
		float3_array{ 0, 0.3, 0 },	   float3_array{ 0, -0.3, 0 },
		float3_array{ 0.3, 0.3, 0 },   float3_array{ -0.3, 0.3, 0 },
		float3_array{ -0.3, -0.3, 0 }, float3_array{ 0.3, -0.3, 0 }
	};

	for (float3_array const & nudge : nudgeList) {
		test_st = vector_fma(nudge, map->step, original_st);
		snap_to_winding(zone, map->texplane, test_st);

		if (IsPositionValid(map, test_st, p.pos)) {
			p.best_s = test_st[0];
			p.best_t = test_st[1];
			p.valid = true;
			return;
		}
	}
	p.valid = false;
}

void FindFacePositions(int facenum)
// this function must be called after g_face_offset and g_face_centroids and
// g_edgeshare have been calculated
{
	dface_t* f;
	positionmap_t* map;
	texinfo_t* ti;
	float3_array const v_up{ 0, 0, 1 };
	float density;
	int imins[2], imaxs[2];
	int is, it;
	int x;

	f = &g_dfaces[facenum];
	map = &g_face_positions[facenum];
	map->valid = true;
	map->facenum = facenum;
	map->facewinding = nullptr;
	map->facewindingwithoffset = nullptr;
	map->texwinding = nullptr;
	map->grid = nullptr;

	ti = &g_texinfo[f->texinfo];
	if (ti->has_special_flag()) {
		map->valid = false;
		return;
	}

	map->face_offset = g_face_offset[facenum];
	map->face_centroid = g_face_centroids[facenum];
	TranslateWorldToTex(facenum, map->worldtotex);
	if (!InvertMatrix(map->worldtotex, map->textoworld)) {
		map->valid = false;
		return;
	}

	map->facewinding = new fast_winding(*f);
	map->faceplane = *getPlaneFromFace(f);
	map->facewindingwithoffset = new fast_winding(*map->facewinding);
	map->facewindingwithoffset->add_offset_to_points(map->face_offset);
	map->faceplanewithoffset = map->faceplane;
	map->faceplanewithoffset.dist = map->faceplane.dist
		+ dot_product(map->face_offset, map->faceplane.normal);

	map->texwinding = new fast_winding();
	map->texwinding->reserve_point_storage(map->facewinding->size());
	for (float3_array const & fwp : map->facewinding->points()) {
		float3_array point{ apply_matrix(map->worldtotex, fwp) };
		point[2] = 0;
		map->texwinding->push_point(point);
	}
	map->texwinding->RemoveColinearPoints();
	map->texplane.normal = v_up;
	if (CalcMatrixSign(map->worldtotex) < 0.0) {
		map->texplane.normal[2] *= -1;
	}
	map->texplane.dist = 0.0;
	if (map->texwinding->size() == 0) {
		delete map->facewinding;
		map->facewinding = nullptr;
		delete map->facewindingwithoffset;
		map->facewindingwithoffset = nullptr;
		delete map->texwinding;
		map->texwinding = nullptr;
		map->valid = false;
		return;
	}
	map->texcentroid = apply_matrix(
		map->worldtotex,
		vector_subtract(map->face_centroid, map->face_offset)
	);
	map->texcentroid[2] = 0.0;

	std::array<float, 2> texmins;
	std::array<float, 2> texmaxs;

	texmins[0] = texmaxs[0] = map->texwinding->point(0)[0];
	texmins[1] = texmaxs[1] = map->texwinding->point(0)[1];

	for (float3_array const & twp : map->texwinding->points()) {
		for (std::size_t k = 0; k < 2; ++k) {
			if (twp[k] < texmins[k]) {
				texmins[k] = twp[k];
			}
			if (twp[k] > texmaxs[k]) {
				texmaxs[k] = twp[k];
			}
		}
	}
	density = 3.0;
	if (g_fastmode) {
		density = 1.0;
	}
	map->step[0] = (float) TEXTURE_STEP / density;
	map->step[1] = (float) TEXTURE_STEP / density;
	map->step[2] = 1.0;
	int k;
	for (k = 0; k < 2; k++) {
		imins[k] = (int
		) floor(texmins[k] / map->step[k] + 0.5 - ON_EPSILON);
		imaxs[k] = (int) ceil(texmaxs[k] / map->step[k] - 0.5 + ON_EPSILON);
	}
	map->start[0] = (imins[0] - 0.5) * map->step[0];
	map->start[1] = (imins[1] - 0.5) * map->step[1];
	map->start[2] = 0.0;
	map->w = imaxs[0] - imins[0] + 1;
	map->h = imaxs[1] - imins[1] + 1;
	if (map->w <= 0 || map->h <= 0
		|| (double) map->w * (double) map->h > 99'999'999) {
		delete map->facewinding;
		map->facewinding = nullptr;
		delete map->facewindingwithoffset;
		map->facewindingwithoffset = nullptr;
		delete map->texwinding;
		map->texwinding = nullptr;
		map->valid = false;
		return;
	}

	map->grid = (position_t*) malloc(map->w * map->h * sizeof(position_t));
	hlassume(map->grid != nullptr, assume_NoMemory);

	for (it = 0; it < map->h; it++) {
		for (is = 0; is < map->w; is++) {
			CalcSinglePosition(map, is, it);
		}
	}

	return;
}

void FreePositionMaps() {
	if (g_drawsample) {
		char name[_MAX_PATH + 20];
		snprintf(name, sizeof(name), "%s_positions.pts", g_Mapname);
		Log("Writing '%s' ...\n", name);
		FILE* f;
		f = fopen(name, "w");
		if (f) {
			float3_array v, dist;
			for (std::size_t i = 0; i < g_numfaces; ++i) {
				positionmap_t* map = &g_face_positions[i];
				if (!map->valid) {
					continue;
				}
				for (std::int32_t j = 0; j < map->h * map->w; ++j) {
					if (!map->grid[j].valid) {
						continue;
					}
					v = map->grid[j].pos;
					VectorSubtract(v, g_drawsample_origin, dist);
					if (dot_product(dist, dist)
						< g_drawsample_radius * g_drawsample_radius) {
						for (float3_array const & p : pos) {
							fprintf(
								f,
								"%g %g %g\n",
								v[0] + p[0],
								v[1] + p[1],
								v[2] + p[2]
							);
						}
					}
				}
			}
			fclose(f);
			Log("OK.\n");
		} else {
			Log("Error.\n");
		}
	}
	for (int facenum = 0; facenum < g_numfaces; facenum++) {
		positionmap_t* map = &g_face_positions[facenum];
		if (map->valid) {
			delete map->facewinding;
			map->facewinding = nullptr;
			delete map->facewindingwithoffset;
			map->facewindingwithoffset = nullptr;
			delete map->texwinding;
			map->texwinding = nullptr;
			free(map->grid);
			map->grid = nullptr;
			map->valid = false;
		}
	}
}

bool FindNearestPosition(
	int facenum,
	fast_winding const * texwinding,
	dplane_t const & texplane,
	float s,
	float t,
	float3_array& pos,
	float* best_s,
	float* best_t,
	float* dist,
	bool* nudged
) {
	int itmin, itmax, ismin, ismax;
	int is;
	int it;
	bool found;
	int best_is;
	int best_it;
	float best_dist;

	positionmap_t const * map = &g_face_positions[facenum];
	if (!map->valid) {
		return false;
	}

	float3_array const original_st = { s, t, 0.0 };

	if (point_in_winding(
			*map->texwinding, map->texplane, original_st, 4 * ON_EPSILON
		)) {
		itmin = (int) ceil(
					(original_st[1] - map->start[1] - 2 * ON_EPSILON)
					/ map->step[1]
				)
			- 1;
		itmax = (int) floor(
			(original_st[1] - map->start[1] + 2 * ON_EPSILON) / map->step[1]
		);
		ismin = (int) ceil(
					(original_st[0] - map->start[0] - 2 * ON_EPSILON)
					/ map->step[0]
				)
			- 1;
		ismax = (int) floor(
			(original_st[0] - map->start[0] + 2 * ON_EPSILON) / map->step[0]
		);
		itmin = std::max(0, itmin);
		itmax = std::min(itmax, map->h - 1);
		ismin = std::max(0, ismin);
		ismax = std::min(ismax, map->w - 1);

		found = false;
		bool best_nudged = true;
		for (it = itmin; it <= itmax; it++) {
			for (is = ismin; is <= ismax; is++) {
				position_t const * p = &map->grid[is + map->w * it];
				if (!p->valid) {
					continue;
				}
				float3_array const current_st{ p->best_s, p->best_t, 0 };

				float3_array v;
				VectorSubtract(current_st, original_st, v);
				float const d = vector_length(v);

				if (!found || !p->nudged && best_nudged
					|| p->nudged == best_nudged
						&& d < best_dist - 2 * ON_EPSILON) {
					found = true;
					best_is = is;
					best_it = it;
					best_dist = d;
					best_nudged = p->nudged;
				}
			}
		}

		if (found) {
			position_t* p;

			p = &map->grid[best_is + map->w * best_it];
			pos = p->pos;
			*best_s = p->best_s;
			*best_t = p->best_t;
			*dist = 0.0;
			*nudged = p->nudged;
			return true;
		}
	}
	*nudged = true;

	itmin = map->h;
	itmax = -1;
	ismin = map->w;
	ismax = -1;
	for (float3_array const & twPoint : texwinding->points()) {
		it = (int) floor(
			(twPoint[1] - map->start[1] + 0.5 * ON_EPSILON) / map->step[1]
		);
		itmin = std::min(itmin, it);
		it = (int) ceil(
				 (twPoint[1] - map->start[1] - 0.5 * ON_EPSILON)
				 / map->step[1]
			 )
			- 1;
		itmax = std::max(it, itmax);
		is = (int) floor(
			(twPoint[0] - map->start[0] + 0.5 * ON_EPSILON) / map->step[0]
		);
		ismin = std::min(ismin, is);
		is = (int) ceil(
				 (twPoint[0] - map->start[0] - 0.5 * ON_EPSILON)
				 / map->step[0]
			 )
			- 1;
		ismax = std::max(is, ismax);
	}
	itmin = std::max(0, itmin);
	itmax = std::min(itmax, map->h - 1);
	ismin = std::max(0, ismin);
	ismax = std::min(ismax, map->w - 1);

	found = false;
	for (it = itmin; it <= itmax; it++) {
		for (is = ismin; is <= ismax; is++) {
			position_t const * p = &map->grid[is + map->w * it];
			if (!p->valid) {
				continue;
			}
			float3_array const current_st{ p->best_s, p->best_t, 0 };
			float3_array v;
			VectorSubtract(current_st, original_st, v);
			float const d = vector_length(v);

			if (!found || d < best_dist - ON_EPSILON) {
				found = true;
				best_is = is;
				best_it = it;
				best_dist = d;
			}
		}
	}

	if (found) {
		position_t const * p = &map->grid[best_is + map->w * best_it];
		pos = p->pos;
		*best_s = p->best_s;
		*best_t = p->best_t;
		*dist = best_dist;
		return true;
	}

	return false;
}
