#include "developer_level.h"
#include "hlassert.h"
#include "hlbsp.h"
#include "log.h"
#include "mathtypes.h"
#include "time_counter.h"
#include "util.h"

#include <cstring>

//  FaceSide
//  ChooseMidPlaneFromList
//  ChoosePlaneFromList
//  SelectPartition

//  CalcSurfaceInfo
//  DivideSurface
//  SplitNodeSurfaces
//  RankForContents
//  ContentsForRank

//  FreeLeafSurfs
//  LinkLeafFaces
//  MakeNodePortal
//  SplitNodePortals
//  CalcNodeBounds
//  CopyFacesToNode
//  BuildBspTree_r
//  SolidBSP

//  Each node or leaf will have a set of portals that completely enclose
//  the volume of the node and pass into an adjacent node.
#include <utility>
#include <vector>

int g_maxnode_size = DEFAULT_MAXNODE_SIZE;

static bool g_reportProgress = false;
static int g_numProcessed = 0;
static int g_numReported = 0;

static void ResetStatus(bool report_progress) {
	g_reportProgress = report_progress;
	g_numProcessed = g_numReported = 0;
}

static void UpdateStatus(void) {
	if (g_reportProgress) {
		++g_numProcessed;
		if ((g_numProcessed / 500) > g_numReported) {
			g_numReported = (g_numProcessed / 500);
			Log("%d...", g_numProcessed);
		}
	}
}

// =====================================================================================
//  FaceSide
//      For BSP hueristic
// =====================================================================================
static face_side FaceSide(
	face_t* in,
	mapplane_t const * const split,
	double* epsilonsplit = nullptr
) {
	double const epsilonmin = 0.002, epsilonmax = 0.2;
	double d_front, d_back;
	double dot;
	int i;

	d_front = d_back = 0;

	// axial planes are fast
	if (split->type <= last_axial) {
		double3_array const * p;
		for (i = 0, p = in->pts.begin(); i < in->pts.size(); i++, ++p) {
			dot = (*p)[std::to_underlying(split->type)] - split->dist;
			if (dot > d_front) {
				d_front = dot;
			}
			if (dot < d_back) {
				d_back = dot;
			}
		}
	} else {
		// sloping planes take longer
		double3_array const * p;
		for (i = 0, p = in->pts.begin(); i < in->pts.size(); ++i, ++p) {
			dot = dot_product(*p, split->normal);
			dot -= split->dist;
			if (dot > d_front) {
				d_front = dot;
			}
			if (dot < d_back) {
				d_back = dot;
			}
		}
	}
	if (d_front <= ON_EPSILON) {
		if (d_front > epsilonmin || d_back > -epsilonmax) {
			if (epsilonsplit) {
				(*epsilonsplit)++;
			}
		}
		return face_side::back;
	}
	if (d_back >= -ON_EPSILON) {
		if (d_back < -epsilonmin || d_front < epsilonmax) {
			if (epsilonsplit) {
				(*epsilonsplit)++;
			}
		}
		return face_side::front;
	}
	if (d_front < epsilonmax || d_back > -epsilonmax) {
		if (epsilonsplit) {
			(*epsilonsplit)++;
		}
	}
	return face_side::on;
}

// organize all surfaces into a tree structure to accelerate intersection
// test can reduce more than 90% compile time for very complicated maps

struct surfacetreenode_t final {
	int size; // can be zero, which invalidates mins and maxs
	int size_discardable;
	double3_array mins;
	double3_array maxs;
	bool isleaf;
	// node
	surfacetreenode_t* children[2];
	std::vector<face_t*>* nodefaces;
	int nodefaces_discardablesize;
	// leaf
	std::vector<face_t*>* leaffaces;
};

struct surfacetree_result_t final {
	int frontsize;
	int backsize;
	std::vector<face_t*>* middle; // may contain coplanar faces and
								  // discardable(SOLIDHINT) faces
};

struct surfacetree_t final {
	bool dontbuild;
	double epsilon; // if a face is not epsilon far from the splitting
					// plane, put it in result.middle
	surfacetreenode_t* headnode;

	surfacetree_result_t result;
};

void BuildSurfaceTree_r(surfacetree_t* tree, surfacetreenode_t* node) {
	node->size = node->leaffaces->size();
	node->size_discardable = 0;
	if (node->size == 0) {
		node->isleaf = true;
		return;
	}

	node->mins.fill(hlbsp_bogus_range);
	node->maxs.fill(-hlbsp_bogus_range);
	for (std::vector<face_t*>::iterator i = node->leaffaces->begin();
		 i != node->leaffaces->end();
		 ++i) {
		face_t* f = *i;
		for (int x = 0; x < f->pts.size(); x++) {
			node->mins = vector_minimums(node->mins, f->pts[x]);
			node->maxs = vector_maximums(node->maxs, f->pts[x]);
		}
		if (f->facestyle == face_discardable) {
			node->size_discardable++;
		}
	}

	int bestaxis = -1;
	{
		double bestdelta = 0;
		for (int k = 0; k < 3; k++) {
			if (node->maxs[k] - node->mins[k] > bestdelta + ON_EPSILON) {
				bestaxis = k;
				bestdelta = node->maxs[k] - node->mins[k];
			}
		}
	}
	if (node->size <= 5 || tree->dontbuild == true || bestaxis == -1) {
		node->isleaf = true;
		return;
	}

	node->isleaf = false;
	double dist, dist1, dist2;
	dist = (node->mins[bestaxis] + node->maxs[bestaxis]) / 2;
	dist1 = (3 * node->mins[bestaxis] + node->maxs[bestaxis]) / 4;
	dist2 = (node->mins[bestaxis] + 3 * node->maxs[bestaxis]) / 4;
	// Each child node is at most 3/4 the size of the parent node.
	// Most faces should be passed to a child node, faces left in the parent
	// node are the ones whose dimensions are large enough to be comparable
	// to the dimension of the parent node.
	node->nodefaces = new std::vector<face_t*>;
	node->nodefaces_discardablesize = 0;
	node->children[0] = (surfacetreenode_t*) malloc(sizeof(surfacetreenode_t
	));
	node->children[0]->leaffaces = new std::vector<face_t*>;
	node->children[1] = (surfacetreenode_t*) malloc(sizeof(surfacetreenode_t
	));
	node->children[1]->leaffaces = new std::vector<face_t*>;
	for (std::vector<face_t*>::iterator i = node->leaffaces->begin();
		 i != node->leaffaces->end();
		 ++i) {
		face_t* f = *i;
		double low = hlbsp_bogus_range;
		double high = -hlbsp_bogus_range;
		for (int x = 0; x < f->pts.size(); x++) {
			low = std::min(low, f->pts[x][bestaxis]);
			high = std::max(high, f->pts[x][bestaxis]);
		}
		if (low < dist1 + ON_EPSILON && high > dist2 - ON_EPSILON) {
			node->nodefaces->push_back(f);
			if (f->facestyle == face_discardable) {
				node->nodefaces_discardablesize++;
			}
		} else if (low >= dist1 && high <= dist2) {
			if ((low + high) / 2 > dist) {
				node->children[0]->leaffaces->push_back(f);
			} else {
				node->children[1]->leaffaces->push_back(f);
			}
		} else if (low >= dist1) {
			node->children[0]->leaffaces->push_back(f);
		} else if (high <= dist2) {
			node->children[1]->leaffaces->push_back(f);
		}
	}
	if (node->children[0]->leaffaces->size() == node->leaffaces->size()
		|| node->children[1]->leaffaces->size()
			== node->leaffaces->size()) {
		Warning(
			"BuildSurfaceTree_r: didn't split node with bound (%f,%f,%f)-(%f,%f,%f)",
			node->mins[0],
			node->mins[1],
			node->mins[2],
			node->maxs[0],
			node->maxs[1],
			node->maxs[2]
		);
		delete node->children[0]->leaffaces;
		delete node->children[1]->leaffaces;
		free(node->children[0]);
		free(node->children[1]);
		delete node->nodefaces;
		node->isleaf = true;
		return;
	}
	delete node->leaffaces;
	BuildSurfaceTree_r(tree, node->children[0]);
	BuildSurfaceTree_r(tree, node->children[1]);
}

surfacetree_t* BuildSurfaceTree(surface_t* surfaces, double epsilon) {
	surfacetree_t* tree;
	tree = (surfacetree_t*) malloc(sizeof(surfacetree_t));
	tree->epsilon = epsilon;
	tree->result.middle = new std::vector<face_t*>;
	tree->headnode = (surfacetreenode_t*) malloc(sizeof(surfacetreenode_t));
	tree->headnode->leaffaces = new std::vector<face_t*>;
	{
		surface_t* p2;
		face_t* f;
		for (p2 = surfaces; p2; p2 = p2->next) {
			if (p2->onnode) {
				continue;
			}
			for (f = p2->faces; f; f = f->next) {
				tree->headnode->leaffaces->push_back(f);
			}
		}
	}
	tree->dontbuild = tree->headnode->leaffaces->size() < 20;
	BuildSurfaceTree_r(tree, tree->headnode);
	if (tree->dontbuild) {
		*tree->result.middle = *tree->headnode->leaffaces;
		tree->result.backsize = 0;
		tree->result.frontsize = 0;
	}
	return tree;
}

void TestSurfaceTree_r(
	surfacetree_t* tree,
	surfacetreenode_t const * node,
	mapplane_t const * split
) {
	if (node->size == 0) {
		return;
	}
	double low, high;
	low = high = -split->dist;
	for (int k = 0; k < 3; k++) {
		if (split->normal[k] >= 0) {
			high += split->normal[k] * node->maxs[k];
			low += split->normal[k] * node->mins[k];
		} else {
			high += split->normal[k] * node->mins[k];
			low += split->normal[k] * node->maxs[k];
		}
	}
	if (low > tree->epsilon) {
		tree->result.frontsize += node->size;
		tree->result.frontsize -= node->size_discardable;
		return;
	}
	if (high < -tree->epsilon) {
		tree->result.backsize += node->size;
		tree->result.backsize -= node->size_discardable;
		return;
	}
	if (node->isleaf) {
		for (std::vector<face_t*>::iterator i = node->leaffaces->begin();
			 i != node->leaffaces->end();
			 ++i) {
			tree->result.middle->push_back(*i);
		}
	} else {
		for (std::vector<face_t*>::iterator i = node->nodefaces->begin();
			 i != node->nodefaces->end();
			 ++i) {
			tree->result.middle->push_back(*i);
		}
		TestSurfaceTree_r(tree, node->children[0], split);
		TestSurfaceTree_r(tree, node->children[1], split);
	}
}

void TestSurfaceTree(surfacetree_t* tree, mapplane_t const * split) {
	if (tree->dontbuild) {
		return;
	}
	tree->result.middle->clear();
	tree->result.backsize = 0;
	tree->result.frontsize = 0;
	TestSurfaceTree_r(tree, tree->headnode, split);
}

void DeleteSurfaceTree_r(surfacetreenode_t* node) {
	if (node->isleaf) {
		delete node->leaffaces;
	} else {
		DeleteSurfaceTree_r(node->children[0]);
		free(node->children[0]);
		DeleteSurfaceTree_r(node->children[1]);
		free(node->children[1]);
		delete node->nodefaces;
	}
}

void DeleteSurfaceTree(surfacetree_t* tree) {
	DeleteSurfaceTree_r(tree->headnode);
	free(tree->headnode);
	delete tree->result.middle;
	free(tree);
}

// =====================================================================================
//  ChooseMidPlaneFromList
//      When there are a huge number of planes, just choose one closest
//      to the middle.
// =====================================================================================
static surface_t* ChooseMidPlaneFromList(
	surface_t* surfaces,
	double3_array const & mins,
	double3_array const & maxs,
	detail_level detailLevel
) {
	surface_t* p;
	surface_t* bestsurface;
	double bestvalue;
	double value;
	double dist;

	//
	// pick the plane that splits the least
	//
	bestvalue = 6.0 * hlbsp_bogus_range * hlbsp_bogus_range;
	bestsurface = nullptr;

	for (p = surfaces; p; p = p->next) {
		if (p->onnode) {
			continue;
		}
		if (p->detailLevel != detailLevel) {
			continue;
		}

		mapplane_t const & plane = g_mapPlanes[p->planenum];

		// check for axis aligned surfaces
		planetype l{ plane.type };
		if (l > last_axial) {
			continue;
		}

		//
		// calculate the split metric along axis l, smaller values are
		// better
		//
		value = 0;

		dist = plane.dist * plane.normal[std::size_t(l)];
		if (maxs[std::size_t(l)] - dist < ON_EPSILON
			|| dist - mins[std::size_t(l)] < ON_EPSILON) {
			continue;
		}
		if (maxs[std::size_t(l)] - dist < g_maxnode_size / 2.0 - ON_EPSILON
			|| dist - mins[std::size_t(l)]
				< g_maxnode_size / 2.0 - ON_EPSILON) {
			continue;
		}
		for (planetype j = planetype::plane_x; j <= planetype::plane_z;
			 j = planetype(std::size_t(j) + 1)) {
			if (j == l) {
				value += (maxs[std::size_t(l)] - dist)
					* (maxs[std::size_t(l)] - dist);
				value += (dist - mins[std::size_t(l)])
					* (dist - mins[std::size_t(l)]);
			} else {
				value += 2 * (maxs[std::size_t(j)] - mins[std::size_t(j)])
					* (maxs[std::size_t(j)] - mins[std::size_t(j)]);
			}
		}
		if (value > bestvalue) {
			continue;
		}

		//
		// currently the best!
		//
		bestvalue = value;
		bestsurface = p;
	}

	if (!bestsurface) {
		return nullptr;
	}

	return bestsurface;
}

// =====================================================================================
//  ChoosePlaneFromList
//      Choose the plane that splits the least faces
// =====================================================================================
static surface_t* ChoosePlaneFromList(
	surface_t* surfaces,
	double3_array const & mins,
	double3_array const & maxs
	// mins and maxs are invalid when detailLevel > 0
	,
	detail_level detailLevel
) {
	surface_t* p;
	surface_t* bestsurface;
	double bestvalue;
	double value;
	face_t* f;
	double planecount;
	double totalsplit;
	double avesplit;
	double(*tmpvalue)[2];
	surfacetree_t* surfacetree;
	std::vector<face_t*>::iterator it;

	planecount = 0;
	totalsplit = 0;
	tmpvalue = (double(*)[2]) malloc(g_numplanes * sizeof(double[2]));
	surfacetree = BuildSurfaceTree(surfaces, ON_EPSILON);

	//
	// pick the plane that splits the least
	//
	bestvalue = 9e30;
	bestsurface = nullptr;

	for (p = surfaces; p; p = p->next) {
		if (p->onnode) {
			continue;
		}
		if (p->detailLevel != detailLevel) {
			continue;
		}
		planecount++;

		double crosscount = 0; // use double here because we need to perform
							   // "crosscount++"
		double frontcount = 0;
		double backcount = 0;
		double coplanarcount = 0;
		double epsilonsplit = 0;

		mapplane_t const & plane = g_mapPlanes[p->planenum];

		for (f = p->faces; f; f = f->next) {
			if (f->facestyle == face_discardable) {
				continue;
			}
			coplanarcount++;
		}
		TestSurfaceTree(surfacetree, &plane);
		{
			frontcount += surfacetree->result.frontsize;
			backcount += surfacetree->result.backsize;
			for (it = surfacetree->result.middle->begin();
				 it != surfacetree->result.middle->end();
				 ++it) {
				f = *it;
				if (f->planenum == p->planenum
					|| f->planenum == (p->planenum ^ 1)) {
					continue;
				}
				if (f->facestyle == face_discardable) {
					FaceSide(f, &plane, &epsilonsplit);
					continue;
				}
				switch (FaceSide(f, &plane, &epsilonsplit)) {
					case face_side::front:
						frontcount++;
						break;
					case face_side::back:
						backcount++;
						break;
					case face_side::on:
						totalsplit++;
						crosscount++;
						break;
					case face_side::cross:
						// Shouldn't happen
						break;
				}
			}
		}

		value = crosscount
			- std::sqrt(coplanarcount); // Not optimized. --vluzacn
		if (coplanarcount == 0) {
			crosscount += 1;
		}
		// This is the most efficient code among what I have ever tested:
		// (1) BSP file is small, despite possibility of slowing down vis
		// and rad (but still faster than the original non BSP balancing
		// method). (2) Factors need not adjust across various maps.
		double frac = (coplanarcount / 2 + crosscount / 2 + frontcount)
			/ (coplanarcount + frontcount + backcount + crosscount);
		double ent = (0.0001 < frac && frac < 0.9999)
			? (-frac * log(frac) / log(2.0)
			   - (1 - frac) * log(1 - frac) / log(2.0))
			: 0.0; // the formula tends to 0 when frac=0,1
		tmpvalue[p->planenum][1] = crosscount * (1 - ent);
		value += epsilonsplit * 10000;

		tmpvalue[p->planenum][0] = value;
	}
	avesplit = totalsplit / planecount;
	for (p = surfaces; p; p = p->next) {
		if (p->onnode) {
			continue;
		}
		if (p->detailLevel != detailLevel) {
			continue;
		}
		value = tmpvalue[p->planenum][0]
			+ avesplit * tmpvalue[p->planenum][1];
		if (value < bestvalue) {
			bestvalue = value;
			bestsurface = p;
		}
	}

	if (!bestsurface) {
		Error("ChoosePlaneFromList: no valid planes");
	}
	free(tmpvalue);
	DeleteSurfaceTree(surfacetree);
	return bestsurface;
}

std::optional<detail_level> CalcSplitDetaillevel(node_t const * node) {
	detail_level bestDetailLevel = std::numeric_limits<detail_level>::max();
	bool found{};
	for (surface_t const * s = node->surfaces; s; s = s->next) {
		if (s->onnode) {
			continue;
		}
		for (face_t const * f = s->faces; f; f = f->next) {
			if (f->facestyle == face_discardable) {
				continue;
			}
			bestDetailLevel = std::min(bestDetailLevel, f->detailLevel);
			found = true;
		}
	}
	if (!found) {
		return std::nullopt;
	}
	return bestDetailLevel;
}

// SelectPartition selects a surface from a linked list of surfaces to split
// the group on. Returns nullptr if the surface list cannot be
// divided any more (a leaf)

static surface_t* SelectPartition(
	surface_t* surfaces,
	node_t const * const node,
	bool const usemidsplit,
	detail_level splitDetailLevel,
	double3_array const & validmins,
	double3_array const & validmaxs
) {
	// We must choose a surface of this detail level

	if (usemidsplit) {
		surface_t* s = ChooseMidPlaneFromList(
			surfaces, validmins, validmaxs, splitDetailLevel
		);
		if (s != nullptr) {
			return s;
		}
	}
	return ChoosePlaneFromList(
		surfaces, node->mins, node->maxs, splitDetailLevel
	);
}

// =====================================================================================
//  CalcSurfaceInfo
//      Calculates the bounding box
// =====================================================================================
static void CalcSurfaceInfo(surface_t* surf) {
	hlassume(
		surf->faces != nullptr, assume_ValidPointer
	); // "CalcSurfaceInfo() surface without a face"

	//
	// calculate a bounding box
	//
	surf->mins.fill(99999);
	surf->maxs.fill(-99999);

	// A surface's detailLevel is the minimum detail level of its faces
	surf->detailLevel = std::numeric_limits<detail_level>::max();
	for (face_t const * f = surf->faces; f; f = f->next) {
		if (std::to_underlying(f->contents) >= 0) {
			Error("Bad contents");
		}
		for (std::size_t i = 0; i < f->pts.size(); ++i) {
			for (std::size_t j = 0; j < 3; ++j) {
				if (f->pts[i][j] < surf->mins[j]) {
					surf->mins[j] = f->pts[i][j];
				}
				if (f->pts[i][j] > surf->maxs[j]) {
					surf->maxs[j] = f->pts[i][j];
				}
			}
		}
		surf->detailLevel = std::min(surf->detailLevel, f->detailLevel);
	}
}

static void FixDetaillevelForDiscardable(
	node_t* node, std::optional<detail_level> detailLevel
) {
	// When we move on to the next detailLevel, some discardable faces of
	// previous detail level remain not on node (because they are
	// discardable). Remove them now
	surface_t *s, **psnext;
	face_t *f, **pfnext;

	for (psnext = &node->surfaces; s = *psnext, s != nullptr;) {
		if (s->onnode) {
			psnext = &s->next;
			continue;
		}
		hlassume(s->faces, assume_ValidPointer);
		for (pfnext = &s->faces; f = *pfnext, f != nullptr;) {
			if (!detailLevel.has_value() || f->detailLevel < detailLevel) {
				*pfnext = f->next;
				delete f;
			} else {
				pfnext = &f->next;
			}
		}
		if (!s->faces) {
			*psnext = s->next;
			delete s;
		} else {
			psnext = &s->next;
			CalcSurfaceInfo(s);
			hlassume(
				detailLevel.has_value() && s->detailLevel >= detailLevel,
				assume_first
			);
		}
	}
}

// =====================================================================================
//  DivideSurface
// =====================================================================================
static void DivideSurface(
	surface_t* in,
	mapplane_t const * const split,
	surface_t** front,
	surface_t** back
) {
	face_t* frontlist = nullptr;
	face_t* backlist = nullptr;

	mapplane_t const * const inplane = &g_mapPlanes[in->planenum];

	// parallel case is easy

	if (inplane->normal[0] == split->normal[0]
		&& inplane->normal[1] == split->normal[1]
		&& inplane->normal[2] == split->normal[2]) {
		if (inplane->dist > split->dist) {
			*front = in;
			*back = nullptr;
			return;
		} else if (inplane->dist < split->dist) {
			*front = nullptr;
			*back = in;
			return;
		} else { // split the surface into front and back
			frontlist = nullptr;
			backlist = nullptr;
			face_t* next;
			for (face_t* facet = in->faces; facet; facet = next) {
				next = facet->next;
				if (facet->planenum & 1) {
					facet->next = backlist;
					backlist = facet;
				} else {
					facet->next = frontlist;
					frontlist = facet;
				}
			}
		}
	} else {
		// do a real split.  may still end up entirely on one side
		// OPTIMIZE: use bounding box for fast test

		face_t* next;
		for (face_t* facet = in->faces; facet; facet = next) {
			next = facet->next;
			face_t* frontFragment;
			face_t* backFragment;
			SplitFace(facet, split, &frontFragment, &backFragment);
			if (frontFragment) {
				frontFragment->next = frontlist;
				frontlist = frontFragment;
			}
			if (backFragment) {
				backFragment->next = backlist;
				backlist = backFragment;
			}
		}
	}

	// if nothing actually got split, just move the in plane
	if (frontlist == nullptr && backlist == nullptr) {
		*front = nullptr;
		*back = nullptr;
		return;
	}
	if (frontlist == nullptr) {
		*front = nullptr;
		*back = in;
		in->faces = backlist;
		return;
	}

	if (backlist == nullptr) {
		*front = in;
		*back = nullptr;
		in->faces = frontlist;
		return;
	}

	// stuff got split, so allocate one new surface and reuse in
	surface_t* newSurface = new surface_t{};
	*newSurface = *in;
	newSurface->faces = backlist;
	*back = newSurface;

	in->faces = frontlist;
	*front = in;

	// recalc bboxes and flags
	CalcSurfaceInfo(newSurface);
	CalcSurfaceInfo(in);
}

// =====================================================================================
//  SplitNodeSurfaces
// =====================================================================================
static void
SplitNodeSurfaces(surface_t* surfaces, node_t const * const node) {
	surface_t* p;
	surface_t* next;
	surface_t* frontlist;
	surface_t* backlist;
	surface_t* frontfrag;
	surface_t* backfrag;

	mapplane_t const * const splitplane = &g_mapPlanes[node->planenum];

	frontlist = nullptr;
	backlist = nullptr;

	for (p = surfaces; p; p = next) {
		next = p->next;
		DivideSurface(p, splitplane, &frontfrag, &backfrag);

		if (frontfrag) {
			if (!frontfrag->faces) {
				Error("surface with no faces");
			}
			frontfrag->next = frontlist;
			frontlist = frontfrag;
		}
		if (backfrag) {
			if (!backfrag->faces) {
				Error("surface with no faces");
			}
			backfrag->next = backlist;
			backlist = backfrag;
		}
	}

	node->children[0]->surfaces = frontlist;
	node->children[1]->surfaces = backlist;
}

static void SplitNodeBrushes(brush_t* brushes, node_t const * node) {
	brush_t *frontlist, *frontfrag;
	brush_t *backlist, *backfrag;
	brush_t *b, *next;
	frontlist = nullptr;
	backlist = nullptr;
	mapplane_t const & splitplane = g_mapPlanes[node->planenum];
	for (b = brushes; b; b = next) {
		next = b->next;
		SplitBrush(b, &splitplane, &frontfrag, &backfrag);
		if (frontfrag) {
			frontfrag->next = frontlist;
			frontlist = frontfrag;
		}
		if (backfrag) {
			backfrag->next = backlist;
			backlist = backfrag;
		}
	}
	node->children[0]->detailbrushes = frontlist;
	node->children[1]->detailbrushes = backlist;
}

// =====================================================================================
//  RankForContents
// =====================================================================================
static std::int32_t RankForContents(contents_t contents) {
	switch (contents) {
		case contents_t::EMPTY:
			// Log("(empty)\n");
			return 0;
		case contents_t::WATER:
			// Log("(water)\n");
			return 1;
		case contents_t::TRANSLUCENT:
			// Log("(traslucent)\n");
			return 2;
		case contents_t::CURRENT_0:
			// Log("(current_0)\n");
			return 3;
		case contents_t::CURRENT_90:
			// Log("(current_90)\n");
			return 4;
		case contents_t::CURRENT_180:
			// Log("(current_180)\n");
			return 5;
		case contents_t::CURRENT_270:
			// Log("(current_270)\n");
			return 6;
		case contents_t::CURRENT_UP:
			// Log("(current_up)\n");
			return 7;
		case contents_t::CURRENT_DOWN:
			// Log("(current_down)\n");
			return 8;
		case contents_t::SLIME:
			// Log("(slime)\n");
			return 9;
		case contents_t::LAVA:
			// Log("(lava)\n");
			return 10;
		case contents_t::SKY:
			// Log("(sky)\n");
			return 11;
		case contents_t::SOLID:
			// Log("(solid)\n");
			return 12;

		default:
			hlassert(false);
			Error(
				"RankForContents: bad contents %i",
				std::to_underlying(contents)
			);
	}
	return -1;
}

// =====================================================================================
//  ContentsForRank
// =====================================================================================
static contents_t ContentsForRank(std::int32_t rank) {
	switch (rank) {
		case -1:
			return contents_t::EMPTY;
		case 0:
			return contents_t::EMPTY;
		case 1:
			return contents_t::WATER;
		case 2:
			return contents_t::TRANSLUCENT;
		case 3:
			return contents_t::CURRENT_0;
		case 4:
			return contents_t::CURRENT_90;
		case 5:
			return contents_t::CURRENT_180;
		case 6:
			return contents_t::CURRENT_270;
		case 7:
			return contents_t::CURRENT_UP;
		case 8:
			return contents_t::CURRENT_DOWN;
		case 9:
			return contents_t::SLIME;
		case 10:
			return contents_t::LAVA;
		case 11:
			return contents_t::SKY;
		case 12:
			return contents_t::SOLID;

		default:
			hlassert(false);
			Error("ContentsForRank: bad rank %i", rank);
	}
	return contents_t::EMPTY;
}

// =====================================================================================
//  FreeLeafSurfs
// =====================================================================================
static void FreeLeafSurfs(node_t* leaf) {
	surface_t* surf;
	surface_t* snext;
	face_t* f;
	face_t* fnext;

	for (surf = leaf->surfaces; surf; surf = snext) {
		snext = surf->next;
		for (f = surf->faces; f; f = fnext) {
			fnext = f->next;
			delete f;
		}
		delete surf;
	}

	leaf->surfaces = nullptr;
}

static void FreeLeafBrushes(node_t* leaf) {
	brush_t *b, *next;
	for (b = leaf->detailbrushes; b; b = next) {
		next = b->next;
		FreeBrush(b);
	}
	leaf->detailbrushes = nullptr;
}

// =====================================================================================
//  LinkLeafFaces
//      Determines the contents of the leaf and creates the final list of
//      original faces that have some fragment inside this leaf
// =====================================================================================
#define MAX_LEAF_FACES 16384

static void
LinkLeafFaces(surface_t* planelist, node_t* leafnode, hull_count hullNum) {
	surface_t* surf;

	int rank = -1;
	int r;
	for (surf = planelist; surf; surf = surf->next) {
		if (!surf->onnode) {
			continue;
		}
		for (face_t* f = surf->faces; f; f = f->next) {
			if (f->contents == contents_t::HINT) {
				f->contents = contents_t::EMPTY;
			}
			if (f->detailLevel) {
				continue;
			}
			r = RankForContents(f->contents);
			if (r > rank) {
				rank = r;
			}
		}
	}
	for (surf = planelist; surf; surf = surf->next) {
		if (!surf->onnode) {
			continue;
		}
		face_t* f;
		for (f = surf->faces; f; f = f->next) {
			if (f->detailLevel) {
				continue;
			}
			r = RankForContents(f->contents);
			if (r != rank) {
				break;
			}
		}
		if (f) {
			break;
		}
	}
	if (surf) {
		entity_t* ent = EntityForModel(g_nummodels - 1);
		if (g_nummodels - 1 != 0 && ent == &g_entities[0]) {
			ent = nullptr;
		}
		Warning(
			"Ambiguous leafnode content ( %s and %s ) at (%.0f,%.0f,%.0f)-(%.0f,%.0f,%.0f) in hull %d of model %d (entity: classname \"%s\", origin \"%s\", targetname \"%s\")",
			(char const *) ContentsToString(ContentsForRank(r)).data(),
			(char const *) ContentsToString(ContentsForRank(rank)).data(),
			leafnode->mins[0],
			leafnode->mins[1],
			leafnode->mins[2],
			leafnode->maxs[0],
			leafnode->maxs[1],
			leafnode->maxs[2],
			hullNum,
			g_nummodels - 1,
			(ent ? (char const *) get_classname(*ent).data() : "unknown"),
			(ent ? (char const *) value_for_key(ent, u8"origin").data()
				 : "unknown"),
			(ent ? (char const *) value_for_key(ent, u8"targetname").data()
				 : "unknown")
		);
		for (surface_t* surf2 = planelist; surf2; surf2 = surf2->next) {
			for (face_t* f2 = surf2->faces; f2; f2 = f2->next) {
				Developer(
					developer_level::spam,
					"content = %d plane = %d normal = (%g,%g,%g)\n",
					std::to_underlying(f2->contents),
					f2->planenum,
					g_mapPlanes[f2->planenum].normal[0],
					g_mapPlanes[f2->planenum].normal[1],
					g_mapPlanes[f2->planenum].normal[2]
				);
				for (int i = 0; i < f2->pts.size(); i++) {
					Developer(
						developer_level::spam,
						"(%g,%g,%g)\n",
						f2->pts[i][0],
						f2->pts[i][1],
						f2->pts[i][2]
					);
				}
			}
		}
	}

	leafnode->contents = ContentsForRank(rank);
}

static void MakeLeaf(node_t* leafnode) {
	int nummarkfaces;
	face_t* markfaces[MAX_LEAF_FACES + 1];
	surface_t* surf;
	face_t* f;

	leafnode->planenum = -1;

	leafnode->iscontentsdetail = leafnode->detailbrushes != nullptr;
	FreeLeafBrushes(leafnode);
	leafnode->detailbrushes = nullptr;
	if (leafnode->boundsbrush) {
		FreeBrush(leafnode->boundsbrush);
	}
	leafnode->boundsbrush = nullptr;

	if (!(leafnode->isportalleaf && leafnode->contents == contents_t::SOLID
		)) {
		nummarkfaces = 0;
		for (surf = leafnode->surfaces; surf; surf = surf->next) {
			if (!surf->onnode) {
				continue;
			}
			if (!surf->onnode) {
				continue;
			}
			for (f = surf->faces; f; f = f->next) {
				if (f->original == nullptr) { // because it is not on node
											  // or its content is solid
					continue;
				}
				if (f->original == nullptr) {
					continue;
				}
				hlassume(
					nummarkfaces < MAX_LEAF_FACES, assume_MAX_LEAF_FACES
				);

				markfaces[nummarkfaces++] = f->original;
			}
		}
		markfaces[nummarkfaces] = nullptr; // end marker
		nummarkfaces++;

		leafnode->markfaces = (face_t**) malloc(
			nummarkfaces * sizeof(*leafnode->markfaces)
		);
		std::memcpy(
			leafnode->markfaces,
			markfaces,
			nummarkfaces * sizeof(*leafnode->markfaces)
		);
	}

	FreeLeafSurfs(leafnode);
	leafnode->surfaces = nullptr;
}

// =====================================================================================
//  MakeNodePortal
//      Create the new portal by taking the full plane winding for the
//      cutting plane and clipping it by all of the planes from the other
//      portals. Each portal tracks the node that created it, so unused
//      nodes can be removed later.
// =====================================================================================
static void MakeNodePortal(node_t* node) {
	bsp_portal_t* new_portal;
	bsp_portal_t* p;
	mapplane_t* plane;
	mapplane_t clipplane;
	accurate_winding* w;
	int side = 0;

	plane = &g_mapPlanes[node->planenum];
	w = new accurate_winding(*plane);

	new_portal = AllocPortal();
	new_portal->plane = *plane;
	new_portal->onnode = node;

	for (p = node->portals; p; p = p->next[side]) {
		clipplane = p->plane;
		if (p->nodes[0] == node) {
			side = 0;
		} else if (p->nodes[1] == node) {
			clipplane.dist = -clipplane.dist;
			clipplane.normal = negate_vector(clipplane.normal);
			side = 1;
		} else {
			Error("MakeNodePortal: mislinked portal");
		}

		w->mutating_clip(clipplane.normal, clipplane.dist, true);
		if (w->size() == 0) {
			Developer(
				developer_level::warning,
				"MakeNodePortal:new portal was clipped away from node@(%.0f,%.0f,%.0f)-(%.0f,%.0f,%.0f)",
				node->mins[0],
				node->mins[1],
				node->mins[2],
				node->maxs[0],
				node->maxs[1],
				node->maxs[2]
			);
			FreePortal(new_portal);
			return;
		}
	}

	new_portal->winding = w;
	AddPortalToNodes(new_portal, node->children[0], node->children[1]);
}

// =====================================================================================
//  SplitNodePortals
//      Move or split the portals that bound node so that the node's
//      children have portals instead of node.
// =====================================================================================
static void SplitNodePortals(node_t* node) {
	bsp_portal_t* p;
	bsp_portal_t* next_portal;
	node_t* f;
	node_t* b;
	node_t* other_node;
	int side = 0;
	mapplane_t* plane;

	plane = &g_mapPlanes[node->planenum];
	f = node->children[0];
	b = node->children[1];

	for (p = node->portals; p; p = next_portal) {
		if (p->nodes[0] == node) {
			side = 0;
		} else if (p->nodes[1] == node) {
			side = 1;
		} else {
			Error("SplitNodePortals: mislinked portal");
		}
		next_portal = p->next[side];

		other_node = p->nodes[!side];
		RemovePortalFromNode(p, p->nodes[0]);
		RemovePortalFromNode(p, p->nodes[1]);

		// Cut the portal into two portals, one on each side of the cut
		// plane
		accurate_winding& pWinding = *p->winding;

		visit_with(
			pWinding.Divide(*plane),
			[&p, &other_node, &side, &b](
				all_in_the_back_winding_division_result
			) {
				if (side == 0) {
					AddPortalToNodes(p, b, other_node);
				} else {
					AddPortalToNodes(p, other_node, b);
				}
			},
			[&p, &other_node, &side, &f](
				all_in_the_front_winding_division_result
			) {
				if (side == 0) {
					AddPortalToNodes(p, f, other_node);
				} else {
					AddPortalToNodes(p, other_node, f);
				}
			},
			[&b, &f, &other_node, &p, &side](
				accurate_winding::split_division_result& arg
			) {
				// The winding is split
				bsp_portal_t* new_portal = AllocPortal();
				*new_portal = *p;
				new_portal->winding = new accurate_winding{
					std::move(arg.back)
				};
				*(p->winding) = std::move(arg.front);

				if (side == 0) {
					AddPortalToNodes(p, f, other_node);
					AddPortalToNodes(new_portal, b, other_node);
				} else {
					AddPortalToNodes(p, other_node, f);
					AddPortalToNodes(new_portal, other_node, b);
				}
			}
		);
	}

	node->portals = nullptr;
}

// =====================================================================================
//  CalcNodeBounds
//      Determines the boundaries of a node by minmaxing all the portal
//      points, whcih completely enclose the node. Returns true if the node
//      should be midsplit.(very large)
// =====================================================================================
static bool CalcNodeBounds(
	node_t* node, double3_array& validmins, double3_array& validmaxs
) {
	bsp_portal_t* p;
	bsp_portal_t* next_portal;
	int side = 0;

	if (node->isdetail) {
		return false;
	}
	node->mins[0] = node->mins[1] = node->mins[2] = hlbsp_bogus_range;
	node->maxs[0] = node->maxs[1] = node->maxs[2] = -hlbsp_bogus_range;

	for (p = node->portals; p; p = next_portal) {
		if (p->nodes[0] == node) {
			side = 0;
		} else if (p->nodes[1] == node) {
			side = 1;
		} else {
			Error("CalcNodeBounds: mislinked portal");
		}
		next_portal = p->next[side];

		for (double3_array const & point : p->winding->points()) {
			for (std::size_t i = 0; i < 3; i++) {
				double const v = point[i];
				if (v < node->mins[i]) {
					node->mins[i] = v;
				}
				if (v > node->maxs[i]) {
					node->maxs[i] = v;
				}
			}
		}
	}

	if (node->isportalleaf) {
		return false;
	}
	for (std::size_t i = 0; i < 3; ++i) {
		validmins[i] = std::max(
			node->mins[i], -(ENGINE_ENTITY_RANGE + g_maxnode_size)
		);
		validmaxs[i] = std::min(
			node->maxs[i], ENGINE_ENTITY_RANGE + g_maxnode_size
		);
	}
	for (std::size_t i = 0; i < 3; ++i) {
		if (validmaxs[i] - validmins[i] <= ON_EPSILON) {
			return false;
		}
	}
	for (std::size_t i = 0; i < 3; ++i) {
		if (validmaxs[i] - validmins[i] > g_maxnode_size + ON_EPSILON) {
			return true;
		}
	}
	return false;
}

// =====================================================================================
//  CopyFacesToNode
//      Do a final merge attempt, then subdivide the faces to surface cache
//      size if needed. These are final faces that will be drawable in the
//      game. Copies of these faces are further chopped up into the leafs,
//      but they will reference these originals.
// =====================================================================================
static void CopyFacesToNode(node_t* node, surface_t* surf) {
	face_t** prevptr;
	face_t* f;
	face_t* newf;

	// merge as much as possible
	MergePlaneFaces(surf);

	// subdivide large faces
	prevptr = &surf->faces;
	while (1) {
		f = *prevptr;
		if (!f) {
			break;
		}
		SubdivideFace(f, prevptr);
		f = *prevptr;
		prevptr = &f->next;
	}

	// copy the faces to the node, and consider them the originals
	node->surfaces = nullptr;
	node->faces = nullptr;
	for (f = surf->faces; f; f = f->next) {
		if (f->facestyle == face_discardable) {
			continue;
		}
		if (f->contents != contents_t::SOLID) {
			newf = new face_t{};
			*newf = *f;
			f->original = newf;
			newf->next = node->faces;
			node->faces = newf;
		}
	}
}

// =====================================================================================
//  BuildBspTree_r
// =====================================================================================
static void BuildBspTree_r(node_t* node, hull_count hullNum) {
	bool midsplit;
	surface_t* allsurfs;
	double3_array validmins, validmaxs;

	midsplit = CalcNodeBounds(node, validmins, validmaxs);
	if (node->boundsbrush) {
		CalcBrushBounds(
			node->boundsbrush, node->loosemins, node->loosemaxs
		);
	} else {
		node->loosemins.fill(hlbsp_bogus_range);
		node->loosemaxs.fill(-hlbsp_bogus_range);
	}

	std::optional<detail_level> splitDetailLevel = CalcSplitDetaillevel(node
	);
	FixDetaillevelForDiscardable(node, splitDetailLevel);
	surface_t* split{};
	if (splitDetailLevel) {
		split = SelectPartition(
			node->surfaces,
			node,
			midsplit,
			splitDetailLevel.value(),
			validmins,
			validmaxs
		);
	}
	if (!node->isdetail && (!split || split->detailLevel > 0)) {
		node->isportalleaf = true;
		LinkLeafFaces(node->surfaces, node, hullNum); // set contents
		if (node->contents == contents_t::SOLID) {
			split = nullptr;
		}
	} else {
		node->isportalleaf = false;
	}
	if (!split) { // this is a leaf node
		MakeLeaf(node);
		return;
	}

	// these are final polygons
	split->onnode = node; // can't use again
	allsurfs = node->surfaces;
	node->planenum = split->planenum;
	node->faces = nullptr;
	CopyFacesToNode(node, split);

	node->children[0] = new node_t{};
	node->children[1] = new node_t{};
	node->children[0]->isdetail = split->detailLevel > 0;
	node->children[1]->isdetail = split->detailLevel > 0;

	// split all the polysurfaces into front and back lists
	SplitNodeSurfaces(allsurfs, node);
	SplitNodeBrushes(node->detailbrushes, node);
	if (node->boundsbrush) {
		for (int k = 0; k < 2; k++) {
			mapplane_t p;
			brush_t *copy, *front, *back;
			if (k == 0) { // front child
				p.normal = g_mapPlanes[split->planenum].normal;
				p.dist = g_mapPlanes[split->planenum].dist
					- BOUNDS_EXPANSION;
			} else { // back child
				p.normal = vector_subtract(
					0.0f, g_mapPlanes[split->planenum].normal
				);
				p.dist = -g_mapPlanes[split->planenum].dist
					- BOUNDS_EXPANSION;
			}
			copy = NewBrushFromBrush(node->boundsbrush);
			SplitBrush(copy, &p, &front, &back);
			if (back) {
				FreeBrush(back);
			}
			if (!front) {
				Warning(
					"BuildBspTree_r: bounds was clipped away at (%f,%f,%f)-(%f,%f,%f).",
					node->loosemins[0],
					node->loosemins[1],
					node->loosemins[2],
					node->loosemaxs[0],
					node->loosemaxs[1],
					node->loosemaxs[2]
				);
			}
			node->children[k]->boundsbrush = front;
		}
		FreeBrush(node->boundsbrush);
	}
	node->boundsbrush = nullptr;

	if (!split->detailLevel) {
		MakeNodePortal(node);
		SplitNodePortals(node);
	}

	// recursively do the children
	BuildBspTree_r(node->children[0], hullNum);
	BuildBspTree_r(node->children[1], hullNum);
	UpdateStatus();
}

// =====================================================================================
//  SolidBSP
//      Takes a chain of surfaces plus a split type, and returns a bsp tree
//      with faces off the nodes. The original surface chain will be
//      completely freed.
// =====================================================================================
node_t* SolidBSP(
	surfchain_t const * const surfhead,
	brush_t* detailbrushes,
	bool report_progress,
	hull_count hullNum
) {
	ResetStatus(report_progress);
	time_counter timeCounter;
	if (report_progress) {
		Log("SolidBSP [hull %u] ", hullNum);
	} else {
		Verbose("----- SolidBSP -----\n");
	}

	node_t* headnode = new node_t{};
	headnode->surfaces = surfhead->surfaces;
	headnode->detailbrushes = detailbrushes;
	headnode->isdetail = false;
	double3_array brushmins = vector_add(surfhead->mins, -SIDESPACE);
	double3_array brushmaxs = vector_add(surfhead->maxs, SIDESPACE);
	headnode->boundsbrush = BrushFromBox(brushmins, brushmaxs);

	// generate six portals that enclose the entire world
	MakeHeadnodePortals(headnode, surfhead->mins, surfhead->maxs);

	// recursively partition everything
	BuildBspTree_r(headnode, hullNum);

	if (report_progress) {
		Log("%d (%.2f seconds)\n", ++g_numProcessed, timeCounter.get_total()
		);
	}

	return headnode;
}
