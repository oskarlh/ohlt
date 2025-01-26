#include "bspfile.h"
#include "cmdlib.h"
#include "log.h" //--vluzacn
#include "mathlib.h"
#include "qrad.h"
#include "winding.h"

// #define      ON_EPSILON      0.001

struct tnode_t {
	planetype type;
	float3_array normal;
	float dist;
	int children[2];
	int pad;
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
			t->children[i] = g_dleafs[-node->children[i] - 1].contents;
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
void MakeTnodes(dmodel_t* /*bm*/) {
	// 32 byte align the structs
	tnodes = (tnode_t*) calloc((g_numnodes + 1), sizeof(tnode_t));

	// The alignment doesn't have any effect at all. --vluzacn
	int ofs = 31
		- (int) (((uintptr_t) tnodes + (uintptr_t) 31) & (uintptr_t) 31);
	tnodes = (tnode_t*) ((byte*) tnodes + ofs);
	tnode_p = tnodes;

	MakeTnode(0);
}

//==========================================================

static int TestLine_r(
	int const node,
	float3_array const & start,
	float3_array const & stop,
	int& linecontent,
	float3_array& skyhit
) {
	tnode_t* tnode;
	float front, back;
	float3_array mid;
	float frac;
	int side;
	int r;

	if (node < 0) {
		if (node == linecontent) {
			return CONTENTS_EMPTY;
		}
		if (node == CONTENTS_SOLID) {
			return CONTENTS_SOLID;
		}
		if (node == CONTENTS_SKY) {
			skyhit = start;
			return CONTENTS_SKY;
		}
		if (linecontent) {
			return CONTENTS_SOLID;
		}
		linecontent = node;
		return CONTENTS_EMPTY;
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
		return TestLine_r(
			tnode->children[0], start, stop, linecontent, skyhit
		);
	}
	if (front < -ON_EPSILON / 2 && back < -ON_EPSILON / 2) {
		return TestLine_r(
			tnode->children[1], start, stop, linecontent, skyhit
		);
	}
	if (fabs(front) <= ON_EPSILON && fabs(back) <= ON_EPSILON) {
		int r1 = TestLine_r(
			tnode->children[0], start, stop, linecontent, skyhit
		);
		if (r1 == CONTENTS_SOLID) {
			return CONTENTS_SOLID;
		}
		int r2 = TestLine_r(
			tnode->children[1], start, stop, linecontent, skyhit
		);
		if (r2 == CONTENTS_SOLID) {
			return CONTENTS_SOLID;
		}
		if (r1 == CONTENTS_SKY || r2 == CONTENTS_SKY) {
			return CONTENTS_SKY;
		}
		return CONTENTS_EMPTY;
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
	r = TestLine_r(tnode->children[side], start, mid, linecontent, skyhit);
	if (r != CONTENTS_EMPTY) {
		return r;
	}
	return TestLine_r(
		tnode->children[!side], mid, stop, linecontent, skyhit
	);
}

int TestLine(
	float3_array const & start,
	float3_array const & stop,
	float3_array& skyhit
) {
	int linecontent = 0;
	return TestLine_r(0, start, stop, linecontent, skyhit);
}

struct opaqueface_t {
	fast_winding* winding;
	dplane_t plane;
	dplane_t* edges;
	tex_vecs texVecs;
	std::uint32_t numedges;
	std::int32_t texinfo;
	std::uint32_t gTexturesIndex;
	bool tex_alphatest;
};

opaqueface_t* opaquefaces;

typedef struct opaquenode_s {
	planetype type;
	float3_array normal;
	float dist;
	int children[2];
	int firstface;
	int numfaces;
} opaquenode_t;

opaquenode_t* opaquenodes;

opaquemodel_t* opaquemodels;

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

	fast_winding* w = f->winding;
	fast_winding const * w2 = f2->winding;
	float3_array const *pA, *pB, *pC, *pD, *p2A, *p2B, *p2C, *p2D;
	int i, i2;

	for (i = 0; i < w->size(); i++) {
		for (i2 = 0; i2 < w2->size(); i2++) {
			pA = &w->m_Points[(i + w->size() - 1) % w->size()];
			pB = &w->m_Points[i];
			pC = &w->m_Points[(i + 1) % w->size()];
			pD = &w->m_Points[(i + 2) % w->size()];
			p2A = &w2->m_Points[(i2 + w2->size() - 1) % w2->size()];
			p2B = &w2->m_Points[i2];
			p2C = &w2->m_Points[(i2 + 1) % w2->size()];
			p2D = &w2->m_Points[(i2 + 2) % w2->size()];
			if (!vectors_almost_same(*pB, *p2C)
				|| !vectors_almost_same(*pC, *p2B)) {
				continue;
			}
			break;
		}
		if (i2 == w2->size()) {
			continue;
		}
		break;
	}
	if (i == w->size()) {
		return false;
	}

	float const * normal = f->plane.normal.data();
	float3_array e1, e2;
	dplane_t pl1, pl2;
	int side1, side2;

	VectorSubtract(*p2D, *pA, e1);
	CrossProduct(normal, e1, pl1.normal); // pointing outward
	if (normalize_vector(pl1.normal) == 0.0) {
		Developer(
			developer_level::warning, "Warning: TryMerge: Empty edge.\n"
		);
		return false;
	}
	pl1.dist = DotProduct(*pA, pl1.normal);
	if (DotProduct(*pB, pl1.normal) - pl1.dist < -ON_EPSILON) {
		return false;
	}
	side1 = (DotProduct(*pB, pl1.normal) - pl1.dist > ON_EPSILON) ? 1 : 0;

	VectorSubtract(*pD, *p2A, e2);
	CrossProduct(normal, e2, pl2.normal); // pointing outward
	if (normalize_vector(pl2.normal) == 0.0) {
		Developer(
			developer_level::warning, "Warning: TryMerge: Empty edge.\n"
		);
		return false;
	}
	pl2.dist = DotProduct(*p2A, pl2.normal);
	if (DotProduct(*p2B, pl2.normal) - pl2.dist < -ON_EPSILON) {
		return false;
	}
	side2 = (DotProduct(*p2B, pl2.normal) - pl2.dist > ON_EPSILON) ? 1 : 0;

	fast_winding neww = fast_winding(
		w->size() + w2->size() - 4 + side1 + side2
	);
	int j;
	int k = 0;
	for (j = (i + 2) % w->size(); j != i; j = (j + 1) % w->size()) {
		neww.m_Points[k] = w->m_Points[j];
		k++;
	}
	if (side1) {
		neww.m_Points[k] = w->m_Points[j];
		k++;
	}
	for (j = (i2 + 2) % w2->size(); j != i2; j = (j + 1) % w2->size()) {
		neww.m_Points[k] = w2->m_Points[j];
		k++;
	}
	if (side2) {
		neww.m_Points[k] = w2->m_Points[j];
		k++;
	}
	neww.RemoveColinearPoints();
	delete f->winding;
	f->winding = nullptr;
	if (neww.size() < 3) {
		Developer(
			developer_level::warning, "Warning: TryMerge: Empty winding.\n"
		);
	} else {
		f->winding = new fast_winding(std::move(neww));
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
			VectorClear(pl->normal);
			pl->dist = -1;
			continue;
		}
		pl->dist = DotProduct(pl->normal, p1);
	}
}

void CreateOpaqueNodes() {
	opaquemodels = (opaquemodel_t*) calloc(
		g_nummodels, sizeof(opaquemodel_t)
	);
	opaquenodes = (opaquenode_t*) calloc(g_numnodes, sizeof(opaquenode_t));
	opaquefaces = (opaqueface_t*) calloc(g_numfaces, sizeof(opaqueface_t));
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
			VectorInverse(of->plane.normal);
			of->plane.dist = -of->plane.dist;
		}
		of->texinfo = df->texinfo;
		texinfo_t* info = &g_texinfo[of->texinfo];
		for (std::size_t j = 0; j < 2; ++j) {
			for (std::size_t k = 0; k < 4; ++k) {
				of->texVecs[j][k] = info->vecs[j][k];
			}
		}
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
	free(opaquefaces);
	free(opaquenodes);
	free(opaquemodels);
}

static int TestLineOpaque_face(int facenum, float3_array const & hit) {
	opaqueface_t* thisface = &opaquefaces[facenum];
	int x;
	if (thisface->numedges == 0) {
		Developer(
			developer_level::warning,
			"Warning: TestLineOpaque: Empty face.\n"
		);
		return 0;
	}
	for (x = 0; x < thisface->numedges; x++) {
		if (DotProduct(hit, thisface->edges[x].normal)
				- thisface->edges[x].dist
			> ON_EPSILON) {
			return 0;
		}
	}
	if (thisface->tex_alphatest) {
		radtexture_t const & tex = g_textures[thisface->gTexturesIndex];
		double x = DotProduct(hit, thisface->texVecs[0])
			+ thisface->texVecs[0][3];
		double y = DotProduct(hit, thisface->texVecs[1])
			+ thisface->texVecs[1][3];
		x = floor(x - tex.width * floor(x / tex.width));
		y = floor(y - tex.height * floor(y / tex.height));
		x = x > tex.width - 1 ? tex.width - 1 : x < 0 ? 0 : x;
		y = y > tex.height - 1 ? tex.height - 1 : y < 0 ? 0 : y;
		if (tex.canvas[(int) y * tex.width + (int) x] == 0xFF) {
			return 0;
		}
	}
	return 1;
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
			front = DotProduct(start, thisnode->normal) - thisnode->dist;
			back = DotProduct(stop, thisnode->normal) - thisnode->dist;
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
	float3_array p1, p2;
	VectorSubtract(start, modelorigin, p1);
	VectorSubtract(stop, modelorigin, p2);
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

int TestPointOpaque_r(int nodenum, bool solid, float3_array const & point) {
	opaquenode_t* thisnode;
	float dist;
	while (1) {
		if (nodenum < 0) {
			if (solid
				&& g_dleafs[-nodenum - 1].contents == CONTENTS_SOLID) {
				return 1;
			} else {
				return 0;
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
				dist = DotProduct(point, thisnode->normal) - thisnode->dist;
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
				return 1;
			}
		}
	}
	return TestPointOpaque_r(thisnode->children[0], solid, point)
		|| TestPointOpaque_r(thisnode->children[1], solid, point);
}

int TestPointOpaque(
	int modelnum,
	float3_array const & modelorigin,
	bool solid,
	float3_array const & point
) {
	opaquemodel_t* thismodel = &opaquemodels[modelnum];
	float3_array newpoint;
	VectorSubtract(point, modelorigin, newpoint);
	int axial;
	for (axial = 0; axial < 3; axial++) {
		if (newpoint[axial] > thismodel->maxs[axial]) {
			return 0;
		}
		if (newpoint[axial] < thismodel->mins[axial]) {
			return 0;
		}
	}
	return TestPointOpaque_r(thismodel->headnode, solid, newpoint);
}
