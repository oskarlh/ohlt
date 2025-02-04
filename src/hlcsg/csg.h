#pragma once

#include "bounding_box.h"
#include "bspfile.h"
#include "cmdlib.h"
#include "cmdlinecfg.h"
#include "filelib.h"
#include "hlassert.h"
#include "hlcsg_settings.h"
#include "hull_size.h"
#include "internal_types.h"
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

#define DEFAULT_SCALESIZE	   -1.0 // dont scale
#define DEFAULT_RESETLOG	   true
#define DEFAULT_NOLIGHTOPT	   false
#define DEFAULT_NULLIFYTRIGGER true

struct valve220_vects {
	double3_array UAxis;
	double3_array VAxis;
	std::array<double, 2> shift;
	std::array<double, 2> scale;
};

template <>
inline std::uint32_t fast_checksum(valve220_vects const & ent) noexcept {
	return fast_checksum(ent.UAxis, ent.VAxis, ent.shift, ent.scale);
}

// TODO: Move to an appropriate location
template <>
inline std::uint32_t fast_checksum(wad_texture_name const & ent) noexcept {
	return fast_checksum(ent.string_view());
}

struct brush_texture_t {
	valve220_vects vects;
	wad_texture_name name;
};

template <>
inline std::uint32_t fast_checksum(brush_texture_t const & ent) noexcept {
	return fast_checksum(ent.vects, ent.name);
}

struct side_t {
	brush_texture_t td;
	bool bevel;
	std::array<double3_array, 3> planepts;
};

template <>
inline std::uint32_t fast_checksum(side_t const & ent) noexcept {
	return fast_checksum(ent.td, ent.bevel, ent.planepts);
}

struct bface_t {
	accurate_winding w;
	mapplane_t* plane;
	bounding_box bounds;
	std::uint16_t planenum;
	std::int16_t texinfo;
	contents_t contents;
	contents_t backcontents;
	bool used;	// just for face counting
	bool bevel; // used for ExpandBrush
};

template <>
inline std::uint32_t fast_checksum(bface_t const & bFace) noexcept {
	return fast_checksum(
		bFace.w,
		bFace.plane,
		bFace.bounds,
		bFace.planenum,
		bFace.texinfo,
		bFace.contents,
		bFace.backcontents,
		bFace.used,
		bFace.bevel
	);
}

struct brushhull_t {
	bounding_box bounds;
	std::vector<bface_t> faces;
};

template <>
inline std::uint32_t fast_checksum(brushhull_t const & brushHull) noexcept {
	return fast_checksum(brushHull.bounds, brushHull.faces);
}

using cliphull_bitmask = std::uint8_t;
static_assert(std::numeric_limits<cliphull_bitmask>::digits >= NUM_HULLS);

struct brush_t { // TODO: Rename this, since we have a brush_t in HLBSP too
	int entitynum;
	// Same as entitynum except if entities are removed/added during the
	// compilation process. Used for helpful error messages
	int originalentitynum;

	// Entity-local brushnum. The first brush of every entity has brushnum
	// 0, the second has brushnum 1 and so on
	// TODO: Rename
	int brushnum;
	// Same as brushnum except if brushes are removed/added during the
	// compilation process. Used for helpful error messages
	int originalbrushnum;

	side_count firstSide;
	side_count numSides;

	cliphull_bitmask cliphull;

	bool noclip;
	bool bevel;
	detail_level detailLevel;
	detail_level
		chopDown; // Allow this brush to chop brushes of lower detail level
	detail_level chopUp; // Allow this brush to be chopped by brushes of
						 // higher detail level
	detail_level clipNodeDetailLevel;
	coplanar_priority coplanarPriority;
	std::array<std::u8string, NUM_HULLS> hullshapes;

	contents_t contents;
	std::array<brushhull_t, NUM_HULLS> hulls;
};

template <>
inline std::uint32_t fast_checksum(brush_t const & brush) noexcept {
	return fast_checksum(
		brush.entitynum,
		brush.originalentitynum,
		brush.brushnum,
		brush.originalbrushnum,
		brush.firstSide,
		brush.numSides,
		brush.cliphull,
		brush.noclip,
		brush.bevel,
		brush.detailLevel,
		brush.chopDown,
		brush.chopUp,
		brush.clipNodeDetailLevel,
		brush.coplanarPriority,
		brush.hullshapes,
		brush.contents,
		brush.hulls
	);
}

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
						 // cross_product(normals[0], normals[1])
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

extern int g_numbrushsides;
extern side_t g_brushsides[MAX_MAP_SIDES];

extern hullshape_t g_defaulthulls[NUM_HULLS];
extern std::vector<hullshape_t> g_hullshapes;

extern void
LoadMapFile(hlcsg_settings const & settings, char const * const filename);

//=============================================================================
// textures.cpp

using WadInclude_i = std::deque<std::u8string>::iterator;
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

enum cliptype {
	clip_smallest,
	clip_normalized,
	clip_simple,
	clip_precise,
	clip_legacy
};

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

//=============================================================================
// properties.cpp

#include <set>
#include <string>
extern void properties_initialize(char const * filename);
extern std::set<std::u8string> g_invisible_items;
