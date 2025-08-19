#pragma once

#include "bounding_box.h"
#include "bspfile.h"
#include "csg_types/csg_types.h"
#include "hashing.h"
#include "hlcsg_settings.h"
#include "hull_size.h"
#include "internal_types/internal_types.h"
#include "planes.h"
#include "vector_inplace.h"
#include "wadpath.h"
#include "winding.h"

#include <deque>
#include <string>

#define DEFAULT_TINY_THRESHOLD 0.0
#define DEFAULT_NOCLIP         false
#define DEFAULT_ONLYENTS       false
#define DEFAULT_WADTEXTURES    true
#define DEFAULT_SKYCLIP        true

#define DEFAULT_CLIPTYPE clip_precise

#define DEFAULT_CLIPNAZI false

#define DEFAULT_SCALESIZE      -1.0 // dont scale
#define DEFAULT_RESETLOG       true
#define DEFAULT_NOLIGHTOPT     false
#define DEFAULT_NULLIFYTRIGGER true

struct valve220_vects final {
	double3_array UAxis;
	double3_array VAxis;
	std::array<double, 2> shift;
	std::array<double, 2> scale;
};

namespace std {
	template <>
	struct hash<valve220_vects> {
		constexpr std::size_t operator()(valve220_vects const & vects
		) const noexcept {
			return hash_multiple(
				vects.UAxis, vects.VAxis, vects.shift, vects.scale
			);
		}
	};
} // namespace std

struct brush_texture_t final {
	valve220_vects vects;
	wad_texture_name name;
};

namespace std {
	template <>
	struct hash<brush_texture_t> {
		constexpr std::size_t operator()(brush_texture_t const & bt
		) const noexcept {
			return hash_multiple(bt.vects, bt.name);
		}
	};
} // namespace std

struct side_t final {
	brush_texture_t td;
	bool bevel;
	std::array<double3_array, 3> planepts;
};

namespace std {
	template <>
	struct hash<side_t> {
		constexpr std::size_t operator()(side_t const & s) const noexcept {
			return hash_multiple(s.td, s.bevel, s.planepts);
		}
	};
} // namespace std

struct bface_t final {
	accurate_winding w;
	mapplane_t* plane; // Appears to be non-owning since NewFaceFromFace
	                   // just copies the pointer
	bounding_box bounds;
	std::uint16_t planenum;
	texinfo_count texinfo;
	contents_t contents;
	contents_t backcontents;
	bool used;  // just for face counting
	bool bevel; // used for expand_brush
};

namespace std {
	template <>
	struct hash<bface_t> {
		constexpr std::size_t operator()(bface_t const & bFace
		) const noexcept {
			return hash_multiple(
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
	};
} // namespace std

struct brushhull_t final {
	bounding_box bounds;
	std::vector<bface_t> faces;
};

namespace std {
	template <>
	struct hash<brushhull_t> {
		constexpr std::size_t operator()(brushhull_t const & brushHull
		) const noexcept {
			return hash_multiple(brushHull.bounds, brushHull.faces);
		}
	};
} // namespace std

using cliphull_bitmask = std::uint8_t;
static_assert(std::numeric_limits<cliphull_bitmask>::digits >= NUM_HULLS);

struct csg_brush
	final { // TODO: Rename this, since we have a csg_brush in HLBSP too
	entity_count entitynum;
	// Same as entitynum except if entities are removed/added during the
	// compilation process. Used for helpful error messages
	entity_count originalentitynum;

	// Entity-local brushnum. The first brush of every entity has brushnum
	// 0, the second has brushnum 1 and so on
	// TODO: Rename
	entity_local_brush_count brushnum;
	// Same as brushnum except if brushes are removed/added during the
	// compilation process. Used for helpful error messages
	entity_local_brush_count originalbrushnum;

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

namespace std {
	template <>
	struct hash<csg_brush> {
		constexpr std::size_t operator()(csg_brush const & brush
		) const noexcept {
			return hash_multiple(
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
	};
} // namespace std

struct hullbrushface_t final {
	double3_array normal;
	double3_array point;

	std::vector<double3_array> vertices;
};

struct hullbrushedge_t final {
	double3_array normals[2];
	double3_array point;

	double3_array vertexes[2];
	double3_array delta; // delta has the same direction as
	                     // cross_product(normals[0], normals[1])
};

struct hullbrushvertex_t final {
	double3_array point;
};

struct hullbrush_t final {
	std::vector<hullbrushface_t> faces;
	std::vector<hullbrushedge_t> edges;
	std::vector<hullbrushvertex_t> vertices;
};

struct hullshape_t final {
	std::u8string id;
	std::optional<hullbrush_t> hullBrush;
	bool disabled;
};

//=============================================================================
// map.c

extern int g_nummapbrushes;
extern csg_brush g_mapbrushes[MAX_MAP_BRUSHES];

extern side_t g_brushsides[MAX_MAP_SIDES];

extern void
LoadMapFile(hlcsg_settings const & settings, char const * const filename);

//=============================================================================
// textures.cpp

using WadInclude_i = std::deque<std::u8string>::iterator;
extern std::deque<std::u8string>
	g_WadInclude; // List of substrings to wadinclude

extern void WriteMiptex(std::filesystem::path const & bspPath);
extern void LogWadUsage(wadpath_t* currentwad, int nummiptex);
extern texinfo_count TexinfoForBrushTexture(
	mapplane_t const * const plane,
	brush_texture_t* bt,
	double3_array const & origin
);
extern std::optional<wad_texture_name>
GetTextureByNumber_CSG(texinfo_count texturenumber);

//=============================================================================
// brush.c

extern csg_brush* Brush_LoadEntity(entity_t* ent, int hullnum);
extern contents_t CheckBrushContents(csg_brush const * const b);

extern void create_brush(csg_brush& b, csg_entity const & ent);
extern void create_hullshape(csg_entity const & fromInfoHullshapeEntity);
extern void init_default_hulls();

//=============================================================================
// csg.c

extern bool g_chart;
extern bool g_onlyents;
extern bool g_noclip;
extern bool g_wadtextures;
extern bool g_skyclip;
extern bool g_estimate;

extern bool g_bUseNullTex;

extern bool g_bClipNazi;

#define EnumPrint(a) #a

enum cliptype {
	clip_precise,
	clip_normalized,
	clip_legacy
};

extern cliptype g_cliptype;
extern char const * GetClipTypeString(cliptype);

extern double g_scalesize;
extern bool g_resetlog;
extern bool g_nolightopt;
extern bool g_nullifytrigger;

extern double g_tiny_threshold;

extern vector_inplace<mapplane_t, MAX_INTERNAL_MAP_PLANES> g_mapPlanes;

extern bface_t NewFaceFromFace(bface_t const & in);
extern bface_t CopyFace(bface_t const & f);

extern std::vector<bface_t>
CopyFaceList(std::vector<bface_t> const & faceList);

extern void GetParamsFromEnt(entity_t* mapent);

//============================================================================
// hullfile.cpp
extern hull_sizes g_hull_size;
void LoadHullfile(std::filesystem::path filePath);

//=============================================================================
// properties.cpp

#include <set>
#include <string>
extern void properties_initialize(std::filesystem::path filePath);
extern std::set<std::u8string> g_invisible_items;
