#pragma once

#include "bspfile.h"
#include "compress.h"
#include "mathlib.h"
#include "wad_texture_name.h"
#include "winding.h"

#include <vector>

#define DEFAULT_FASTMODE      false
#define DEFAULT_LERP_ENABLED  true
#define DEFAULT_STUDIOSHADOW  true
#define DEFAULT_FADE          1.0
#define DEFAULT_BOUNCE        8
#define DEFAULT_AMBIENT_RED   0.0
#define DEFAULT_AMBIENT_GREEN 0.0
#define DEFAULT_AMBIENT_BLUE  0.0

// Lightmap pixels with a component value above 188 is clamped down.
// Hopefully one day Valve will fix gl_overbright and we can use up to 255.
constexpr int8_color_element DEFAULT_LIMITTHRESHOLD = 188;

#define DEFAULT_TEXSCALE         true
#define DEFAULT_CHOP             64.0
#define DEFAULT_TEXCHOP          32.0
#define DEFAULT_DLIGHT_THRESHOLD 10.0
#define DEFAULT_SMOOTHING_VALUE  50.0
#define DEFAULT_SMOOTHING2_VALUE 0
#define DEFAULT_INCREMENTAL      false

#define DEFAULT_INDIRECT_SUN     1.0
#define DEFAULT_EXTRA            false
#define DEFAULT_SKY_LIGHTING_FIX true
#define DEFAULT_CIRCUS           false
#define DEFAULT_CORING           0.01
#define DEFAULT_SUBDIVIDE        true
#define DEFAULT_ALLOW_OPAQUES    true
#define DEFAULT_ALLOW_SPREAD     true

// TODO: Increase these
#define DEFAULT_LIGHTING_GAMMA 0.55

#define DEFAULT_LIGHTING_SCALE 2.0 // 1.0 //vluzacn

// [http://hullu.xtragaming.com/] Transparency light support for bounced
// light(transfers) is extremely slow for 'vismatrix' and 'sparse' at the
// moment. Only recommended to be used with 'nomatrix' mode
#define DEFAULT_CUSTOMSHADOW_WITH_BOUNCELIGHT false

// RGB Transfers support for HLRAD .. to be used with
// -customshadowwithbounce
#define DEFAULT_RGB_TRANSFERS false
// o_O ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

#define DEFAULT_TRANSTOTAL_HACK  0.2 // 0.5 //vluzacn
#define DEFAULT_SOFTSKY          true
#define DEFAULT_TRANSLUCENTDEPTH 2.0f
#define DEFAULT_NOTEXTURES       false
#define DEFAULT_TEXREFLECTGAMMA \
	1.76f // 2.0(texgamma cvar) / 2.5 (gamma cvar) * 2.2 (screen gamma)
	      // = 1.76
#define DEFAULT_TEXREFLECTSCALE \
	0.7f // arbitrary (This is lower than 1.0, because textures are usually
	     // brightened in order to look better in Goldsrc. Textures are made
	     // brightened because Goldsrc is only able to darken the texture
	     // when combining the texture with the lightmap.)
#define DEFAULT_BLUR                      1.5 // classic lighting is equivalent to "-blur 1.0"
#define DEFAULT_NOEMITTERRANGE            false
#define DEFAULT_BLEEDFIX                  true
#define DEFAULT_EMBEDLIGHTMAP_POWEROFTWO  true
#define DEFAULT_EMBEDLIGHTMAP_DENOMINATOR 188.0
#define DEFAULT_EMBEDLIGHTMAP_GAMMA       1.05
#define DEFAULT_EMBEDLIGHTMAP_RESOLUTION  1
#define DEFAULT_TEXLIGHTGAP               0.0

// Ideally matches what is in the FGD :)
#define SPAWNFLAG_NOBLEEDADJUST (1 << 0)

// DEFAULT_HUNT_OFFSET is how many units in front of the plane to place the
// samples Unit of '1' causes the 1 unit crate trick to cause extra shadows
constexpr float DEFAULT_HUNT_OFFSET = 0.5;
// DEFAULT_HUNT_SIZE number of iterations (one based) of radial search in
// HuntForWorld
#define DEFAULT_HUNT_SIZE 11
// DEFAULT_HUNT_SCALE amount to grow from origin point per iteration of
// DEFAULT_HUNT_SIZE in HuntForWorld
#define DEFAULT_HUNT_SCALE 0.1
#define DEFAULT_EDGE_WIDTH 0.8

constexpr float PATCH_HUNT_OFFSET = 0.5;
#define HUNT_WALL_EPSILON \
	(3 * ON_EPSILON) // place sample at least this distance away from any
	                 // wall //--vluzacn

#define MINIMUM_PATCH_DISTANCE ON_EPSILON
#define ACCURATEBOUNCE_THRESHOLD \
	4.0 // If the receiver patch is closer to emitter patch than
	    // EXACTBOUNCE_THRESHOLD * emitter_patch->radius, calculate the
	    // exact visibility amount.
#define ACCURATEBOUNCE_DEFAULT_SKYLEVEL 5 // sample 1026 normals

#define ALLSTYLES 64 // HL limit. //--vluzacn

constexpr float hlrad_bogus_range = 131'072;

struct matrix_t final {
	std::array<float3_array, 4> v;
};

// a 4x4 matrix that represents the following transformation (see the
// ApplyMatrix function)
//
//  / X \    / v[0][0] v[1][0] v[2][0] v[3][0] \ / X \.
//  | Y | -> | v[0][1] v[1][1] v[2][1] v[3][1] | | Y |
//  | Z |    | v[0][2] v[1][2] v[2][2] v[3][2] | | Z |
//  \ 1 /    \    0       0       0       1    / \ 1 /

//
// LIGHTMAP.C STUFF
//

struct transfer_index_t final {
	std::uint32_t size : 12;
	std::uint32_t index : 20;
};

using transfer_raw_index_t = std::uint32_t;
using transfer_data_t = unsigned char;

using rgb_transfer_data_t = unsigned char;

#define MAX_COMPRESSED_TRANSFER_INDEX_SIZE ((1 << 12) - 1)

#define MAX_PATCHES                  (65535 * 16) // limited by transfer_index_t
#define MAX_VISMATRIX_PATCHES        65535
#define MAX_SPARSE_VISMATRIX_PATCHES MAX_PATCHES

enum ePatchFlags {
	ePatchFlagNull = 0,
	ePatchFlagOutside = 1
};

struct patch_t final {
	patch_t* next;       // next in face
	float3_array origin; // Center centroid of winding (cached info
	                     // calculated from winding)
	float area; // Surface area of this patch (cached info calculated from
	            // winding)
	float exposure;
	float emitter_range;  // Range from patch origin (cached info calculated
	                      // from winding)
	int emitter_skylevel; // The "skylevel" used for sampling of normals,
	                      // when the receiver patch is within the range of
	                      // ACCURATEBOUNCE_THRESHOLD * this->radius.
	                      // (cached info calculated from winding)
	fast_winding*
		winding; // fast_winding (patches are triangles, so its easy)
	float scale; // Texture scale for this face (blend of S and T scale)
	float chop;  // Texture chop for this face factoring in S and T scale

	unsigned iIndex;
	unsigned iData;

	transfer_index_t* tIndex;
	transfer_data_t* tData;
	rgb_transfer_data_t* tRGBData;

	int faceNumber;
	ePatchFlags flags;
	bool translucent_b; // gather light from behind
	float3_array translucent_v;
	float3_array texturereflectivity;
	float3_array bouncereflectivity;

	unsigned char totalstyle[MAXLIGHTMAPS];
	unsigned char directstyle[MAXLIGHTMAPS];
	// HLRAD_AUTOCORING: totallight: all light gathered by patch
	float3_array totallight[MAXLIGHTMAPS]; // accumulated by radiosity does
	                                       // NOT include light accounted
	                                       // for by direct lighting
	// HLRAD_AUTOCORING: directlight: emissive light gathered by sample
	float3_array directlight[MAXLIGHTMAPS]; // direct light only
	int bouncestyle; // light reflected from this patch must convert to this
	                 // style. -1 = normal (don't convert)
	unsigned char emitstyle;
	float3_array baselight; // emissivity only, uses emitstyle
	bool emitmode;          // texlight emit mode. 1 for normal, 0 for fast.
	float samples;
	// TODO: Create a single struct for these and allocate everything at
	// once
	std::array<float3_array, ALLSTYLES>*
		samplelight_all; // NULL except during BuildFacelights
	std::array<unsigned char, ALLSTYLES>*
		totalstyle_all; // NULL except during BuildFacelights
	std::array<float3_array, ALLSTYLES>*
		totallight_all; // NULL except during BuildFacelights
	std::array<float3_array, ALLSTYLES>*
		directlight_all; // NULL except during BuildFacelights
	int leafnum;
};

enum emittype_t {
	emit_surface,
	emit_point,
	emit_spotlight,
	emit_skylight
};

struct directlight_t final {
	directlight_t* next;
	emittype_t type;
	int style;
	float3_array origin;
	float3_array intensity;
	float3_array normal; // for surfaces and spotlights
	float stopdot;       // for spotlights
	float stopdot2;      // for spotlights

	// 'Arghrad'-like features
	float fade; // Falloff scaling for inverse square falloff 1.0
	            // = normal, 0.5 = farther, 2.0 = shorter etc

	// -----------------------------------------------------------------------------------
	// Changes by Adam Foster - afoster@compsoc.man.ac.uk
	// Diffuse light_environment light colour
	// Really horrible hack which probably won't work!
	float3_array diffuse_intensity;
	// -----------------------------------------------------------------------------------
	float3_array diffuse_intensity2;
	float sunspreadangle;
	int numsunnormals;
	float3_array* sunnormals;
	float* sunnormalweights;

	float patch_area;
	float patch_emitter_range;
	patch_t* patch;
	float texlightgap;
	bool topatch;
};

// LRC
float3_array get_total_light(patch_t const & patch, int style) noexcept;

struct facelist_t final {
	dface_t* face;
	facelist_t* next;
};

struct edgeshare_t final {
	dface_t* faces[2];
	float3_array interface_normal; // HLRAD_GetPhongNormal_VL: this field
	                               // must be set when smooth==true
	float3_array vertex_normal[2];
	float cos_normals_angle; // HLRAD_GetPhongNormal_VL: this field must be
	                         // set when smooth==true
	bool coplanar;
	bool smooth;
	facelist_t* vertex_facelist[2]; // possible smooth faces, not include
	                                // faces[0] and faces[1]
	matrix_t textotex[2]; // how we translate texture coordinates from one
	                      // face to the other face
};

extern std::array<edgeshare_t, MAX_MAP_EDGES> g_edgeshare;

//
// lerp.c stuff
//

// These are bitflags for lighting adjustments for special cases
enum eModelLightmodes {
	eModelLightmodeNull = 0,
	eModelLightmodeOpaque = 0x02,
	eModelLightmodeNonsolid = 0x08, // for opaque entities with {texture
};

struct opaqueList_t final {
	int entitynum;
	int modelnum;
	float3_array origin;

	float3_array transparency_scale;
	bool transparency;
	int style; // -1 = no style; transparency must be false if style >= 0
	// style0 and same style will change to this style, other styles will be
	// blocked.
	bool block; // this entity can't be seen inside, so all lightmap sample
	            // should move outside.
};

struct radtexture_t final {
	wad_texture_name name; // Not always same with the name in texdata
	std::int32_t width;
	std::int32_t height;
	std::unique_ptr<std::uint8_t[]> canvas; // [height][width]
	std::array<std::array<std::uint8_t, 3>, 256> palette;
	float3_array reflectivity;
};

extern int g_numtextures;
extern std::unique_ptr<radtexture_t[]> g_textures;
extern void AddWadFolder(std::filesystem::path);
extern void LoadTextures();
extern void EmbedLightmapInTextures();

struct minlight_t final {
	wad_texture_name name;
	float value;
}; // info_minlights

using minlight_i = std::vector<minlight_t>::iterator;

//
// HLRAD globals
//

extern std::vector<minlight_t> s_minlights;
extern std::array<patch_t*, MAX_MAP_FACES> g_face_patches;
extern entity_t* g_face_entity[MAX_MAP_FACES];
extern float3_array g_face_offset[MAX_MAP_FACES]; // for models with origins
extern eModelLightmodes g_face_lightmode[MAX_MAP_FACES];
extern std::array<float3_array, MAX_MAP_EDGES> g_face_centroids;
extern entity_t* g_face_texlights[MAX_MAP_FACES];
extern std::vector<patch_t> g_patches;

extern float g_dlight_threshold;
extern float g_coring;
extern int g_lerp_enabled;

extern void MakeShadowSplits();

//==============================================

extern bool g_fastmode;
extern bool g_extra;
extern float3_array g_ambient;
extern int8_color_element g_limitthreshold;
extern bool g_drawoverload;
extern unsigned g_numbounce;
extern float g_qgamma;
extern float g_indirect_sun;
extern float g_smoothing_threshold;
extern float g_smoothing_value;
extern float g_smoothing_threshold_2;
extern float g_smoothing_value_2;
extern float* g_smoothvalues; //[nummiptex]
extern bool g_estimate;
extern float g_fade;
extern bool g_incremental;
extern bool g_circus;
extern bool g_allow_spread;
extern bool g_sky_lighting_fix;
extern float g_chop;    // Chop value for normal textures
extern float g_texchop; // Chop value for texture lights
extern std::vector<opaqueList_t> g_opaque_face_list;

extern float g_lighting_gamma;
extern float g_lighting_scale;

struct lighting_cone_power_and_scale final {
	float power{ 1.0 };
	float scale{ 1.0 };
};

extern bool g_customshadow_with_bouncelight;
extern bool g_rgb_transfers;

extern float g_transtotal_hack;
extern float_color_element g_minlight;
extern float_type g_transfer_compress_type;
extern vector_type g_rgbtransfer_compress_type;
extern bool g_softsky;
extern bool g_blockopaque;
extern bool g_drawpatch;
extern bool g_drawsample;
extern float3_array g_drawsample_origin;
extern float g_drawsample_radius;
extern bool g_drawedge;
extern bool g_drawlerp;
extern bool g_drawnudge;
extern float g_corings[ALLSTYLES];
extern int stylewarningcount; // not thread safe
extern int stylewarningnext;  // not thread safe
extern std::unique_ptr<float3_array[]> g_translucenttextures;
extern float g_translucentdepth;
extern std::unique_ptr<lighting_cone_power_and_scale[]>
	g_lightingconeinfo; // size == nummiptex
extern bool g_notextures;
extern float g_texreflectgamma;
extern float g_texreflectscale;
extern float g_blur;
extern bool g_noemitterrange;
extern bool g_bleedfix;
extern float g_maxdiscardedlight;
extern float3_array g_maxdiscardedpos;
extern float g_texlightgap;

extern void MakeTnodes();
extern void PairEdges();
#define SKYLEVELMAX         8
#define SKYLEVEL_SOFTSKYON  7
#define SKYLEVEL_SOFTSKYOFF 4
#define SUNSPREAD_SKYLEVEL  7
constexpr float SUNSPREAD_THRESHOLD = 15.0f;
extern int g_numskynormals[SKYLEVELMAX + 1]; // 0, 6, 18, 66, 258, 1026,
                                             // 4098, 16386, 65538
extern float3_array* g_skynormals[SKYLEVELMAX + 1]; //[numskynormals]
extern float*
	g_skynormalsizes[SKYLEVELMAX + 1]; // the weight of each normal
extern void BuildDiffuseNormals();
extern void BuildFacelights(int facenum);
extern void PrecompLightmapOffsets();
extern void ReduceLightmap();
extern void FinalLightFace(int facenum);
extern void CreateFacelightDependencyList(); // run before AddPatchLights
extern void AddPatchLights(int facenum);
extern void FreeFacelightDependencyList();
extern contents_t TestLine(
	float3_array const & start,
	float3_array const & stop,
	float3_array& skyhitout
);

inline contents_t
TestLine(float3_array const & start, float3_array const & stop) {
	float3_array skyhitout;
	return TestLine(start, stop, skyhitout);
}

extern std::array<float3_array, 15> const pos;

enum class vis_method {
	vismatrix,
	sparse_vismatrix,
	no_vismatrix
};

struct opaquemodel_t final {
	float3_array mins, maxs;
	int headnode;
};

extern void CreateOpaqueNodes();
extern int TestLineOpaque(
	int modelnum,
	float3_array const & modelorigin,
	float3_array const & start,
	float3_array const & stop
);
extern int CountOpaqueFaces(int modelnum);
extern void DeleteOpaqueNodes();
extern bool TestPointOpaque(
	int modelnum,
	float3_array const & modelorigin,
	bool solid,
	float3_array const & point
);

extern void CreateDirectLights();
extern void DeleteDirectLights();
extern void GetPhongNormal(
	int facenum, float3_array const & spot, float3_array& phongnormal
); // added "const" --vluzacn

using funcCheckVisBit = bool (*)(
	unsigned,
	unsigned,
	float3_array&,
	unsigned int&,
	std::vector<float3_array> const & transparencyList
);
extern funcCheckVisBit g_CheckVisBit;
extern std::vector<float3_array> g_transparencyList;
extern bool CheckVisBitBackwards(
	unsigned receiver,
	unsigned emitter,
	float3_array const & backorigin,
	float3_array const & backnormal,
	float3_array& transparency_out
);
extern void MdlLightHack(void);

// hlradutil.c
extern float PatchPlaneDist(patch_t const * const patch);
extern dleaf_t* PointInLeaf(float3_array const & point);
extern void MakeBackplanes();
extern dplane_t const * getPlaneFromFace(dface_t const * const face);
extern dplane_t const * getPlaneFromFaceNumber(unsigned int facenum);
extern void
getAdjustedPlaneFromFaceNumber(unsigned int facenum, dplane_t* plane);
extern dleaf_t* HuntForWorld(
	float3_array& point,
	float3_array const & plane_offset,
	dplane_t const * plane,
	int hunt_size,
	float hunt_scale,
	float hunt_offset
);

// apply_matrix: (x y z 1)T -> matrix * (x y z 1)T
constexpr float3_array
apply_matrix(matrix_t const & m, float3_array const & in) noexcept {
	float3_array out{ m.v[3] };
	for (std::size_t i = 0; i < 3; ++i) {
		out = vector_fma(m.v[i], in[i], out);
	}
	return out;
}

extern void ApplyMatrixOnPlane(
	matrix_t const & m_inverse,
	float3_array const & in_normal,
	float in_dist,
	float3_array& out_normal,
	float& out_dist
);
extern matrix_t
MultiplyMatrix(matrix_t const & m_left, matrix_t const & m_right) noexcept;
extern matrix_t
MatrixForScale(float3_array const & center, float scale) noexcept;
extern float CalcMatrixSign(matrix_t const & m);
extern void TranslateWorldToTex(int facenum, matrix_t& m);
extern bool InvertMatrix(matrix_t const & m, matrix_t& m_inverse);
extern void FindFacePositions(int facenum);
extern void FreePositionMaps();
extern bool FindNearestPosition(
	int facenum,
	fast_winding const * texwinding,
	dplane_t const & texplane,
	float s,
	float t,
	float3_array& pos,
	float* best_s,
	float* best_t,
	float* best_dist,
	bool* nudged
);

// makescales.c
extern void MakeScalesVismatrix();
extern void MakeScalesSparseVismatrix();
extern void MakeScalesNoVismatrix();

// transfers.c
extern size_t g_total_transfer;
extern bool readtransfers(char const * const transferfile, long numpatches);
extern void
writetransfers(char const * const transferfile, long total_patches);

// vismatrixutil.c (shared between vismatrix.c and sparse.c)
extern void MakeScales(int threadnum);
extern void DumpTransfersMemoryUsage();
extern void MakeRGBScales(int threadnum);

// transparency.c (transparency array functions - shared between vismatrix.c
// and sparse.c)
extern void GetTransparency(
	unsigned const p1,
	unsigned const p2,
	float3_array& trans,
	unsigned int& next_index,
	std::vector<float3_array> const & transparencyList
);
extern void AddTransparencyToRawArray(
	unsigned const p1,
	unsigned const p2,
	float3_array const & trans,
	std::vector<float3_array>& transparencyList
);
extern void CreateFinalTransparencyArrays(
	char const * print_name, std::vector<float3_array>& transparencyList
);
extern void FreeTransparencyArrays();
extern void GetStyle(
	unsigned const p1,
	unsigned const p2,
	int& style,
	unsigned int& next_index
);
extern void
AddStyleToStyleArray(unsigned const p1, unsigned const p2, int const style);
extern void CreateFinalStyleArrays(char const * print_name);
extern void FreeStyleArrays();

// lerp.c
extern void CreateTriangulations(int facenum);
extern void
GetTriangulationPatches(int facenum, int* numpatches, int const ** patches);
extern void InterpolateSampleLight(
	float3_array const & position, int surface, int style, float3_array& out
);
extern void FreeTriangulations();

// mathutil.c
extern bool TestSegmentAgainstOpaqueList(
	float3_array const & p1,
	float3_array const & p2,
	float3_array& scaleout,
	int& opaquestyleout
);
extern bool intersect_linesegment_plane(
	dplane_t const & plane,
	float3_array const & p1,
	float3_array const & p2,
	float3_array& point
);
extern bool point_in_winding(
	fast_winding const & w,
	dplane_t const & plane,
	float3_array const & point,
	float epsilon = 0.0
);
extern bool point_in_winding_noedge(
	fast_winding const & w,
	dplane_t const & plane,
	float3_array const & point,
	float width
);
extern void snap_to_winding(
	fast_winding const & w, dplane_t const & plane, float3_array& point
);
extern float snap_to_winding_noedge(
	fast_winding const & w,
	dplane_t const & plane,
	float3_array& point,
	float width,
	float maxmove
);
extern float3_array snap_point_to_plane(
	dplane_t const * const plane, float3_array const & point, float offset
) noexcept;
extern float CalcSightArea(
	float3_array const & receiver_origin,
	float3_array const & receiver_normal,
	fast_winding const * emitter_winding,
	int skylevel,
	float lighting_power,
	float lighting_scale
);
extern float CalcSightArea_SpotLight(
	float3_array const & receiver_origin,
	float3_array const & receiver_normal,
	fast_winding const * emitter_winding,
	float3_array const & emitter_normal,
	float emitter_stopdot,
	float emitter_stopdot2,
	int skylevel,
	float lighting_power,
	float lighting_scale
);
extern void GetAlternateOrigin(
	float3_array const & pos,
	float3_array const & normal,
	patch_t const * patch,
	float3_array& origin
);

// studio.cpp
extern void LoadStudioModels();
extern void FreeStudioModels();
extern bool TestSegmentAgainstStudioList(
	float3_array const & p1, float3_array const & p2
);
extern bool g_studioshadow;
