#pragma once

#include "bsp5.h"

#include <list>

struct btreepoint_t; // 0d object
struct btreeedge_t;	 // 1d object
struct btreeface_t;	 // 2d object
struct btreeleaf_t;	 // 3d object

struct bpartition_t final {
	bbrinklevel_e type;
	bpartition_t* next;
	int planenum;
	contents_t content;
	bool planeside;
};

struct bclipnode_t final {
	mapplane_t const * plane;
	bclipnode_t* children[2]; // children[0] is the front side of the plane
							  // (face_side::front = 0)
	bpartition_t* partitions;
	btreeleaf_t* treeleaf;

	int planenum;
	contents_t content;
	bool isleaf;
};

struct btreepoint_r final {
	btreepoint_t* p;
	bool side;
};

struct btreeedge_r final {
	btreeedge_t* e;
	bool side;
};

struct btreeface_r final {
	btreeface_t* f;
	bool side;
};

struct btreeleaf_r final {
	btreeleaf_t* l;
	bool side;
};

using btreeedge_l = std::list<btreeedge_r>;
using btreeface_l = std::list<btreeface_r>;

struct btreeleaf_t final {
	btreeface_l* faces;

	bclipnode_t* clipnode; // not defined for infinite leaf

	bool infinite; // note: the infinite leaf is not convex
};

struct bbrinknode_t final {
	// We will only focus on the BSP shape which encircles a brink,
	// so we extract the clipnodes that meet with the brink and store them
	// here
	bool isleaf;

	int planenum;
	mapplane_t const * plane;
	int children[2];

	contents_t content;
	bclipnode_t* clipnode;
};

struct bbrink_t final {
	std::vector<bbrinknode_t> nodes{};
	btreeedge_t* edge{}; // Only for use in deciding brink type
	double3_array start{};
	double3_array stop{};
	double3_array direction{};

	int numnodes{ 0 }; // Including both nodes and leafs
};

struct bbrinkinfo_t final {
	bclipnode_t* clipnodes;
	btreeleaf_t* leaf_outside;
	bbrink_t** brinks;
	int numclipnodes;
	int numobjects;
	int numbrinks;
};

extern bbrinkinfo_t*
CreateBrinkinfo(dclipnode_t const * clipnodes, int headnode);
extern bool FixBrinks(
	bbrinkinfo_t const * brinkinfo,
	bbrinklevel_e level,
	int& headnode_out,
	dclipnode_t* clipnodes_out,
	int maxsize,
	int size,
	int& size_out
);
extern void DeleteBrinkinfo(bbrinkinfo_t* brinkinfo);
