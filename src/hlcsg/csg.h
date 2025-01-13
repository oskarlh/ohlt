#pragma once

#include <deque>
#include <string>
#include <map>

#include "cmdlib.h"
#include "messages.h"
#include "win32fix.h"
#include "log.h"
#include "hlassert.h"
#include "mathlib.h"
#include "scriplib.h"
#include "winding.h"
#include "threads.h"
#include "bspfile.h"
#include "blockmem.h"
#include "filelib.h"
#include "bounding_box.h"
#include "hull_size.h"
#include "wadpath.h"
#include "cmdlinecfg.h"
#include "planes.h"

#define DEFAULT_BRUSH_UNION_THRESHOLD 0.0f
#define DEFAULT_TINY_THRESHOLD        0.0
#define DEFAULT_NOCLIP      false
#define DEFAULT_ONLYENTS    false
#define DEFAULT_WADTEXTURES true
#define DEFAULT_SKYCLIP     true

#define DEFAULT_CLIPTYPE clip_simple //clip_legacy //--vluzacn

#define DEFAULT_CLIPNAZI    false

#define DEFAULT_WADAUTODETECT true //Already true in settings.cfg, why not here? //seedee


#define DEFAULT_SCALESIZE -1.0 //dont scale
#define DEFAULT_RESETLOG true
#define DEFAULT_NOLIGHTOPT false
#ifdef HLCSG_GAMETEXTMESSAGE_UTF8
#define DEFAULT_NOUTF8 false
#endif
#define DEFAULT_NULLIFYTRIGGER true

// AJM: added in
#define UNLESS(a)  if (!(a))


struct valve_vects
{
    vec3_array UAxis;
    vec3_array VAxis;
    vec_t shift[2];
    vec_t rotate;
    vec_t scale[2];
};

struct quark_vects {
    std::array<std::array<float, 4>, 2> vects;
};

typedef union
{
    valve_vects     valve;
    quark_vects     quark;
}
vects_union;

extern int      g_nMapFileVersion;                         // map file version * 100 (ie 201), zero for pre-Worldcraft 2.0.1 maps

typedef struct
{
    char            txcommand;
    vects_union     vects;
    wad_texture_name name;
} brush_texture_t;

typedef struct side_s
{
    brush_texture_t td;
	bool			bevel;
    vec_t           planepts[3][3];
} side_t;

struct bface_t {
    Winding w{};
    mapplane_t* plane{};
    bounding_box bounds{};
    int planenum{};
    int texinfo{};
    int contents{};
    int backcontents{};
    bool used{}; // just for face counting
	bool bevel{}; //used for ExpandBrush
};

// NUM_HULLS should be no larger than MAX_MAP_HULLS
#define NUM_HULLS 4

struct brushhull_t {
    bounding_box     bounds;
    std::vector<bface_t> faces;
};

struct brush_t {
	int				originalentitynum;
	int				originalbrushnum;
    int             entitynum;
    int             brushnum;

    int             firstside;
    int             numsides;

    unsigned int    noclip; // !!!FIXME: this should be a flag bitfield so we can use it for other stuff (ie. is this a detail brush...)
	unsigned int	cliphull;
	bool			bevel;
	int				detaillevel;
	int				chopdown; // allow this brush to chop brushes of lower detail level
	int				chopup; // allow this brush to be chopped by brushes of higher detail level
	int				clipnodedetaillevel;
	int				coplanarpriority;
	char8_t* hullshapes[NUM_HULLS]; // might be NULL

    std::int32_t contents;
    brushhull_t hulls[NUM_HULLS];
};

struct hullbrushface_t {
	vec3_array normal;
	vec3_array point;

	std::int32_t numvertexes;
	vec3_array *vertexes;
};

struct hullbrushedge_t{
	vec3_array normals[2];
	vec3_array point;

	vec3_array vertexes[2];
	vec3_array delta; // delta has the same direction as CrossProduct(normals[0],normals[1])
};

struct hullbrushvertex_t {
	vec3_array point;
};

struct hullbrush_t {
	int numfaces;
	hullbrushface_t *faces;
	int numedges;
	hullbrushedge_t *edges;
	int numvertexes;
	hullbrushvertex_t *vertexes;
};

struct hullshape_t {
	std::u8string id;
	hullbrush_t **brushes;
	int numbrushes; // must be 0 or 1
	bool disabled;
};

#ifdef HLCSG_GAMETEXTMESSAGE_UTF8
std::u8string ansiToUtf8(std::string_view ansiString);
#endif

//=============================================================================
// map.c

extern int      g_nummapbrushes;
extern brush_t  g_mapbrushes[MAX_MAP_BRUSHES];

#define MAX_MAP_SIDES   (MAX_MAP_BRUSHES*6)

extern int      g_numbrushsides;
extern side_t   g_brushsides[MAX_MAP_SIDES];

extern hullshape_t g_defaulthulls[NUM_HULLS];
extern std::vector<hullshape_t> g_hullshapes;

extern void     TextureAxisFromPlane(const mapplane_t* const pln, vec3_t xv, vec3_t yv);
extern void     LoadMapFile(const char* const filename);

//=============================================================================
// textures.cpp

typedef std::deque<std::u8string>::iterator WadInclude_i;
extern std::deque<std::u8string> g_WadInclude;  // List of substrings to wadinclude

extern void     WriteMiptex(const std::filesystem::path& bspPath);
extern void     LogWadUsage(wadpath_t* currentwad, int nummiptex);
extern int      TexinfoForBrushTexture(const mapplane_t* const plane, brush_texture_t* bt, const vec3_t origin
					);
extern std::optional<wad_texture_name> GetTextureByNumber_CSG(int texturenumber);

//=============================================================================
// brush.c

extern brush_t* Brush_LoadEntity(entity_t* ent, int hullnum);
extern contents_t CheckBrushContents(const brush_t* const b);

extern void CreateBrush(int brushnum);
extern void CreateHullShape (int entitynum, bool disabled, std::u8string_view id, int defaulthulls);
extern void InitDefaultHulls ();

//=============================================================================
// csg.c

extern bool g_chart;
extern bool g_onlyents;
extern bool g_noclip;
extern bool g_wadtextures;
extern bool g_skyclip;
extern bool g_estimate;         
extern const char* g_hullfile;        

extern bool g_bUseNullTex; 


extern bool g_bClipNazi; 

#define EnumPrint(a) #a
typedef enum{clip_smallest,clip_normalized,clip_simple,clip_precise,clip_legacy} cliptype;
extern cliptype g_cliptype;
extern const char* GetClipTypeString(cliptype);

extern vec_t g_scalesize;
extern bool g_resetlog;
extern bool g_nolightopt;
#ifdef HLCSG_GAMETEXTMESSAGE_UTF8
extern bool g_noutf8;
#endif
extern bool g_nullifytrigger;

extern vec_t g_tiny_threshold;
extern vec_t g_BrushUnionThreshold;

extern std::array<mapplane_t, MAX_INTERNAL_MAP_PLANES> g_mapplanes;
extern int g_nummapplanes;

extern bface_t NewFaceFromFace(const bface_t& in);
extern bface_t CopyFace(const bface_t& f);

extern std::vector<bface_t> CopyFaceList(const std::vector<bface_t>& faceList);

extern void GetParamsFromEnt(entity_t* mapent);


//=============================================================================
// brushunion.c
void CalculateBrushUnions(int brushnum);
 
//============================================================================
// hullfile.cpp
extern hull_sizes g_hull_size;
extern void LoadHullfile(const char* filename);

extern const char *g_wadcfgfile;
extern const char *g_wadconfigname;
extern void LoadWadcfgfile (const char *filename);
extern void LoadWadconfig (const char *filename, const char *configname);

//============================================================================
// autowad.cpp      AJM

extern bool g_bWadAutoDetect; 


//=============================================================================
// properties.cpp

#include <string>
#include <set>
extern void properties_initialize(const char* filename);
extern std::set< std::string > g_invisible_items;
