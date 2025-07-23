#include "bspfile.h"
#include "cmdlib.h"
#include "hlrad.h"
#include "log.h" //--vluzacn
#include "mathlib.h"
#include "winding.h"

struct tnode_t final {
	planetype type;
	float3_array normal;
	float dist;
	int children[2];
};

static tnode_t* tnodes;
static tnode_t* tnode_p;

/*
 * ==============
 * MakeTnode
 *
 * Converts the disk node structure into the efficient tracing structure
 * ==============
 */
static void MakeTnode(int const nodenum) {
	tnode_t* t;
	int i;
	dnode_t* node;

	t = tnode_p++;

	node = g_dnodes.data() + nodenum;
	dplane_t const & plane = g_dplanes[node->planenum];

	t->type = plane.type;
	t->normal = plane.normal;
	if (plane.normal[std::size_t(plane.type) % 3] < 0) {
		if (plane.type <= last_axial) {
			Warning("MakeTnode: negative plane");
		} else {
			Developer(
				developer_level::message,
				"Warning: MakeTnode: negative plane\n"
			);
		}
	}
	t->dist = plane.dist;

	for (i = 0; i < 2; i++) {
		if (node->children[i] < 0) {
			t->children[i] = std::to_underlying(
				g_dleafs[-node->children[i] - 1].contents
			);
		} else {
			t->children[i] = tnode_p - tnodes;
			MakeTnode(node->children[i]);
		}
	}
}

/*
 * =============
 * MakeTnodes
 *
 * Loads the node structure out of a .bsp file to be used for light
 * occlusion
 * =============
 */
void MakeTnodes() {
	tnodes = (tnode_t*) calloc((g_numnodes), sizeof(tnode_t));
	tnode_p = tnodes;
	MakeTnode(0);
}

//==========================================================

static contents_t TestLine_r(
	int const node,
	float3_array const & start,
	float3_array const & stop,
	float3_array& skyhit
) {
	tnode_t* tnode;
	float front, back;
	float3_array mid;
	float frac;
	int side;
	contents_t r;

	if (node < 0) {
		if (node != 0) {
			contents_t nodeContents{ node };
			if (nodeContents == contents_t::SOLID) {
				return contents_t::SOLID;
			}
			if (nodeContents == contents_t::SKY) {
				skyhit = start;
				return contents_t::SKY;
			}
		}
		return contents_t::EMPTY;
	}

	tnode = &tnodes[node];
	switch (tnode->type) {
		case planetype::plane_x:
			front = start[0] - tnode->dist;
			back = stop[0] - tnode->dist;
			break;
		case planetype::plane_y:
			front = start[1] - tnode->dist;
			back = stop[1] - tnode->dist;
			break;
		case planetype::plane_z:
			front = start[2] - tnode->dist;
			back = stop[2] - tnode->dist;
			break;
		default:
			front = (start[0] * tnode->normal[0]
					 + start[1] * tnode->normal[1]
					 + start[2] * tnode->normal[2])
				- tnode->dist;
			back = (stop[0] * tnode->normal[0] + stop[1] * tnode->normal[1]
					+ stop[2] * tnode->normal[2])
				- tnode->dist;
			break;
	}

	if (front > ON_EPSILON / 2 && back > ON_EPSILON / 2) {
		return TestLine_r(tnode->children[0], start, stop, skyhit);
	}
	if (front < -ON_EPSILON / 2 && back < -ON_EPSILON / 2) {
		return TestLine_r(tnode->children[1], start, stop, skyhit);
	}
	if (fabs(front) <= ON_EPSILON && fabs(back) <= ON_EPSILON) {
		contents_t r1 = TestLine_r(tnode->children[0], start, stop, skyhit);
		if (r1 == contents_t::SOLID) {
			return contents_t::SOLID;
		}
		contents_t r2 = TestLine_r(tnode->children[1], start, stop, skyhit);
		if (r2 == contents_t::SOLID) {
			return contents_t::SOLID;
		}
		if (r1 == contents_t::SKY || r2 == contents_t::SKY) {
			return contents_t::SKY;
		}
		return contents_t::EMPTY;
	}
	side = (front - back) < 0;
	frac = front / (front - back);
	if (frac < 0) {
		frac = 0;
	}
	if (frac > 1) {
		frac = 1;
	}
	mid[0] = start[0] + (stop[0] - start[0]) * frac;
	mid[1] = start[1] + (stop[1] - start[1]) * frac;
	mid[2] = start[2] + (stop[2] - start[2]) * frac;
	r = TestLine_r(tnode->children[side], start, mid, skyhit);
	if (r != contents_t::EMPTY) {
		return r;
	}
	return TestLine_r(tnode->children[!side], mid, stop, skyhit);
}

contents_t TestLine(
	float3_array const & start,
	float3_array const & stop,
	float3_array& skyhit
) {
	return TestLine_r(0, start, stop, skyhit);
}

struct opaqueface_t final {
	fast_winding* winding;
	dplane_t plane;
	dplane_t* edges;
	std::array<tex_vec, 2> texVecs;
	std::uint32_t numedges;
	std::int32_t texinfo;
	std::uint32_t gTexturesIndex;
	bool tex_alphatest;
};

struct opaquenode_t final {
	planetype type;
	float3_array normal;
	float dist;
	int children[2];
	int firstface;
	int numfaces;
};

static std::unique_ptr<opaquemodel_t[]> opaquemodels;
static std::unique_ptr<opaquenode_t[]> opaquenodes;
static std::unique_ptr<opaqueface_t[]> opaquefaces;

struct try_merge_points final {
	float3_array const & pA;
	float3_array const & pB;
	float3_array const & pD;
	float3_array const & p2A;
	float3_array const & p2B;
	float3_array const & p2D;

	int i;
	int i2;
};

std::optional<try_merge_points> find_points_for_try_merge(
	opaqueface_t const & f, opaqueface_t const & f2
) noexcept {
	fast_winding const & w = *f.winding;
	fast_winding const & w2 = *f2.winding;
	for (int i = 0; i < w.size(); i++) {
		float3_array const & pA = w.point_before(i, 1);
		float3_array const & pB = w.point(i);
		float3_array const & pC = w.point_after(i, 1);
		float3_array const & pD = w.point_after(i, 2);
		for (int i2 = 0; i2 < w2.size(); i2++) {
			float3_array const & p2A = w2.point_before(i2, 1);
			float3_array const & p2B = w2.point(i2);
			float3_array const & p2C = w2.point_after(i2, 1);
			float3_array const & p2D = w2.point_after(i2, 2);
			if (vectors_almost_same(pB, p2C)
				&& vectors_almost_same(pC, p2B)) {
				return try_merge_points{ .pA = pA,
										 .pB = pB,
										 .pD = pD,
										 .p2A = p2A,
										 .p2B = p2B,
										 .p2D = p2D,
										 .i = i,
										 .i2 = i2 };
			}
		}
	}
	return std::nullopt;
}

bool TryMerge(opaqueface_t* f, opaqueface_t const * f2) {
	if (!f->winding || !f2->winding) {
		return false;
	}
	if (fabs(f2->plane.dist - f->plane.dist) > ON_EPSILON
		|| fabs(f2->plane.normal[0] - f->plane.normal[0]) > NORMAL_EPSILON
		|| fabs(f2->plane.normal[1] - f->plane.normal[1]) > NORMAL_EPSILON
		|| fabs(f2->plane.normal[2] - f->plane.normal[2])
			> NORMAL_EPSILON) {
		return false;
	}
	if ((f->tex_alphatest || f2->tex_alphatest)
		&& f->texinfo != f2->texinfo) {
		return false;
	}

	fast_winding const & w = *f->winding;
	fast_winding const & w2 = *f2->winding;

	float3_array const & normal = f->plane.normal;
	dplane_t pl1, pl2;

	std::optional<try_merge_points> maybePoints = find_points_for_try_merge(
		*f, *f2
	);
	if (!maybePoints) {
		return false;
	}
	try_merge_points const & points = maybePoints.value();

	float3_array const e1 = vector_subtract(points.p2D, points.pA);
	pl1.normal = cross_product(normal, e1); // Pointing outward
	if (normalize_vector(pl1.normal) == 0.0) {
		Developer(
			developer_level::warning, "Warning: TryMerge: Empty edge.\n"
		);
		return false;
	}
	pl1.dist = dot_product(points.pA, pl1.normal);
	if (dot_product(points.pB, pl1.normal) - pl1.dist < -ON_EPSILON) {
		return false;
	}
	bool const side1 = dot_product(points.pB, pl1.normal) - pl1.dist
		> ON_EPSILON;

	float3_array const e2 = vector_subtract(points.pD, points.p2A);
	pl2.normal = cross_product(normal, e2); // Pointing outward
	if (normalize_vector(pl2.normal) == 0.0) {
		Developer(
			developer_level::warning, "Warning: TryMerge: Empty edge.\n"
		);
		return false;
	}
	pl2.dist = dot_product(points.p2A, pl2.normal);
	if (dot_product(points.p2B, pl2.normal) - pl2.dist < -ON_EPSILON) {
		return false;
	}
	bool const side2 = dot_product(points.p2B, pl2.normal) - pl2.dist
		> ON_EPSILON;

	fast_winding neww;
	neww.reserve_point_storage(w.size() + w2.size() - 4 + side1 + side2);
	int j;
	for (j = (points.i + 2) % w.size(); j != points.i;
		 j = (j + 1) % w.size()) {
		neww.push_point(w.point(j));
	}
	if (side1) {
		neww.push_point(w.point(j));
	}
	for (j = (points.i2 + 2) % w2.size(); j != points.i2;
		 j = (j + 1) % w2.size()) {
		neww.push_point(w2.point(j));
	}
	if (side2) {
		neww.push_point(w2.point(j));
	}
	neww.RemoveColinearPoints();
	if (neww.size() < 3) { // This should probably be a fatal error
		Developer(
			developer_level::warning, "Warning: TryMerge: Empty winding.\n"
		);
		delete f->winding;
		f->winding = nullptr;
	} else {
		*f->winding = std::move(neww);
	}
	return true;
}

int MergeOpaqueFaces(int firstface, int numfaces) {
	int i, j, newnum;
	opaqueface_t* faces = &opaquefaces[firstface];
	for (i = 0; i < numfaces; i++) {
		for (j = 0; j < i; j++) {
			if (TryMerge(&faces[i], &faces[j])) {
				delete faces[j].winding;
				faces[j].winding = nullptr;
				j = -1;
				continue;
			}
		}
	}
	for (i = 0, j = 0; i < numfaces; i++) {
		if (faces[i].winding) {
			faces[j] = faces[i];
			j++;
		}
	}
	newnum = j;
	for (; j < numfaces; j++) {
		faces[j] = opaqueface_t{};
	}
	return newnum;
}

void BuildFaceEdges(opaqueface_t* f) {
	if (!f->winding) {
		return;
	}
	f->numedges = f->winding->size();
	f->edges = (dplane_t*) calloc(f->numedges, sizeof(dplane_t));

	float3_array const & n = f->plane.normal;
	for (std::size_t x = 0; x < f->winding->size(); ++x) {
		float3_array const & p1 = f->winding->point(x);
		float3_array const & p2 = f->winding->point(
			(x + 1) % f->winding->size()
		);
		dplane_t* pl = &f->edges[x];
		pl->normal = cross_product(n, vector_subtract(p2, p1));
		if (normalize_vector(pl->normal) == 0.0) {
			Developer(
				developer_level::warning,
				"Warning: BuildFaceEdges: Empty edge.\n"
			);
			pl->normal = {};
			pl->dist = -1;
			continue;
		}
		pl->dist = dot_product(pl->normal, p1);
	}
}

void CreateOpaqueNodes() {
	opaquemodels = std::make_unique<opaquemodel_t[]>(g_nummodels);
	opaquenodes = std::make_unique<opaquenode_t[]>(g_numnodes);
	opaquefaces = std::make_unique<opaqueface_t[]>(g_numfaces);
	for (std::size_t i = 0; i < g_numfaces; ++i) {
		opaqueface_t* of = &opaquefaces[i];
		dface_t* df = &g_dfaces[i];
		of->winding = new fast_winding(*df);
		if (of->winding->size() < 3) {
			delete of->winding;
			of->winding = nullptr;
		}
		of->plane = g_dplanes[df->planenum];
		if (df->side) {
			of->plane.normal = negate_vector(of->plane.normal);
			of->plane.dist = -of->plane.dist;
		}
		of->texinfo = df->texinfo;
		texinfo_t* info = &g_texinfo[of->texinfo];
		of->texVecs = info->vecs;
		radtexture_t* tex = &g_textures[info->miptex];
		of->tex_alphatest = tex->name.is_transparent_or_decal();
		of->gTexturesIndex = info->miptex;
	}
	for (std::size_t i = 0; i < g_numnodes; ++i) {
		opaquenode_t* on = &opaquenodes[i];
		dnode_t* dn = &g_dnodes[i];
		on->type = g_dplanes[dn->planenum].type;
		on->normal = g_dplanes[dn->planenum].normal;
		on->dist = g_dplanes[dn->planenum].dist;
		on->children[0] = dn->children[0];
		on->children[1] = dn->children[1];
		on->firstface = dn->firstface;
		on->numfaces = dn->numfaces;
		on->numfaces = MergeOpaqueFaces(on->firstface, on->numfaces);
	}
	for (std::size_t i = 0; i < g_numfaces; ++i) {
		BuildFaceEdges(&opaquefaces[i]);
	}
	for (std::size_t i = 0; i < g_nummodels; ++i) {
		opaquemodel_t* om = &opaquemodels[i];
		dmodel_t* dm = &g_dmodels[i];
		om->headnode = dm->headnode[0];
		for (std::size_t j = 0; j < 3; ++j) {
			om->mins[j] = dm->mins[j] - 1;
			om->maxs[j] = dm->maxs[j] + 1;
		}
	}
}

void DeleteOpaqueNodes() {
	int i;
	for (i = 0; i < g_numfaces; i++) {
		opaqueface_t* of = &opaquefaces[i];
		if (of->winding) {
			delete of->winding;
		}
		if (of->edges) {
			free(of->edges);
		}
	}
	opaquefaces.reset();
	opaquenodes.reset();
	opaquemodels.reset();
}

static bool TestLineOpaque_face(int facenum, float3_array const & hit) {
	opaqueface_t* thisface = &opaquefaces[facenum];
	if (thisface->numedges == 0) {
		Developer(
			developer_level::warning,
			"Warning: TestLineOpaque: Empty face.\n"
		);
		return false;
	}
	for (std::size_t x = 0; x < thisface->numedges; ++x) {
		if (dot_product(hit, thisface->edges[x].normal)
				- thisface->edges[x].dist
			> ON_EPSILON) {
			return false;
		}
	}
	if (thisface->tex_alphatest) {
		radtexture_t const & tex = g_textures[thisface->gTexturesIndex];
		float x = dot_product(hit, thisface->texVecs[0].xyz)
			+ thisface->texVecs[0].offset;
		float y = dot_product(hit, thisface->texVecs[1].xyz)
			+ thisface->texVecs[1].offset;
		x = floor(x - tex.width * floor(x / tex.width));
		y = floor(y - tex.height * floor(y / tex.height));
		x = x > tex.width - 1 ? tex.width - 1 : x < 0 ? 0 : x;
		y = y > tex.height - 1 ? tex.height - 1 : y < 0 ? 0 : y;
		if (tex.canvas[(int) y * tex.width + (int) x] == 0xFF) {
			return false;
		}
	}
	return true;
}

static int TestLineOpaque_r(
	int nodenum, float3_array const & start, float3_array const & stop
) {
	opaquenode_t* thisnode;
	float front, back;
	if (nodenum < 0) {
		return 0;
	}
	thisnode = &opaquenodes[nodenum];
	switch (thisnode->type) {
		case planetype::plane_x:
			front = start[0] - thisnode->dist;
			back = stop[0] - thisnode->dist;
			break;
		case planetype::plane_y:
			front = start[1] - thisnode->dist;
			back = stop[1] - thisnode->dist;
			break;
		case planetype::plane_z:
			front = start[2] - thisnode->dist;
			back = stop[2] - thisnode->dist;
			break;
		default:
			front = dot_product(start, thisnode->normal) - thisnode->dist;
			back = dot_product(stop, thisnode->normal) - thisnode->dist;
	}
	if (front > ON_EPSILON / 2 && back > ON_EPSILON / 2) {
		return TestLineOpaque_r(thisnode->children[0], start, stop);
	}
	if (front < -ON_EPSILON / 2 && back < -ON_EPSILON / 2) {
		return TestLineOpaque_r(thisnode->children[1], start, stop);
	}
	if (fabs(front) <= ON_EPSILON && fabs(back) <= ON_EPSILON) {
		return TestLineOpaque_r(thisnode->children[0], start, stop)
			|| TestLineOpaque_r(thisnode->children[1], start, stop);
	}
	{
		int side;
		float frac;
		float3_array mid;
		int facenum;
		side = (front - back) < 0;
		frac = front / (front - back);
		if (frac < 0) {
			frac = 0;
		}
		if (frac > 1) {
			frac = 1;
		}
		mid[0] = start[0] + (stop[0] - start[0]) * frac;
		mid[1] = start[1] + (stop[1] - start[1]) * frac;
		mid[2] = start[2] + (stop[2] - start[2]) * frac;
		for (facenum = thisnode->firstface;
			 facenum < thisnode->firstface + thisnode->numfaces;
			 facenum++) {
			if (TestLineOpaque_face(facenum, mid)) {
				return 1;
			}
		}
		return TestLineOpaque_r(thisnode->children[side], start, mid)
			|| TestLineOpaque_r(thisnode->children[!side], mid, stop);
	}
}

int TestLineOpaque(
	int modelnum,
	float3_array const & modelorigin,
	float3_array const & start,
	float3_array const & stop
) {
	opaquemodel_t* thismodel = &opaquemodels[modelnum];
	float front, back, frac;
	float3_array p1 = vector_subtract(start, modelorigin);
	float3_array p2 = vector_subtract(stop, modelorigin);
	int axial;
	for (axial = 0; axial < 3; axial++) {
		front = p1[axial] - thismodel->maxs[axial];
		back = p2[axial] - thismodel->maxs[axial];
		if (front >= -ON_EPSILON && back >= -ON_EPSILON) {
			return 0;
		}
		if (front > ON_EPSILON || back > ON_EPSILON) {
			frac = front / (front - back);
			if (front > back) {
				p1[0] = p1[0] + (p2[0] - p1[0]) * frac;
				p1[1] = p1[1] + (p2[1] - p1[1]) * frac;
				p1[2] = p1[2] + (p2[2] - p1[2]) * frac;
			} else {
				p2[0] = p1[0] + (p2[0] - p1[0]) * frac;
				p2[1] = p1[1] + (p2[1] - p1[1]) * frac;
				p2[2] = p1[2] + (p2[2] - p1[2]) * frac;
			}
		}
		front = thismodel->mins[axial] - p1[axial];
		back = thismodel->mins[axial] - p2[axial];
		if (front >= -ON_EPSILON && back >= -ON_EPSILON) {
			return 0;
		}
		if (front > ON_EPSILON || back > ON_EPSILON) {
			frac = front / (front - back);
			if (front > back) {
				p1[0] = p1[0] + (p2[0] - p1[0]) * frac;
				p1[1] = p1[1] + (p2[1] - p1[1]) * frac;
				p1[2] = p1[2] + (p2[2] - p1[2]) * frac;
			} else {
				p2[0] = p1[0] + (p2[0] - p1[0]) * frac;
				p2[1] = p1[1] + (p2[1] - p1[1]) * frac;
				p2[2] = p1[2] + (p2[2] - p1[2]) * frac;
			}
		}
	}
	return TestLineOpaque_r(thismodel->headnode, p1, p2);
}

int CountOpaqueFaces_r(opaquenode_t* node) {
	int count;
	count = node->numfaces;
	if (node->children[0] >= 0) {
		count += CountOpaqueFaces_r(&opaquenodes[node->children[0]]);
	}
	if (node->children[1] >= 0) {
		count += CountOpaqueFaces_r(&opaquenodes[node->children[1]]);
	}
	return count;
}

int CountOpaqueFaces(int modelnum) {
	return CountOpaqueFaces_r(&opaquenodes[opaquemodels[modelnum].headnode]
	);
}

static bool
TestPointOpaque_r(int nodenum, bool solid, float3_array const & point) {
	opaquenode_t* thisnode;
	float dist;
	while (1) {
		if (nodenum < 0) {
			if (solid
				&& g_dleafs[-nodenum - 1].contents == contents_t::SOLID) {
				return true;
			} else {
				return false;
			}
		}
		thisnode = &opaquenodes[nodenum];
		switch (thisnode->type) {
			case planetype::plane_x:
				dist = point[0] - thisnode->dist;
				break;
			case planetype::plane_y:
				dist = point[1] - thisnode->dist;
				break;
			case planetype::plane_z:
				dist = point[2] - thisnode->dist;
				break;
			default:
				dist = dot_product(point, thisnode->normal)
					- thisnode->dist;
		}
		if (dist > HUNT_WALL_EPSILON) {
			nodenum = thisnode->children[0];
		} else if (dist < -HUNT_WALL_EPSILON) {
			nodenum = thisnode->children[1];
		} else {
			break;
		}
	}
	{
		int facenum;
		for (facenum = thisnode->firstface;
			 facenum < thisnode->firstface + thisnode->numfaces;
			 facenum++) {
			if (TestLineOpaque_face(facenum, point)) {
				return true;
			}
		}
	}
	return TestPointOpaque_r(thisnode->children[0], solid, point)
		|| TestPointOpaque_r(thisnode->children[1], solid, point);
}

bool TestPointOpaque(
	int modelnum,
	float3_array const & modelorigin,
	bool solid,
	float3_array const & point
) {
	opaquemodel_t* thismodel = &opaquemodels[modelnum];
	float3_array newpoint = vector_subtract(point, modelorigin);
	for (std::size_t axial = 0; axial < 3; ++axial) {
		if (newpoint[axial] > thismodel->maxs[axial]) {
			return false;
		}
		if (newpoint[axial] < thismodel->mins[axial]) {
			return false;
		}
	}
	return TestPointOpaque_r(thismodel->headnode, solid, newpoint);
}
