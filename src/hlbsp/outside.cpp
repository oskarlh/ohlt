#include "filelib.h"
#include "hlbsp.h"
#include "log.h"

#include <algorithm>
#include <cstring>
#include <ranges> // IWYU pragma: keep as long as we're using cartesian_product behind an #ifdef __cpp_lib_ranges_cartesian_product
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
	if (node->isportalleaf) {
		// Log("PointInLeaf::node->contents == %i\n", node->contents);
		return node;
	}

	double const d = dot_product(g_mapPlanes[node->planenum].normal, point)
		- g_mapPlanes[node->planenum].dist;

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
	node_t* n = PointInLeaf(headnode, point);
	if (n->contents == contents_t::SOLID) {
		return false;
	}
	// Log("PlaceOccupant::n->contents == %i\n", n->contents);

	n->occupied = num;
	return true;
}

// =====================================================================================
//  MarkLeakTrail
// =====================================================================================
static bsp_portal_t* prevleaknode;
static FILE* pointfile;
static FILE* linefile;

static void MarkLeakTrail(bsp_portal_t* n2) {
	bsp_portal_t* n1 = prevleaknode;
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

	double3_array dir = vector_subtract(p2, p1);
	double len = vector_length(dir);
	normalize_vector(dir);

	while (len > 2) {
		fprintf(pointfile, "%f %f %f\n", p1[0], p1[1], p1[2]);
		for (std::size_t i = 0; i < 3; ++i) {
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
	if (n->planenum == -1) {
		if (!(n->isportalleaf && n->contents == contents_t::SOLID)) {
			free(n->markfaces);
			n->markfaces = nullptr;
		}
		return;
	}

	for (std::size_t i = 0; i < 2; ++i) {
		FreeDetailNode_r(n->children[i]);
		delete n->children[i];
		n->children[i] = nullptr;
	}
	face_t* next;
	for (face_t* f = n->faces; f; f = next) {
		next = f->next;
		delete f;
	}
	n->faces = nullptr;
}

static void FillLeaf(node_t* l) {
	if (!l->isportalleaf) {
		Warning("FillLeaf: not leaf");
		return;
	}
	if (l->contents == contents_t::SOLID) {
		Warning("FillLeaf: fill solid");
		return;
	}
	FreeDetailNode_r(l);
	l->contents = contents_t::SOLID;
	l->planenum = -1;
}

static int hit_occupied;
static int backdraw;

static bool RecursiveFillOutside(node_t* l, bool const fill) {
	if ((l->contents == contents_t::SOLID)
	    || (l->contents == contents_t::SKY)) {
		/*if (l->contents != contents_t::SOLID)
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

	for (bsp_portal_t* p = l->portals; p;) {
		int s = (p->nodes[0] == l);

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
		for (face_t** fp = node->markfaces; *fp; fp++) {
			(*fp)->outputnumber = 0;
		}
	} else {
		MarkFacesInside_r(node->children[0]);
		MarkFacesInside_r(node->children[1]);
	}
}

static node_t* ClearOutFaces_r(node_t* node) {
	// mark the node and all it's faces, so they
	// can be removed if no children use them

	node->valid = 0; // will be set if any children touch it
	for (face_t* f = node->faces; f; f = f->next) {
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
		face_t* f = node->faces;
		node->faces = nullptr;

		for (face_t* fnext; f; f = fnext) {
			fnext = f->next;
			if (f->outputnumber == -1) { // never referenced, so free it
				c_free_faces++;
				delete f;
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
			if (node->children[0]->contents == contents_t::SOLID
			    && node->children[1]->contents == contents_t::SOLID) {
				node->contents = contents_t::SOLID;
				node->planenum = -1;
				node->isportalleaf = true;
				return node;
			}

			// if one child is solid, shortcut down the other side
			if (node->children[0]->contents == contents_t::SOLID) {
				return node->children[1];
			}
			if (node->children[1]->contents == contents_t::SOLID) {
				return node->children[0];
			}

			c_falsenodes++;
		}
		return node;
	}

	//
	// leaf node
	//
	if (node->contents != contents_t::SOLID) {
		// this node is still inside

		// mark all the nodes used as portals
		for (bsp_portal_t* p = node->portals; p;) {
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
			bool const isComment = line.starts_with(u8'#')
				|| line.starts_with(u8"//");
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

template <class ContainerType>
auto cartesian_product(ContainerType const & a, ContainerType const & b) {
#ifdef __cpp_lib_ranges_cartesian_product
	return std::ranges::views::cartesian_product(a, b);
#else
	std::vector<std::tuple<
		typename ContainerType::value_type,
		typename ContainerType::value_type>>
		cartProd;
	cartProd.reserve(a.size() * b.size());
	for (auto const & aEl : a) {
		for (auto const & bEl : b) {
			cartProd.emplace_back(aEl, bEl);
		}
	}
	return cartProd;

#endif
}

// =====================================================================================
//  FillOutside
// =====================================================================================
node_t*
FillOutside(node_t* node, bool const leakfile, unsigned const hullnum) {
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
	bool inside = false;
	for (int i = 1; i < g_numentities; i++) {
		double3_array origin = get_double3_for_key(
			g_entities[i], u8"origin"
		);
		std::u8string_view const cl = get_classname(g_entities[i]);
		if (!isClassnameAllowableOutside(cl)) {
			if (has_key_value(&g_entities[i], u8"origin")) {
				origin[2] += 1; // so objects on floor are ok

				// nudge playerstart around if needed so clipping hulls
				// allways have a valid point
				if (cl == u8"info_player_start") {
					double3_array offsets{ -16, 0, -16 };

					for (auto [xOffset, yOffset] :
					     cartesian_product(offsets, offsets)) {
						double3_array originPlusOffset{ origin };
						originPlusOffset[0] += xOffset;
						originPlusOffset[1] += yOffset;
						if (PlaceOccupant(i, originPlusOffset, node)) {
							inside = true;
							break;
						}
					}
				} else if (PlaceOccupant(i, origin, node)) {
					inside = true;
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

	int s = !(g_outside_node.portals->nodes[1] == &g_outside_node);

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

	bool ret = RecursiveFillOutside(
		g_outside_node.portals->nodes[s], false
	);

	if (leakfile) {
		fclose(pointfile);
		fclose(linefile);
	}

	if (ret) {
		double3_array origin = get_double3_for_key(
			g_entities[hit_occupied], u8"origin"
		);
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
		if (node->contents == contents_t::SOLID
		    || node->contents == contents_t::SKY) {
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
		int s;
		for (bsp_portal_t* p = node->portals; p; p = p->next[!s]) {
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
	g_outside_node.empty = 0;
	ResetMark_r(node);
	for (entity_count i = 1; i < g_numentities; i++) {
		if (has_key_value(&g_entities[i], u8"origin")) {
			double3_array origin = get_double3_for_key(
				g_entities[i], u8"origin"
			);
			origin[2] += 1;
			node_t* innode = PointInLeaf(node, origin);
			MarkOccupied_r(innode);
			origin[2] -= 2;
			innode = PointInLeaf(node, origin);
			MarkOccupied_r(innode);
		}
	}
	RemoveUnused_r(node);
}
