#include "hlbsp.h"
#include "log.h"

#include <cstring>

node_t g_outside_node; // portals outside the world face this

//=============================================================================

/*
 * =============
 * AddPortalToNodes
 * =============
 */
void AddPortalToNodes(bsp_portal_t* p, node_t* front, node_t* back) {
	if (p->nodes[0] || p->nodes[1]) {
		Error("AddPortalToNode: allready included");
	}

	p->nodes[0] = front;
	p->next[0] = front->portals;
	front->portals = p;

	p->nodes[1] = back;
	p->next[1] = back->portals;
	back->portals = p;
}

/*
 * =============
 * RemovePortalFromNode
 * =============
 */
void RemovePortalFromNode(bsp_portal_t* portal, node_t* l) {
	bsp_portal_t** pp;
	bsp_portal_t* t;

	// remove reference to the current portal
	pp = &l->portals;
	while (1) {
		t = *pp;
		if (!t) {
			Error("RemovePortalFromNode: portal not in leaf");
		}

		if (t == portal) {
			break;
		}

		if (t->nodes[0] == l) {
			pp = &t->next[0];
		} else if (t->nodes[1] == l) {
			pp = &t->next[1];
		} else {
			Error("RemovePortalFromNode: portal not bounding leaf");
		}
	}

	if (portal->nodes[0] == l) {
		*pp = portal->next[0];
		portal->nodes[0] = nullptr;
	} else if (portal->nodes[1] == l) {
		*pp = portal->next[1];
		portal->nodes[1] = nullptr;
	}
}

//============================================================================

/*
 * ================
 * MakeHeadnodePortals
 *
 * The created portals will face the global g_outside_node
 * ================
 */
void MakeHeadnodePortals(
	node_t* node, double3_array const & mins, double3_array const & maxs
) {
	std::array<double3_array, 2> bounds;
	bsp_portal_t* p;
	bsp_portal_t* portals[6];
	mapplane_t bplanes[6];

	// pad with some space so there will never be null volume leafs
	for (std::size_t i = 0; i < 3; ++i) {
		bounds[0][i] = mins[i] - SIDESPACE;
		bounds[1][i] = maxs[i] + SIDESPACE;
	}

	g_outside_node.contents = contents_t::SOLID;
	g_outside_node.portals = nullptr;

	for (std::size_t i = 0; i < 3; ++i) {
		for (std::size_t j = 0; j < 2; ++j) {
			int n = j * 3 + i;

			p = AllocPortal();
			portals[n] = p;

			mapplane_t& pl = bplanes[n];
			pl = {};
			if (j) {
				pl.normal[i] = -1;
				pl.dist = -bounds[j][i];
			} else {
				pl.normal[i] = 1;
				pl.dist = bounds[j][i];
			}
			p->plane = pl;
			p->winding = new accurate_winding(pl);
			AddPortalToNodes(p, node, &g_outside_node);
		}
	}

	// clip the basewindings by all the other planes
	for (std::size_t i = 0; i < 6; ++i) {
		for (std::size_t j = 0; j < 6; ++j) {
			if (j == i) {
				continue;
			}
			portals[i]->winding->mutating_clip(
				bplanes[j].normal, bplanes[j].dist, true
			);
		}
	}
}

/*
 * ==============================================================================
 *
 * PORTAL FILE GENERATION
 *
 * ==============================================================================
 */

static FILE* pf;
static FILE* pf_view;
extern bool g_viewportal;
static int num_visleafs; // leafs the player can be in
static int num_visportals;

static void WritePortalFile_r(node_t const * const node) {
	int i;
	bsp_portal_t* p;
	accurate_winding* w;
	mapplane_t plane2;

	if (!node->isportalleaf) {
		WritePortalFile_r(node->children[0]);
		WritePortalFile_r(node->children[1]);
		return;
	}

	if (node->contents == contents_t::SOLID) {
		return;
	}

	for (p = node->portals; p;) {
		w = p->winding;
		if (w && p->nodes[0] == node) {
			if (p->nodes[0]->contents == p->nodes[1]->contents) {
				// write out to the file

				// sometimes planes get turned around when they are very
				// near the changeover point between different axis.
				// interpret the plane the same way vis will, and flip the
				// side orders if needed
				w->getPlane(plane2);
				if (dot_product(p->plane.normal, plane2.normal)
				    < 1.0 - ON_EPSILON) { // backwards...
					if (dot_product(p->plane.normal, plane2.normal)
					    > -1.0 + ON_EPSILON) {
						Warning("Colinear portal @");
						w->Print();
					} else {
						Warning("Backward portal @");
						w->Print();
					}
					fprintf(
						pf,
						"%zu %i %i ",
						w->size(),
						p->nodes[1]->visleafnum,
						p->nodes[0]->visleafnum
					);
				} else {
					fprintf(
						pf,
						"%zu %i %i ",
						w->size(),
						p->nodes[0]->visleafnum,
						p->nodes[1]->visleafnum
					);
				}

				for (i = 0; i < w->size(); i++) {
					fprintf(
						pf,
						"(%f %f %f) ",
						w->point(i)[0],
						w->point(i)[1],
						w->point(i)[2]
					);
				}
				fprintf(pf, "\n");
				if (g_viewportal) {
					double3_array center1, center2;
					double3_array from = { 0.0, 0.0, -65536 };
					double3_array center = w->getCenter();
					double3_array const halfOfNormal = vector_scale(
						p->plane.normal, 0.5
					);
					center1 = vector_add(center, halfOfNormal);
					center2 = vector_subtract(center, halfOfNormal);
					fprintf(
						pf_view,
						"%5.2f %5.2f %5.2f\n",
						from[0],
						from[1],
						from[2]
					);
					fprintf(
						pf_view,
						"%5.2f %5.2f %5.2f\n",
						center1[0],
						center1[1],
						center1[2]
					);
					for (i = 0; i < w->size(); i++) {
						double3_array const & p1{ w->point(i) };
						double3_array const & p2{
							w->point((i + 1) % w->size())
						};
						fprintf(
							pf_view,
							"%5.2f %5.2f %5.2f\n",
							p1[0],
							p1[1],
							p1[2]
						);
						fprintf(
							pf_view,
							"%5.2f %5.2f %5.2f\n",
							p2[0],
							p2[1],
							p2[2]
						);
						fprintf(
							pf_view,
							"%5.2f %5.2f %5.2f\n",
							center2[0],
							center2[1],
							center2[2]
						);
						fprintf(
							pf_view,
							"%5.2f %5.2f %5.2f\n",
							center1[0],
							center1[1],
							center1[2]
						);
					}
				}
			}
		}

		if (p->nodes[0] == node) {
			p = p->next[0];
		} else {
			p = p->next[1];
		}
	}
}

/*
 * ================
 * NumberLeafs_r
 * ================
 */
static void NumberLeafs_r(node_t* node) {
	bsp_portal_t* p;

	if (!node->isportalleaf) { // decision node
		node->visleafnum = -99;
		NumberLeafs_r(node->children[0]);
		NumberLeafs_r(node->children[1]);
		return;
	}

	if (node->contents
	    == contents_t::SOLID) { // solid block, viewpoint never inside
		node->visleafnum = -1;
		return;
	}

	node->visleafnum = num_visleafs++;

	for (p = node->portals; p;) {
		if (p->nodes[0] == node) // only write out from first leaf
		{
			if (p->nodes[0]->contents == p->nodes[1]->contents) {
				num_visportals++;
			}
			p = p->next[0];
		} else {
			p = p->next[1];
		}
	}
}

static int CountChildLeafs_r(node_t* node) {
	if (node->planenum == -1) {       // dleaf
		if (node->iscontentsdetail) { // solid
			return 0;
		} else {
			return 1;
		}
	} else { // node
		int count = 0;
		count += CountChildLeafs_r(node->children[0]);
		count += CountChildLeafs_r(node->children[1]);
		return count;
	}
}

static void WriteLeafCount_r(node_t* node) {
	if (!node->isportalleaf) {
		WriteLeafCount_r(node->children[0]);
		WriteLeafCount_r(node->children[1]);
	} else if (node->contents != contents_t::SOLID) {
		int count = CountChildLeafs_r(node);
		fprintf(pf, "%i\n", count);
	}
}

/*
 * ================
 * WritePortalfile
 * ================
 */
void WritePortalfile(node_t* headnode) {
	// set the visleafnum field in every leaf and count the total number of
	// portals
	num_visleafs = 0;
	num_visportals = 0;
	NumberLeafs_r(headnode);

	// write the file
	pf = fopen(g_portfilename.c_str(), "w");
	if (!pf) {
		Error("Error writing portal file %s", g_portfilename.c_str());
	}
	if (g_viewportal) {
		std::filesystem::path viewPortalFilePath{
			path_to_temp_file_with_extension(g_Mapname, u8"_portal.pts")
		};

		pf_view = fopen(viewPortalFilePath.c_str(), "w");
		if (!pf_view) {
			Error("Couldn't open %s", viewPortalFilePath.c_str());
		}
		Log("Writing '%s' ...\n", viewPortalFilePath.c_str());
	}

	fprintf(pf, "%i\n", num_visleafs);
	fprintf(pf, "%i\n", num_visportals);

	WriteLeafCount_r(headnode);
	WritePortalFile_r(headnode);
	fclose(pf);
	if (g_viewportal) {
		fclose(pf_view);
	}
	Log("BSP generation successful, writing portal file '%s'\n",
	    g_portfilename.c_str());
}

//===================================================

void FreePortals(node_t* node) {
	bsp_portal_t* p;
	bsp_portal_t* nextp;

	if (!node->isportalleaf) {
		FreePortals(node->children[0]);
		FreePortals(node->children[1]);
		return;
	}

	for (p = node->portals; p; p = nextp) {
		if (p->nodes[0] == node) {
			nextp = p->next[0];
		} else {
			nextp = p->next[1];
		}
		RemovePortalFromNode(p, p->nodes[0]);
		RemovePortalFromNode(p, p->nodes[1]);
		delete p->winding;
		FreePortal(p);
	}
}
