#pragma once

#include "bounding_box.h"
#include "bspfile.h"
#include "cmdlib.h"
#include "cmdlinecfg.h"
#include "filelib.h"
#include "hlassert.h"
#include "hlcsg_settings.h"
#include "hull_size.h"
#include "log.h"
#include "mathlib.h"
#include "messages.h"
#include "planes.h"
#include "scriplib.h"
#include "threads.h"
#include "wadpath.h"
#include "win32fix.h"
#include "winding.h"

#include <deque>
#include <map>
#include <string>

#define DEFAULT_BRUSH_UNION_THRESHOLD 0.0f
#define DEFAULT_TINY_THRESHOLD		  0.0
#define DEFAULT_NOCLIP				  false
#define DEFAULT_ONLYENTS			  false
#define DEFAULT_WADTEXTURES			  true
#define DEFAULT_SKYCLIP				  true

#define DEFAULT_CLIPTYPE clip_simple // clip_legacy //--vluzacn

#define DEFAULT_CLIPNAZI false

#define DEFAULT_WADAUTODETECT \
	true // Already true in settings.cfg, why not here? //seedee

#define DEFAULT_SCALESIZE	   -1.0 // dont scale
#define DEFAULT_RESETLOG	   true
#define DEFAULT_NOLIGHTOPT	   false
#define DEFAULT_NULLIFYTRIGGER true

// AJM: added in
#define UNLESS(a) if (!(a))

struct valve220_vects {
	double3_array UAxis;
	double3_array VAxis;
	std::array<double, 2> shift;
	double rotate;
	std::array<double, 2> scale;
};

struct brush_texture_t {
	valve220_vects vects;
	wad_texture_name name;
};

struct side_t {
	brush_texture_t td;
	bool bevel;
	double planepts[3][3];
};

struct bface_t {
	accurate_winding w;
	mapplane_t* plane;
	bounding_box bounds;
	int planenum;
	int texinfo;
	int contents;
	int backcontents;
	bool used;	// just for face counting
	bool bevel; // used for ExpandBrush
};

// NUM_HULLS should be no larger than MAX_MAP_HULLS
#define NUM_HULLS 4

struct brushhull_t {
	bounding_box bounds;
	std::vector<bface_t> faces;
};

struct brush_t {
	int originalentitynum;
	int originalbrushnum;
	int entitynum;
	int brushnum;

	int firstside;
	int numsides;

	unsigned int
		noclip; // !!!FIXME: this should be a flag bitfield so we can use it
				// for other stuff (ie. is this a detail brush...)
	unsigned int cliphull;
	bool bevel;
	int detaillevel;
	int chopdown; // allow this brush to chop brushes of lower detail level
	int chopup;	  // allow this brush to be chopped by brushes of higher
				  // detail level
	int clipnodedetaillevel;
	int coplanarpriority;
	std::array<std::u8string, NUM_HULLS> hullshapes;

	std::int32_t contents;
	std::array<brushhull_t, NUM_HULLS> hulls;
};

struct hullbrushface_t {
	double3_array normal;
	double3_array point;

	std::int32_t numvertexes;
	double3_array* vertexes;
};

struct hullbrushedge_t {
	double3_array normals[2];
	double3_array point;

	double3_array vertexes[2];
	double3_array delta; // delta has the same direction as
						 // CrossProduct(normals[0],normals[1])
};

struct hullbrushvertex_t {
	double3_array point;
};

struct hullbrush_t {
	int numfaces;
	hullbrushface_t* faces;
	int numedges;
	hullbrushedge_t* edges;
	int numvertexes;
	hullbrushvertex_t* vertexes;
};

struct hullshape_t {
	std::u8string id;
	hullbrush_t** brushes;
	int numbrushes; // must be 0 or 1
	bool disabled;
};

//=============================================================================
// map.c

extern int g_nummapbrushes;
extern brush_t g_mapbrushes[MAX_MAP_BRUSHES];

#define MAX_MAP_SIDES (MAX_MAP_BRUSHES * 6)

extern int g_numbrushsides;
extern side_t g_brushsides[MAX_MAP_SIDES];

extern hullshape_t g_defaulthulls[NUM_HULLS];
extern std::vector<hullshape_t> g_hullshapes;

extern void
LoadMapFile(hlcsg_settings const & settings, char const * const filename);

//=============================================================================
// textures.cpp

typedef std::deque<std::u8string>::iterator WadInclude_i;
extern std::deque<std::u8string>
	g_WadInclude; // List of substrings to wadinclude

extern void WriteMiptex(std::filesystem::path const & bspPath);
extern void LogWadUsage(wadpath_t* currentwad, int nummiptex);
extern int TexinfoForBrushTexture(
	mapplane_t const * const plane,
	brush_texture_t* bt,
	double3_array const & origin
);
extern std::optional<wad_texture_name>
GetTextureByNumber_CSG(int texturenumber);

//=============================================================================
// brush.c

extern brush_t* Brush_LoadEntity(entity_t* ent, int hullnum);
extern contents_t CheckBrushContents(brush_t const * const b);

extern void CreateBrush(int brushnum);
extern void CreateHullShape(
	int entitynum, bool disabled, std::u8string_view id, int defaulthulls
);
extern void InitDefaultHulls();

//=============================================================================
// csg.c

extern bool g_chart;
extern bool g_onlyents;
extern bool g_noclip;
extern bool g_wadtextures;
extern bool g_skyclip;
extern bool g_estimate;
extern char const * g_hullfile;

extern bool g_bUseNullTex;

extern bool g_bClipNazi;

#define EnumPrint(a) #a

typedef enum {
	clip_smallest,
	clip_normalized,
	clip_simple,
	clip_precise,
	clip_legacy
} cliptype;

extern cliptype g_cliptype;
extern char const * GetClipTypeString(cliptype);

extern double g_scalesize;
extern bool g_resetlog;
extern bool g_nolightopt;
extern bool g_nullifytrigger;

extern double g_tiny_threshold;
extern double g_BrushUnionThreshold;

extern std::array<mapplane_t, MAX_INTERNAL_MAP_PLANES> g_mapplanes;
extern int g_nummapplanes;

extern bface_t NewFaceFromFace(bface_t const & in);
extern bface_t CopyFace(bface_t const & f);

extern std::vector<bface_t>
CopyFaceList(std::vector<bface_t> const & faceList);

extern void GetParamsFromEnt(entity_t* mapent);

//=============================================================================
// brushunion.c
void CalculateBrushUnions(int brushnum);

//============================================================================
// hullfile.cpp
extern hull_sizes g_hull_size;
extern void LoadHullfile(char const * filename);

extern std::filesystem::path g_wadcfgfile;
extern std::u8string g_wadconfigname;
extern void LoadWadcfgfile(std::filesystem::path wadCfgPath);
extern void
LoadWadconfig(char const * filename, std::u8string_view configname);

//============================================================================
// autowad.cpp      AJM

extern bool g_bWadAutoDetect;

//=============================================================================
// properties.cpp

#include <set>
#include <string>
extern void properties_initialize(char const * filename);
extern std::set<std::u8string> g_invisible_items;
