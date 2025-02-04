#pragma once

#include "bspfile.h"
#include "cmdlib.h"
#include "cmdlinecfg.h"
#include "filelib.h"
#include "hlassert.h"
#include "internal_types.h"
#include "log.h"
#include "mathlib.h"
#include "messages.h"
#include "threads.h"
#include "win32fix.h"
#include "winding.h"

#include <filesystem>

extern std::array<mapplane_t, MAX_INTERNAL_MAP_PLANES> g_mapplanes;

#define ENTITIES_VOID	  "entities.void"
#define ENTITIES_VOID_EXT ".void"

constexpr double hlbsp_bogus_range = 144000.0;

// The exact bounding box of the brushes is expanded some for the headnode
// volume. Is this still needed?
constexpr float SIDESPACE = 24;

//============================================================================

#define MIN_SUBDIVIDE_SIZE 64

#define MAX_SUBDIVIDE_SIZE 512

#define DEFAULT_SUBDIVIDE_SIZE               \
	((MAX_SURFACE_EXTENT - 1) * TEXTURE_STEP \
	) // #define DEFAULT_SUBDIVIDE_SIZE  240 //--vluzacn

#define MIN_MAXNODE_SIZE	 64
#define MAX_MAXNODE_SIZE	 65536
#define DEFAULT_MAXNODE_SIZE 1024

#define DEFAULT_NOFILL			false
#define DEFAULT_NOINSIDEFILL	false
#define DEFAULT_NOTJUNC			false
#define DEFAULT_NOBRINK			false
#define DEFAULT_NOCLIP			false
#define DEFAULT_NOOPT			false
#define DEFAULT_NOCLIPNODEMERGE false
#define DEFAULT_LEAKONLY		false
#define DEFAULT_WATERVIS		false

#define MAXEDGES 48 // 32
#define MAXPOINTS \
	28					 // don't let a base face get past this
						 // because it can be split more later
#define MAXNODESIZE 1024 // Valve default is 1024

enum facestyle_e {
	face_normal = 0,
	face_hint,
	face_skip,
	face_null,
	face_discardable, // contents must not differ between front and back
};

struct face_t {
	face_t* next;
	face_t* original; // Face on node
	int planenum;
	int texturenum;
	contents_t contents;	  // contents in front of face
	detail_level detailLevel; // From HLCSG
	int* outputedges;		  // used in WriteDrawNodes

	int outputnumber; // only valid for original faces after write surfaces
	int numpoints;
	int referenced; // only valid for original faces
	facestyle_e facestyle;

	double3_array pts[MAXEDGES]; // FIXME: change to use winding_t
};

struct node_t;

struct surface_t {
	surface_t* next;
	face_t* faces; // links to all the faces on either side of the surf
	node_t*
		onnode; // true if surface has already been used as a splitting node
	double3_array mins, maxs;
	int planenum;
	detail_level detailLevel; // Minimum detail level of its faces
};

struct surfchain_t {
	double3_array mins, maxs;
	surface_t* surfaces;
};

struct side_t {
	side_t* next;
	mapplane_t plane; // Facing inside (reversed when loading brush file)
	accurate_winding wind; // (Also reversed)
};

struct brush_t { // TODO: Rename this, since we have a brush_t in HLCSG too
	brush_t* next;
	side_t* sides;
};

//
// there is a node_t structure for every node and leaf in the bsp tree
//
#define PLANENUM_LEAF -1
#define BOUNDS_EXPANSION \
	1.0 // expand the bounds of detail leafs when clipping its boundsbrush,
		// to prevent some strange brushes in the func_detail from clipping
		// away the entire boundsbrush making the func_detail invisible.

struct portal_t;

struct node_t {
	surface_t* surfaces;
	brush_t* detailbrushes;
	brush_t* boundsbrush;
	double3_array loosemins,
		loosemaxs; // all leafs and nodes have this, while 'mins' and 'maxs'
				   // are only valid for nondetail leafs and nodes.

	bool isdetail;	   // is under a diskleaf
	bool isportalleaf; // not detail and children are detail; only visleafs
					   // have contents, portals, mins, maxs
	bool iscontentsdetail;	  // inside a detail brush
	double3_array mins, maxs; // bounding volume of portals;

	// information for decision nodes
	int planenum; // -1 = leaf node

	constexpr bool is_leaf_node() const noexcept {
		return planenum == -1;
	}

	node_t* children[2]; // only valid for decision nodes
	face_t* faces;		 // decision nodes only, list for both sides

	// information for leafs
	contents_t contents; // leaf nodes (0 for decision nodes)
	face_t** markfaces;	 // leaf nodes only, point to node faces
	portal_t* portals;
	int visleafnum; // -1 = solid
	int valid;		// for flood filling
	int occupied;	// light number in leaf for outside filling
	int empty;
};

//=============================================================================
// solidbsp.c
extern void SubdivideFace(face_t* f, face_t** prevptr);
extern node_t* SolidBSP(
	surfchain_t const * const surfhead,
	brush_t* detailbrushes,
	bool report_progress
);

//=============================================================================
// merge.c
extern void MergePlaneFaces(surface_t* plane);
extern void MergeAll(surface_t* surfhead);

//=============================================================================
// surfaces.c
extern void MakeFaceEdges();
extern int
GetEdge(double3_array const & p1, double3_array const & p2, face_t* f);

//=============================================================================
// portals.c
struct portal_t {
	mapplane_t plane;
	node_t* onnode;	  // NULL = outside box
	node_t* nodes[2]; // [0] = front side of plane
	portal_t* next[2];
	accurate_winding* winding;
};

extern node_t g_outside_node; // portals outside the world face this

extern void AddPortalToNodes(portal_t* p, node_t* front, node_t* back);
extern void RemovePortalFromNode(portal_t* portal, node_t* l);
extern void MakeHeadnodePortals(
	node_t* node, double3_array const & mins, double3_array const & maxs
);

extern void FreePortals(node_t* node);
extern void WritePortalfile(node_t* headnode);

//=============================================================================
// tjunc.c
void tjunc(node_t* headnode);

//=============================================================================
// writebsp.c
extern void WriteClipNodes(node_t* headnode);
extern void WriteDrawNodes(node_t* headnode);

extern void BeginBSPFile();
extern void FinishBSPFile(bsp_data const & bspData);

//=============================================================================
// outside.c
extern node_t* FillOutside(node_t* node, bool leakfile, unsigned hullnum);
extern void LoadAllowableOutsideList(char const * const filename);
extern void FreeAllowableOutsideList();
extern void FillInside(node_t* node);

//=============================================================================
// misc functions
extern void GetParamsFromEnt(entity_t* mapent);

extern face_t* AllocFace();
extern void FreeFace(face_t* f);

extern portal_t* AllocPortal();
extern void FreePortal(struct portal_t* p);

extern surface_t* AllocSurface();
extern void FreeSurface(surface_t* s);

extern side_t* NewSideFromSide(side_t const * s);
extern brush_t* AllocBrush();
extern void FreeBrush(brush_t* b);
extern brush_t* NewBrushFromBrush(brush_t const * b);
extern void SplitBrush(
	brush_t* in, mapplane_t const * split, brush_t** front, brush_t** back
);
extern brush_t*
BrushFromBox(double3_array const & mins, double3_array const & maxs);
extern void CalcBrushBounds(
	brush_t const * b, double3_array& mins, double3_array& maxs
);

extern node_t* AllocNode();

extern bool should_face_have_facestyle_null(
	wad_texture_name textureName, contents_t faceContents
) noexcept;
#define BRINK_FLOOR_THRESHOLD 0.7

enum bbrinklevel_e {
	BrinkNone = 0,
	BrinkFloorBlocking,
	BrinkFloor,
	BrinkWallBlocking,
	BrinkWall,
	BrinkAny,
};

//=============================================================================
// cull.c
extern void CullStuff();

//=============================================================================
// qbsp.c
extern bool g_nofill;
extern bool g_noinsidefill;
extern bool g_notjunc;
extern bool g_nobrink;
extern bool g_noclipnodemerge;
extern bool g_watervis;
extern bool g_chart;
extern bool g_estimate;
extern int g_maxnode_size;
extern int g_subdivide_size;
extern int g_hullnum;
extern bool g_bLeakOnly;
extern bool g_bLeaked;
extern std::filesystem::path g_portfilename;
extern std::filesystem::path g_pointfilename;
extern std::filesystem::path g_linefilename;
extern std::filesystem::path g_bspfilename;
extern std::filesystem::path g_extentfilename;

extern bool g_bUseNullTex;

extern bool g_nohull2;

extern face_t* NewFaceFromFace(face_t const * const in);
extern void SplitFace(
	face_t* in,
	mapplane_t const * const split,
	face_t** front,
	face_t** back
);
