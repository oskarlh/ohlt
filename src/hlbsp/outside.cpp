#include "bsp5.h"

#include <algorithm>
#include <cstring>
#include <vector>

//  PointInLeaf
//  PlaceOccupant
//  MarkLeakTrail
//  RecursiveFillOutside
//  ClearOutFaces_r
//  isClassnameAllowableOutside
//  FreeAllowableOutsideList
//  LoadAllowableOutsideList
//  FillOutside

static int outleafs;
static int valid;
static int c_falsenodes;
static int c_free_faces;
static int c_keep_faces;

// =====================================================================================
//  PointInLeaf
// =====================================================================================
static node_t* PointInLeaf(node_t* node, double3_array const & point) {
	double d;

	if (node->isportalleaf) {
		// Log("PointInLeaf::node->contents == %i\n", node->contents);
		return node;
	}

	d = DotProduct(g_mapplanes[node->planenum].normal, point)
		- g_mapplanes[node->planenum].dist;

	if (d > 0) {
		return PointInLeaf(node->children[0], point);
	}

	return PointInLeaf(node->children[1], point);
}

// =====================================================================================
//  PlaceOccupant
// =====================================================================================
static bool PlaceOccupant(
	int const num, double3_array const & point, node_t* headnode
) {
	node_t* n;

	n = PointInLeaf(headnode, point);
	if (n->contents == CONTENTS_SOLID) {
		return false;
	}
	// Log("PlaceOccupant::n->contents == %i\n", n->contents);

	n->occupied = num;
	return true;
}

// =====================================================================================
//  MarkLeakTrail
// =====================================================================================
static portal_t* prevleaknode;
static FILE* pointfile;
static FILE* linefile;

static void MarkLeakTrail(portal_t* n2) {
	int i;
	float len;
	portal_t* n1;

	n1 = prevleaknode;
	prevleaknode = n2;

	if (!n1) {
		return;
	}

	double3_array p1 = n1->winding->getCenter();
	double3_array p2 = n2->winding->getCenter();

	// Linefile
	fprintf(
		linefile,
		"%f %f %f - %f %f %f\n",
		p1[0],
		p1[1],
		p1[2],
		p2[0],
		p2[1],
		p2[2]
	);

	// Pointfile
	fprintf(pointfile, "%f %f %f\n", p1[0], p1[1], p1[2]);

	double3_array dir;
	VectorSubtract(p2, p1, dir);
	len = vector_length(dir);
	normalize_vector(dir);

	while (len > 2) {
		fprintf(pointfile, "%f %f %f\n", p1[0], p1[1], p1[2]);
		for (i = 0; i < 3; i++) {
			p1[i] += dir[i] * 2;
		}
		len -= 2;
	}
}

// =====================================================================================
//  RecursiveFillOutside
//      Returns true if an occupied leaf is reached
//      If fill is false, just check, don't fill
// =====================================================================================
static void FreeDetailNode_r(node_t* n) {
	int i;
	if (n->planenum == -1) {
		if (!(n->isportalleaf && n->contents == CONTENTS_SOLID)) {
			free(n->markfaces);
			n->markfaces = nullptr;
		}
		return;
	}
	for (i = 0; i < 2; i++) {
		FreeDetailNode_r(n->children[i]);
		free(n->children[i]);
		n->children[i] = nullptr;
	}
	face_t *f, *next;
	for (f = n->faces; f; f = next) {
		next = f->next;
		FreeFace(f);
	}
	n->faces = nullptr;
}

static void FillLeaf(node_t* l) {
	if (!l->isportalleaf) {
		Warning("FillLeaf: not leaf");
		return;
	}
	if (l->contents == CONTENTS_SOLID) {
		Warning("FillLeaf: fill solid");
		return;
	}
	FreeDetailNode_r(l);
	l->contents = CONTENTS_SOLID;
	l->planenum = -1;
}

static int hit_occupied;
static int backdraw;

static bool RecursiveFillOutside(node_t* l, bool const fill) {
	portal_t* p;
	int s;

	if ((l->contents == CONTENTS_SOLID) || (l->contents == CONTENTS_SKY)) {
		/*if (l->contents != CONTENTS_SOLID)
			Log("RecursiveFillOutside::l->contents == %i \n",
		   l->contents);*/

		return false;
	}

	if (l->valid == valid) {
		return false;
	}

	if (l->occupied) {
		hit_occupied = l->occupied;
		backdraw = 1000;
		return true;
	}

	l->valid = valid;

	// fill it and it's neighbors
	if (fill) {
		FillLeaf(l);
	}
	outleafs++;

	for (p = l->portals; p;) {
		s = (p->nodes[0] == l);

		if (RecursiveFillOutside(
				p->nodes[s], fill
			)) { // leaked, so stop filling
			if (backdraw-- > 0) {
				MarkLeakTrail(p);
			}
			return true;
		}
		p = p->next[!s];
	}

	return false;
}

// =====================================================================================
//  ClearOutFaces_r
//      Removes unused nodes
// =====================================================================================
static void MarkFacesInside_r(node_t* node) {
	if (node->planenum == -1) {
		face_t** fp;
		for (fp = node->markfaces; *fp; fp++) {
			(*fp)->outputnumber = 0;
		}
	} else {
		MarkFacesInside_r(node->children[0]);
		MarkFacesInside_r(node->children[1]);
	}
}

static node_t* ClearOutFaces_r(node_t* node) {
	face_t* f;
	face_t* fnext;
	portal_t* p;

	// mark the node and all it's faces, so they
	// can be removed if no children use them

	node->valid = 0; // will be set if any children touch it
	for (f = node->faces; f; f = f->next) {
		f->outputnumber = -1;
	}

	// go down the children
	if (!node->isportalleaf) {
		//
		// decision node
		//
		node->children[0] = ClearOutFaces_r(node->children[0]);
		node->children[1] = ClearOutFaces_r(node->children[1]);

		// free any faces not in open child leafs
		f = node->faces;
		node->faces = nullptr;

		for (; f; f = fnext) {
			fnext = f->next;
			if (f->outputnumber == -1) { // never referenced, so free it
				c_free_faces++;
				FreeFace(f);
			} else {
				c_keep_faces++;
				f->next = node->faces;
				node->faces = f;
			}
		}

		if (!node->valid) {
			// Here leaks memory. --vluzacn
			// this node does not touch any interior leafs

			// if both children are solid, just make this node solid
			if (node->children[0]->contents == CONTENTS_SOLID
				&& node->children[1]->contents == CONTENTS_SOLID) {
				node->contents = CONTENTS_SOLID;
				node->planenum = -1;
				node->isportalleaf = true;
				return node;
			}

			// if one child is solid, shortcut down the other side
			if (node->children[0]->contents == CONTENTS_SOLID) {
				return node->children[1];
			}
			if (node->children[1]->contents == CONTENTS_SOLID) {
				return node->children[0];
			}

			c_falsenodes++;
		}
		return node;
	}

	//
	// leaf node
	//
	if (node->contents != CONTENTS_SOLID) {
		// this node is still inside

		// mark all the nodes used as portals
		for (p = node->portals; p;) {
			if (p->onnode) {
				p->onnode->valid = 1;
			}
			if (p->nodes[0] == node) // only write out from first leaf
			{
				p = p->next[0];
			} else {
				p = p->next[1];
			}
		}

		MarkFacesInside_r(node);

		return node;
	}

	return node;
}

static std::vector<std::u8string> g_strAllowableOutsideList;

bool isClassnameAllowableOutside(std::u8string_view classname) {
	return std::ranges::contains(g_strAllowableOutsideList, classname);
}

void FreeAllowableOutsideList() {
	g_strAllowableOutsideList.clear();
}

void LoadAllowableOutsideList(char const * const filename) {
	if (!filename) {
		return;
	}

	std::u8string const text
		= read_utf8_file(filename, true).value_or(u8"");

	if (text.empty()) {
		return;
	}

	Log("Reading allowable void entities from file '%s'\n", filename);

	std::u8string_view remainingText{ text };
	while (!remainingText.empty()) {
		char8_t const * nextEol = std::ranges::find(remainingText, u8'\n');
		std::u8string_view line{ remainingText.data(), nextEol };

		if (!line.empty()) {
			bool const isComment
				= line.starts_with(u8'#') || line.starts_with(u8"//");
			if (!isComment) {
				Verbose(
					"- %s can be placed in the void\n",
					(char const *) std::u8string(line).c_str()
				);
				g_strAllowableOutsideList.emplace_back(line);
			}
		}

		if (line.length() == remainingText.length()) {
			remainingText = {};
		} else {
			remainingText = remainingText.substr(line.length() + 1);
		}
	}
}

// =====================================================================================
//  FillOutside
// =====================================================================================
node_t*
FillOutside(node_t* node, bool const leakfile, unsigned const hullnum) {
	int s;
	int i;
	bool inside;
	bool ret;
	double3_array origin;

	Verbose("----- FillOutside ----\n");

	if (g_nofill) {
		Log("skipped\n");
		return node;
	}
	if (hullnum == 2 && g_nohull2) {
		return node;
	}

	//
	// place markers for all entities so
	// we know if we leak inside
	//
	inside = false;
	for (i = 1; i < g_numentities; i++) {
		origin = get_double3_for_key(g_entities[i], u8"origin");
		std::u8string_view const cl = get_classname(g_entities[i]);
		if (!isClassnameAllowableOutside(cl)) {
			if (has_key_value(&g_entities[i], u8"origin")) {
				origin[2] += 1; // so objects on floor are ok

				// nudge playerstart around if needed so clipping hulls
				// allways have a valid point
				if (cl == u8"info_player_start") {
					int x, y;

					for (x = -16; x <= 16; x += 16) {
						for (y = -16; y <= 16; y += 16) {
							origin[0] += x;
							origin[1] += y;
							if (PlaceOccupant(i, origin, node)) {
								inside = true;
								goto gotit;
							}
							origin[0] -= x;
							origin[1] -= y;
						}
					}
				gotit:;
				} else {
					if (PlaceOccupant(i, origin, node)) {
						inside = true;
					}
				}
			}
		}
	}

	if (!inside) {
		Warning(
			"No entities exist in hull %i, no filling performed for this hull",
			hullnum
		);
		return node;
	}

	if (!g_outside_node.portals) {
		Warning(
			"No outside node portal found in hull %i, no filling performed for this hull",
			hullnum
		);
		return node;
	}

	s = !(g_outside_node.portals->nodes[1] == &g_outside_node);

	// first check to see if an occupied leaf is hit
	outleafs = 0;
	valid++;

	prevleaknode = nullptr;

	if (leakfile) {
		pointfile = fopen(g_pointfilename.c_str(), "w");
		if (!pointfile) {
			Error("Couldn't open pointfile %s\n", g_pointfilename.c_str());
		}

		linefile = fopen(g_linefilename.c_str(), "w");
		if (!linefile) {
			Error("Couldn't open linefile %s\n", g_linefilename.c_str());
		}
	}

	ret = RecursiveFillOutside(g_outside_node.portals->nodes[s], false);

	if (leakfile) {
		fclose(pointfile);
		fclose(linefile);
	}

	if (ret) {
		origin = get_double3_for_key(g_entities[hit_occupied], u8"origin");
		{
			Warning(
				"=== LEAK in hull %i ===\nEntity %s @ (%4.0f,%4.0f,%4.0f)",
				hullnum,
				(char const *) get_classname(g_entities[hit_occupied])
					.data(),
				origin[0],
				origin[1],
				origin[2]
			);
			PrintOnce(
				"\n  A LEAK is a hole in the map, where the inside of it is exposed to the\n"
				"(unwanted) outside region.  The entity listed in the error is just a helpful\n"
				"indication of where the beginning of the leak pointfile starts, so the\n"
				"beginning of the line can be quickly found and traced to until reaching the\n"
				"outside. Unless this entity is accidentally on the outside of the map, it\n"
				"probably should not be deleted.  Some complex rotating objects entities need\n"
				"their origins outside the map.  To deal with these, just enclose the origin\n"
				"brush with a solid world brush\n"
			);
		}

		if (!g_bLeaked) {
			// First leak spits this out
			Log("Leak pointfile generated\n\n");
		}

		if (g_bLeakOnly) {
			Error("Stopped by leak.");
		}

		g_bLeaked = true;

		return node;
	}
	if (leakfile && !ret) {
		std::filesystem::remove(g_linefilename);
		std::filesystem::remove(g_pointfilename);
	}

	// now go back and fill things in
	valid++;
	RecursiveFillOutside(g_outside_node.portals->nodes[s], true);

	// remove faces and nodes from filled in leafs
	c_falsenodes = 0;
	c_free_faces = 0;
	c_keep_faces = 0;
	node = ClearOutFaces_r(node);

	Verbose("%5i outleafs\n", outleafs);
	Verbose("%5i freed faces\n", c_free_faces);
	Verbose("%5i keep faces\n", c_keep_faces);
	Verbose("%5i falsenodes\n", c_falsenodes);

	// save portal file for vis tracing
	if ((hullnum == 0) && leakfile) {
		WritePortalfile(node);
	}

	return node;
}

void ResetMark_r(node_t* node) {
	if (node->isportalleaf) {
		if (node->contents == CONTENTS_SOLID
			|| node->contents == CONTENTS_SKY) {
			node->empty = 0;
		} else {
			node->empty = 1;
		}
	} else {
		ResetMark_r(node->children[0]);
		ResetMark_r(node->children[1]);
	}
}

void MarkOccupied_r(node_t* node) {
	if (node->empty == 1) {
		node->empty = 0;
		portal_t* p;
		int s;
		for (p = node->portals; p; p = p->next[!s]) {
			s = (p->nodes[0] == node);
			MarkOccupied_r(p->nodes[s]);
		}
	}
}

void RemoveUnused_r(node_t* node) {
	if (node->isportalleaf) {
		if (node->empty == 1) {
			FillLeaf(node);
		}
	} else {
		RemoveUnused_r(node->children[0]);
		RemoveUnused_r(node->children[1]);
	}
}

void FillInside(node_t* node) {
	int i;
	g_outside_node.empty = 0;
	ResetMark_r(node);
	for (i = 1; i < g_numentities; i++) {
		if (has_key_value(&g_entities[i], u8"origin")) {
			double3_array origin;
			node_t* innode;
			origin = get_double3_for_key(g_entities[i], u8"origin");
			origin[2] += 1;
			innode = PointInLeaf(node, origin);
			MarkOccupied_r(innode);
			origin[2] -= 2;
			innode = PointInLeaf(node, origin);
			MarkOccupied_r(innode);
		}
	}
	RemoveUnused_r(node);
}
