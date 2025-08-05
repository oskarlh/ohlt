#include "brink.h"
#include "bsp_file_sizes.h"
#include "hlbsp.h"
#include "log.h"
#include "messages.h"

#include <cstring>
#include <map>

using PlaneMap = std::map<int, int>;
static PlaneMap gPlaneMap;
static int gNumMappedPlanes;
static mapplane_t gMappedPlanes[MAX_MAP_PLANES];
extern bool g_noopt;

using texinfomap_t = std::map<texinfo_count, texinfo_count>;
static texinfo_count g_nummappedtexinfo;
static texinfo_t g_mappedtexinfo[FINAL_MAX_MAP_TEXINFO];
static texinfomap_t g_texinfomap;

int count_mergedclipnodes;
using clipnodemap_t = std::map<std::pair<int, std::pair<int, int>>, int>;

inline clipnodemap_t::key_type MakeKey(dclipnode_t const & c) {
	return std::make_pair(
		c.planenum, std::make_pair(c.children[0], c.children[1])
	);
}

// =====================================================================================
//  WritePlane
//  hook for plane optimization
// =====================================================================================
static int WritePlane(int planenum) {
	planenum = planenum & (~1);

	if (g_noopt) {
		return planenum;
	}

	PlaneMap::const_iterator const item = gPlaneMap.find(planenum);
	if (item != gPlaneMap.end()) {
		return item->second;
	}
	// add plane to BSP
	hlassume(gNumMappedPlanes < MAX_MAP_PLANES, assume_MAX_MAP_PLANES);
	gMappedPlanes[gNumMappedPlanes] = g_mapPlanes[planenum];
	gPlaneMap.insert(PlaneMap::value_type(planenum, gNumMappedPlanes));

	return gNumMappedPlanes++;
}

static texinfo_count WriteTexinfo(texinfo_count texinfo) {
	if (texinfo >= g_numtexinfo) {
		Error("Bad texinfo number %d.\n", texinfo);
	}

	if (g_noopt) {
		return texinfo;
	}

	texinfomap_t::iterator it;
	it = g_texinfomap.find(texinfo);
	if (it != g_texinfomap.end()) {
		return it->second;
	}

	hlassume(
		g_nummappedtexinfo < FINAL_MAX_MAP_TEXINFO,
		assume_FINAL_MAX_MAP_TEXINFO
	);
	texinfo_count const c = g_nummappedtexinfo;
	g_mappedtexinfo[g_nummappedtexinfo] = g_texinfo[texinfo];
	g_texinfomap.insert(
		texinfomap_t::value_type(texinfo, g_nummappedtexinfo)
	);
	g_nummappedtexinfo++;
	return c;
}

// =====================================================================================
//  WriteClipNodes_r
// =====================================================================================
static int WriteClipNodes_r(
	node_t* node, node_t const * portalleaf, clipnodemap_t* outputmap
) {
	if (node->isportalleaf) {
		if (node->contents == contents_t::SOLID) {
			delete node;
			return std::to_underlying(contents_t::SOLID);
		} else {
			portalleaf = node;
		}
	}
	if (node->planenum == -1) {
		int num;
		if (node->iscontentsdetail) {
			num = std::to_underlying(contents_t::SOLID);
		} else {
			num = std::to_underlying(portalleaf->contents);
		}
		free(node->markfaces);
		delete node;
		return num;
	}

	dclipnode_t tmpclipnode{}; // this clipnode will be inserted into
							   // g_dclipnodes[c] if it can't be merged

	dclipnode_t* cn = &tmpclipnode;
	int c = g_numclipnodes;
	g_numclipnodes++;
	if (node->planenum & 1) {
		Error("WriteClipNodes_r: odd planenum");
	}
	cn->planenum = WritePlane(node->planenum);
	for (std::size_t i = 0; i < 2; ++i) {
		cn->children[i] = WriteClipNodes_r(
			node->children[i], portalleaf, outputmap
		);
	}
	clipnodemap_t::const_iterator const output{ outputmap->find(MakeKey(*cn)
	) };
	if (g_noclipnodemerge || output == outputmap->end()) {
		hlassume(c < MAX_MAP_CLIPNODES, assume_MAX_MAP_CLIPNODES);
		g_dclipnodes[c] = *cn;
		(*outputmap)[MakeKey(*cn)] = c;
	} else {
		count_mergedclipnodes++;
		if (g_numclipnodes != c + 1) {
			Error("Merge clipnodes: internal error");
		}
		g_numclipnodes = c;
		c = output->second; // use existing clipnode
	}

	delete node;
	return c;
}

// =====================================================================================
//  WriteClipNodes
//      Called after the clipping hull is completed.  Generates a disk
//      format representation and frees the original memory.
// =====================================================================================
void WriteClipNodes(node_t* nodes) {
	// we only merge among the clipnodes of the same hull of the same model
	clipnodemap_t outputmap;
	WriteClipNodes_r(nodes, nullptr, &outputmap);
}

// =====================================================================================
//  WriteDrawLeaf
// =====================================================================================
static int WriteDrawLeaf(node_t* node, node_t const * portalleaf) {
	face_t** fp;
	face_t* f;
	dleaf_t* leaf_p;
	int leafnum = g_numleafs;

	// emit a leaf
	hlassume(g_numleafs < MAX_MAP_LEAFS, assume_MAX_MAP_LEAFS);
	leaf_p = &g_dleafs[g_numleafs];
	g_numleafs++;

	leaf_p->contents = portalleaf->contents;

	//
	// write bounding box info
	//
	double3_array mins, maxs;

	if (node->isdetail) {
		// intersect its loose bounds with the strict bounds of its parent
		// portalleaf
		mins = vector_maximums(portalleaf->mins, node->loosemins);
		maxs = vector_minimums(portalleaf->maxs, node->loosemaxs);
	} else {
		mins = node->mins;
		maxs = node->maxs;
	}
	for (int k = 0; k < 3; k++) {
		leaf_p->mins[k] = (short
		) std::max(-32767, std::min((int) mins[k], 32767));
		leaf_p->maxs[k] = (short
		) std::max(-32767, std::min((int) maxs[k], 32767));
	}

	leaf_p->visofs = -1; // no vis info yet

	//
	// write the marksurfaces
	//
	leaf_p->firstmarksurface = g_nummarksurfaces;

	hlassume(node->markfaces != nullptr, assume_EmptySolid);

	for (fp = node->markfaces; *fp; fp++) {
		// emit a marksurface
		f = *fp;
		do {
			// fix face 0 being seen everywhere
			if (f->outputnumber == -1) {
				f = f->original;
				continue;
			}
			if (get_texture_by_number(f->texturenum).is_hidden()) {
				f = f->original;
				continue;
			}
			g_dmarksurfaces[g_nummarksurfaces] = f->outputnumber;
			hlassume(
				g_nummarksurfaces < MAX_MAP_MARKSURFACES,
				assume_MAX_MAP_MARKSURFACES
			);
			g_nummarksurfaces++;
			f = f->original; // grab tjunction split faces
		} while (f);
	}
	free(node->markfaces);

	leaf_p->nummarksurfaces = g_nummarksurfaces - leaf_p->firstmarksurface;
	return leafnum;
}

// =====================================================================================
//  WriteFace
// =====================================================================================
static void WriteFace(face_t* f) {
	dface_t* df;

	wad_texture_name const textureName{ get_texture_by_number(f->texturenum
	) };

	if (textureName.is_ordinary_hint() || textureName.is_skip()
		|| should_face_have_facestyle_null(textureName, f->contents)
		|| textureName.marks_discardable_faces()
		|| f->texturenum == no_texinfo
		|| f->referenced
			== 0 // this face is not referenced by any nonsolid leaf because
				 // it is completely covered by func_details
		|| textureName.is_env_sky()) {
		f->outputnumber = -1;
		return;
	}

	f->outputnumber = g_numfaces;

	df = &g_dfaces[g_numfaces];
	hlassume(g_numfaces < MAX_MAP_FACES, assume_MAX_MAP_FACES);
	g_numfaces++;

	df->planenum = WritePlane(f->planenum);
	df->side = f->planenum & 1;
	df->firstedge = g_numsurfedges;
	df->numedges = f->pts.size();

	df->texinfo = WriteTexinfo(f->texturenum);

	for (int i = 0; i < f->pts.size(); i++) {
		int e = f->outputedges[i];
		hlassume(
			g_numsurfedges < MAX_MAP_SURFEDGES, assume_MAX_MAP_SURFEDGES
		);
		g_dsurfedges[g_numsurfedges] = e;
		g_numsurfedges++;
	}
	free(f->outputedges);
	f->outputedges = nullptr;
}

// =====================================================================================
//  WriteDrawNodes_r
// =====================================================================================
static int WriteDrawNodes_r(node_t* node, node_t const * portalleaf) {
	if (node->isportalleaf) {
		if (node->contents == contents_t::SOLID) {
			return -1;
		} else {
			portalleaf = node;
			// Warning: make sure parent data have not been freed when
			// writing children.
		}
	}
	if (node->planenum == -1) {
		if (node->iscontentsdetail) {
			free(node->markfaces);
			return -1;
		} else {
			int leafnum = WriteDrawLeaf(node, portalleaf);
			return -1 - leafnum;
		}
	}
	dnode_t* n;
	int i;
	face_t* f;
	int nodenum = g_numnodes;

	// emit a node
	hlassume(g_numnodes < MAX_MAP_NODES, assume_MAX_MAP_NODES);
	n = &g_dnodes[g_numnodes];
	g_numnodes++;

	double3_array mins, maxs;

	if (node->isdetail) {
		// intersect its loose bounds with the strict bounds of its parent
		// portalleaf
		mins = vector_maximums(portalleaf->mins, node->loosemins);
		maxs = vector_minimums(portalleaf->maxs, node->loosemaxs);
	} else {
		mins = node->mins;
		maxs = node->maxs;
	}
	for (int k = 0; k < 3; k++) {
		n->mins[k] = (short
		) std::max(-32767, std::min((int) mins[k], 32767));
		n->maxs[k] = (short
		) std::max(-32767, std::min((int) maxs[k], 32767));
	}

	if (node->planenum & 1) {
		Error("WriteDrawNodes_r: odd planenum");
	}
	n->planenum = WritePlane(node->planenum);
	n->firstface = g_numfaces;

	for (f = node->faces; f; f = f->next) {
		WriteFace(f);
	}

	n->numfaces = g_numfaces - n->firstface;

	//
	// recursively output the other nodes
	//
	for (i = 0; i < 2; ++i) {
		n->children[i] = WriteDrawNodes_r(node->children[i], portalleaf);
	}
	return nodenum;
}

// =====================================================================================
//  FreeDrawNodes_r
// =====================================================================================
static void FreeDrawNodes_r(node_t* node) {
	for (int i = 0; i < 2; i++) {
		if (node->children[i]->planenum != -1) {
			FreeDrawNodes_r(node->children[i]);
		}
	}

	//
	// free the faces on the node
	//
	face_t* next;
	for (face_t* f = node->faces; f; f = next) {
		next = f->next;
		delete f;
	}

	delete node;
}

// =====================================================================================
//  WriteDrawNodes
//      Called after a drawing hull is completed
//      Frees all nodes and faces
// =====================================================================================
void OutputEdges_face(face_t* f) {
	wad_texture_name const textureName{ get_texture_by_number(f->texturenum
	) };

	if (textureName.is_ordinary_hint() || textureName.is_skip()
		|| should_face_have_facestyle_null(textureName, f->contents)
		|| textureName.marks_discardable_faces()
		|| f->texturenum == no_texinfo || f->referenced == 0
		|| textureName.is_env_sky()) {
		return;
	}
	f->outputedges = (int*) malloc(f->pts.size() * sizeof(int));
	hlassume(f->outputedges != nullptr, assume_NoMemory);
	int i;
	for (i = 0; i < f->pts.size(); i++) {
		int e = GetEdge(f->pts[i], f->pts[(i + 1) % f->pts.size()], f);
		f->outputedges[i] = e;
	}
}

std::optional<detail_level>
OutputEdges_r(node_t* node, detail_level detailLevel) {
	if (node->is_leaf_node()) {
		return std::nullopt;
	}
	std::optional<detail_level> next;

	for (face_t* f = node->faces; f; f = f->next) {
		if (f->detailLevel > detailLevel) {
			if (!next || f->detailLevel < next) {
				next = f->detailLevel;
			}
		}
		if (f->detailLevel == detailLevel) {
			OutputEdges_face(f);
		}
	}
	for (std::size_t i = 0; i < 2; ++i) {
		std::optional<detail_level> r = OutputEdges_r(
			node->children[i], detailLevel
		);
		if (!next.has_value()
			|| (r.has_value() && r.value() < next.value())) {
			next = r;
		}
	}
	return next;
}

static void RemoveCoveredFaces_r(node_t* node) {
	if (node->isportalleaf) {
		if (node->contents == contents_t::SOLID) {
			return; // stop here, don't go deeper into children
		}
	}
	if (node->planenum == -1) {
		// this is a leaf
		if (node->iscontentsdetail) {
			return;
		} else {
			face_t** fp;
			for (fp = node->markfaces; *fp; fp++) {
				for (face_t* f = *fp; f;
					 f = f->original) // for each tjunc subface
				{
					f->referenced++; // mark the face as referenced
				}
			}
		}
		return;
	}

	// this is a node
	for (face_t* f = node->faces; f; f = f->next) {
		f->referenced = 0; // clear the mark
	}

	RemoveCoveredFaces_r(node->children[0]);
	RemoveCoveredFaces_r(node->children[1]);
}

void WriteDrawNodes(node_t* headnode) {
	RemoveCoveredFaces_r(headnode); // fill "referenced" value
	// higher detail level should not compete for edge pairing with lower
	// detail level.

	std::optional<detail_level> nextDetailLevel = 0;
	while (
		(nextDetailLevel = OutputEdges_r(headnode, nextDetailLevel.value()))
			.has_value()
	)
		;

	WriteDrawNodes_r(headnode, nullptr);
}

// =====================================================================================
//  BeginBSPFile
// =====================================================================================
void BeginBSPFile() {
	// these values may actually be initialized
	// if the file existed when loaded, so clear them explicitly
	gNumMappedPlanes = 0;
	gPlaneMap.clear();

	g_nummappedtexinfo = 0;
	g_texinfomap.clear();

	count_mergedclipnodes = 0;
	g_nummodels = 0;
	g_numfaces = 0;
	g_numnodes = 0;
	g_numclipnodes = 0;
	g_numvertexes = 0;
	g_nummarksurfaces = 0;
	g_numsurfedges = 0;

	// edge 0 is not used, because 0 can't be negated
	g_numedges = 1;

	// leaf 0 is common solid with no faces
	g_numleafs = 1;
	g_dleafs[0].contents = contents_t::SOLID;
}

// =====================================================================================
//  FinishBSPFile
// =====================================================================================
void FinishBSPFile(bsp_data const & bspData) {
	Verbose("--- FinishBSPFile ---\n");

	if (g_dmodels[0].visleafs > MAX_MAP_LEAFS_ENGINE) {
		Warning(
			"Number of world leaves(%d) exceeded MAX_MAP_LEAFS(%zd)\nIf you encounter problems when running your map, consider this the most likely cause.\n",
			g_dmodels[0].visleafs,
			MAX_MAP_LEAFS_ENGINE
		);
	}
	if (g_dmodels[0].numfaces > MAX_MAP_WORLDFACES) {
		Warning(
			"Number of world faces(%d) exceeded %zd. Some faces will disappear in game.\nTo reduce world faces, change some world brushes (including func_details) to func_walls.\n",
			g_dmodels[0].numfaces,
			MAX_MAP_WORLDFACES
		);
	}
	Developer(
		developer_level::message,
		"count_mergedclipnodes = %d\n",
		count_mergedclipnodes
	);
	if (!g_noclipnodemerge) {
		Log("Reduced %d clipnodes to %d\n",
			g_numclipnodes + count_mergedclipnodes,
			g_numclipnodes);
	}
	if (!g_noopt) {
		{
			Log("Reduced %d texinfos to %d\n",
				g_numtexinfo,
				g_nummappedtexinfo);
			for (int i = 0; i < g_nummappedtexinfo; i++) {
				g_texinfo[i] = g_mappedtexinfo[i];
			}
			g_numtexinfo = g_nummappedtexinfo;
		}
		{
			dmiptexlump_t* l = (dmiptexlump_t*) g_dtexdata.data();
			std::int32_t& g_nummiptex = l->nummiptex;
			bool* Used = (bool*) calloc(g_nummiptex, sizeof(bool));
			int Num = 0, Size = 0;
			int* Map = (int*) malloc(g_nummiptex * sizeof(int));
			int i;
			hlassume(Used != nullptr && Map != nullptr, assume_NoMemory);
			int* lumpsizes = (int*) malloc(g_nummiptex * sizeof(int));
			int const newdatasizemax = g_texdatasize
				- ((byte*) &l->dataofs[g_nummiptex] - (byte*) l);
			byte* newdata = (byte*) malloc(newdatasizemax);
			int newdatasize = 0;
			hlassume(
				lumpsizes != nullptr && newdata != nullptr, assume_NoMemory
			);
			int total = 0;
			for (i = 0; i < g_nummiptex; i++) {
				if (l->dataofs[i] == -1) {
					lumpsizes[i] = -1;
					continue;
				}
				lumpsizes[i] = g_texdatasize - l->dataofs[i];
				for (int j = 0; j < g_nummiptex; j++) {
					int lumpsize = l->dataofs[j] - l->dataofs[i];
					if (l->dataofs[j] == -1 || lumpsize < 0
						|| lumpsize == 0 && j <= i) {
						continue;
					}
					if (lumpsize < lumpsizes[i]) {
						lumpsizes[i] = lumpsize;
					}
				}
				total += lumpsizes[i];
			}
			if (total != newdatasizemax) {
				Warning("Bad texdata structure.\n");
				goto skipReduceTexdata;
			}
			for (i = 0; i < g_numtexinfo; i++) {
				texinfo_t* t = &g_texinfo[i];
				if (t->miptex < 0 || t->miptex >= g_nummiptex) {
					Warning("Bad miptex number %d.\n", t->miptex);
					goto skipReduceTexdata;
				}
				Used[t->miptex] = true;
			}
			for (i = 0; i < g_nummiptex; i++) {
				if (l->dataofs[i] < 0) {
					continue;
				}
				if (Used[i] == true) {
					miptex_t* m = (miptex_t*) ((std::byte*) l
											   + l->dataofs[i]);
					wad_texture_name name{ m->name };
					if (!name.is_animation_frame() && !name.is_tile()) {
						continue;
					}

					for (bool alternateAnimation :
						 std::array{ false, true }) {
						for (std::size_t frameNumber = 0; frameNumber < 10;
							 ++frameNumber) {
							name.set_animation_frame_or_tile_number(
								frameNumber, alternateAnimation
							);
							// TODO: IS THIS REALLY THE BEST WAY TO FIND
							// THESE TEXTURES????
							for (std::size_t k = 0; k < g_nummiptex; ++k) {
								if (l->dataofs[k] < 0) {
									continue;
								}
								miptex_t const * m2
									= (miptex_t*) ((byte*) l + l->dataofs[k]
									);
								if (name == m2->name) {
									Used[k] = true;
								}
							}
						}
					}
				}
			}
			for (i = 0; i < g_nummiptex; i++) {
				if (Used[i]) {
					Map[i] = Num;
					Num++;
				} else {
					Map[i] = -1;
				}
			}
			for (i = 0; i < g_numtexinfo; i++) {
				texinfo_t* t = &g_texinfo[i];
				t->miptex = Map[t->miptex];
			}
			Size += (byte*) &l->dataofs[Num] - (byte*) l;
			for (i = 0; i < g_nummiptex; i++) {
				if (Used[i]) {
					if (lumpsizes[i] == -1) {
						l->dataofs[Map[i]] = -1;
					} else {
						std::memcpy(
							(byte*) newdata + newdatasize,
							(byte*) l + l->dataofs[i],
							lumpsizes[i]
						);
						l->dataofs[Map[i]] = Size;
						newdatasize += lumpsizes[i];
						Size += lumpsizes[i];
					}
				}
			}
			std::memcpy(&l->dataofs[Num], newdata, newdatasize);
			Log("Reduced %d texdatas to %d (%d bytes to %d)\n",
				g_nummiptex,
				Num,
				g_texdatasize,
				Size);
			g_nummiptex = Num;
			g_texdatasize = Size;
		skipReduceTexdata:;
			free(lumpsizes);
			free(newdata);
			free(Used);
			free(Map);
		}
		Log("Reduced %d planes to %d\n", g_numplanes, gNumMappedPlanes);
		g_mapPlanes.clear();
		for (int counter = 0; counter < gNumMappedPlanes; counter++) {
			g_mapPlanes.emplace_back(gMappedPlanes[counter]);
		}
		g_numplanes = gNumMappedPlanes;
	} else {
		hlassume(
			g_numtexinfo < FINAL_MAX_MAP_TEXINFO,
			assume_FINAL_MAX_MAP_TEXINFO
		);
		hlassume(g_numplanes < MAX_MAP_PLANES, assume_MAX_MAP_PLANES);
	}

	if (!g_nobrink) {
		Log("FixBrinks:\n");
		dclipnode_t* clipnodes; //[MAX_MAP_CLIPNODES]
		int numclipnodes;
		clipnodes = (dclipnode_t*) malloc(
			MAX_MAP_CLIPNODES * sizeof(dclipnode_t)
		);
		hlassume(clipnodes != nullptr, assume_NoMemory);
		std::array<bbrinkinfo_t*, NUM_HULLS>* brinkinfo; //[MAX_MAP_MODELS]
		int(*headnode)[NUM_HULLS];						 //[MAX_MAP_MODELS]
		brinkinfo = (std::array<bbrinkinfo_t*, NUM_HULLS>*) malloc(
			MAX_MAP_MODELS * sizeof(std::array<bbrinkinfo_t*, NUM_HULLS>)
		);

		hlassume(brinkinfo != nullptr, assume_NoMemory);
		headnode = (int(*)[NUM_HULLS]
		) malloc(MAX_MAP_MODELS * sizeof(int[NUM_HULLS]));
		hlassume(headnode != nullptr, assume_NoMemory);

		int i, j, level;
		for (i = 0; i < g_nummodels; i++) {
			dmodel_t* m = &g_dmodels[i];
			Developer(developer_level::message, " model %d\n", i);
			for (j = 1; j < NUM_HULLS; j++) {
				brinkinfo[i][j] = CreateBrinkinfo(
					g_dclipnodes.data(), m->headnode[j]
				);
			}
		}
		for (level = BrinkAny; level > BrinkNone; level--) {
			numclipnodes = 0;
			count_mergedclipnodes = 0;
			for (i = 0; i < g_nummodels; i++) {
				for (j = 1; j < NUM_HULLS; j++) {
					if (!FixBrinks(
							brinkinfo[i][j],
							(bbrinklevel_e) level,
							headnode[i][j],
							clipnodes,
							MAX_MAP_CLIPNODES,
							numclipnodes,
							numclipnodes
						)) {
						break;
					}
				}
				if (j < NUM_HULLS) {
					break;
				}
			}
			if (i == g_nummodels) {
				break;
			}
		}
		for (i = 0; i < g_nummodels; i++) {
			for (j = 1; j < NUM_HULLS; j++) {
				DeleteBrinkinfo(brinkinfo[i][j]);
			}
		}
		if (level == BrinkNone) {
			Warning(
				"No brinks have been fixed because clipnode data is almost full."
			);
		} else {
			if (level != BrinkAny) {
				Warning(
					"Not all brinks have been fixed because clipnode data is almost full."
				);
			}
			Developer(
				developer_level::message,
				"count_mergedclipnodes = %d\n",
				count_mergedclipnodes
			);
			Log("Increased %d clipnodes to %d.\n",
				g_numclipnodes,
				numclipnodes);
			g_numclipnodes = numclipnodes;
			std::memcpy(
				g_dclipnodes.data(),
				clipnodes,
				numclipnodes * sizeof(dclipnode_t)
			);
			for (i = 0; i < g_nummodels; i++) {
				dmodel_t* m = &g_dmodels[i];
				for (j = 1; j < NUM_HULLS; j++) {
					m->headnode[j] = headnode[i][j];
				}
			}
		}
		free(brinkinfo);
		free(headnode);
		free(clipnodes);
	}

	WriteExtentFile(g_extentfilename);

	if (g_chart) {
		print_bsp_file_sizes(bspData);
	}

	for (int i = 0; i < g_numplanes; i++) {
		mapplane_t const & mp = g_mapPlanes[i];
		dplane_t& dp = g_dplanes[i];
		dp = {};
		dp.normal = to_float3(mp.normal);
		dp.dist = mp.dist;
		dp.type = mp.type;
	}
	WriteBSPFile(g_bspfilename);
}
