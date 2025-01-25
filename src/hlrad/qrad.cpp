/*

	R A D I O S I T Y    -aka-    R A D

	Code based on original code from Valve Software,
	Modified by Sean "Zoner" Cavanaugh (seanc@gearboxsoftware.com) with
   permission. Modified by Tony "Merl" Moore (merlinis@bigpond.net.au) [AJM]
	Modified by amckern (amckern@yahoo.com)
	Modified by vluzacn (vluzacn@163.com)
	Modified by seedee (cdaniel9000@gmail.com)
	Modified by Oskar Larsson Högfeldt (AKA Oskar Potatis) (oskar@oskar.pm)

*/

#include "qrad.h"

#include "bsp_file_sizes.h"
#include "rad_cli_option_defaults.h"
#include "utf8.h"

#include <algorithm>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

using namespace std::literals;

/*
 * NOTES
 * -----
 * every surface must be divided into at least two g_patches each axis
 */

bool g_pre25update = DEFAULT_PRE25UPDATE;
bool g_fastmode = DEFAULT_FASTMODE;
bool g_studioshadow = DEFAULT_STUDIOSHADOW;

static vis_method g_method = cli_option_defaults::visMethod;

float g_fade = DEFAULT_FADE;

patch_t* g_face_patches[MAX_MAP_FACES];
entity_t* g_face_entity[MAX_MAP_FACES];
eModelLightmodes g_face_lightmode[MAX_MAP_FACES];
patch_t* g_patches;
entity_t* g_face_texlights[MAX_MAP_FACES];
unsigned g_num_patches;

static std::array<float3_array, MAXLIGHTMAPS>* addlight;
static std::array<float3_array, MAXLIGHTMAPS>* emitlight;
static std::array<unsigned char, MAXLIGHTMAPS>* newstyles;

float3_array g_face_offset[MAX_MAP_FACES]; // for rotating bmodels

float g_direct_scale = DEFAULT_DLIGHT_SCALE;

unsigned g_numbounce = DEFAULT_BOUNCE; // 3; /* Originally this was 8 */

static bool g_dumppatches = DEFAULT_DUMPPATCHES;

float3_array g_ambient{ DEFAULT_AMBIENT_RED,
						DEFAULT_AMBIENT_GREEN,
						DEFAULT_AMBIENT_BLUE };
float g_limitthreshold = DEFAULT_LIMITTHRESHOLD;
bool g_drawoverload = false;

float g_lightscale = DEFAULT_LIGHTSCALE;
float g_dlight_threshold
	= DEFAULT_DLIGHT_THRESHOLD; // was DIRECT_LIGHT constant

char g_source[_MAX_PATH] = "";

char g_vismatfile[_MAX_PATH] = "";
bool g_incremental = DEFAULT_INCREMENTAL;
float g_indirect_sun = DEFAULT_INDIRECT_SUN;
bool g_extra = DEFAULT_EXTRA;
bool g_texscale = DEFAULT_TEXSCALE;

float g_smoothing_threshold;
float g_smoothing_value = DEFAULT_SMOOTHING_VALUE;
float g_smoothing_threshold_2;
float g_smoothing_value_2 = DEFAULT_SMOOTHING2_VALUE;

bool g_circus = DEFAULT_CIRCUS;
bool g_allow_opaques = DEFAULT_ALLOW_OPAQUES;
bool g_allow_spread = DEFAULT_ALLOW_SPREAD;

float3_array g_colour_qgamma = { DEFAULT_COLOUR_GAMMA_RED,
								 DEFAULT_COLOUR_GAMMA_GREEN,
								 DEFAULT_COLOUR_GAMMA_BLUE };
float3_array g_colour_lightscale = { DEFAULT_COLOUR_LIGHTSCALE_RED,
									 DEFAULT_COLOUR_LIGHTSCALE_GREEN,
									 DEFAULT_COLOUR_LIGHTSCALE_BLUE };
float3_array g_colour_jitter_hack = { DEFAULT_COLOUR_JITTER_HACK_RED,
									  DEFAULT_COLOUR_JITTER_HACK_GREEN,
									  DEFAULT_COLOUR_JITTER_HACK_BLUE };
float3_array g_jitter_hack = { DEFAULT_JITTER_HACK_RED,
							   DEFAULT_JITTER_HACK_GREEN,
							   DEFAULT_JITTER_HACK_BLUE };

bool g_customshadow_with_bouncelight
	= DEFAULT_CUSTOMSHADOW_WITH_BOUNCELIGHT;
bool g_rgb_transfers = DEFAULT_RGB_TRANSFERS;

float g_transtotal_hack = DEFAULT_TRANSTOTAL_HACK;
std::uint8_t g_minlight = cli_option_defaults::minLight;
float_type g_transfer_compress_type
	= cli_option_defaults::transferCompressType;
vector_type g_rgbtransfer_compress_type
	= cli_option_defaults::rgbTransferCompressType;
bool g_softsky = DEFAULT_SOFTSKY;
bool g_blockopaque = cli_option_defaults::blockOpaque;
bool g_notextures = DEFAULT_NOTEXTURES;
float g_texreflectgamma = DEFAULT_TEXREFLECTGAMMA;
float g_texreflectscale = DEFAULT_TEXREFLECTSCALE;
bool g_bleedfix = DEFAULT_BLEEDFIX;
bool g_drawpatch = false;
bool g_drawsample = false;
float3_array g_drawsample_origin = { 0, 0, 0 };
float g_drawsample_radius = 0;
bool g_drawedge = false;
bool g_drawlerp = false;
bool g_drawnudge = false;

// Cosine of smoothing angle(in radians)
float g_coring = DEFAULT_CORING; // Light threshold to force to
								 // blackness(minimizes lightmaps)
bool g_chart = cli_option_defaults::chart;
bool g_estimate = cli_option_defaults::estimate;
bool g_info = cli_option_defaults::info;

// Patch creation and subdivision criteria
bool g_subdivide = DEFAULT_SUBDIVIDE;
float g_chop = DEFAULT_CHOP;
float g_texchop = DEFAULT_TEXCHOP;

// Opaque faces
std::vector<opaqueList_t> g_opaque_face_list;

float g_corings[ALLSTYLES];
std::unique_ptr<float3_array[]> g_translucenttextures{};
float g_translucentdepth = DEFAULT_TRANSLUCENTDEPTH;
float g_blur = DEFAULT_BLUR;
bool g_noemitterrange = DEFAULT_NOEMITTERRANGE;
float g_texlightgap = DEFAULT_TEXLIGHTGAP;

// Misc
int leafparents[MAX_MAP_LEAFS];
int nodeparents[MAX_MAP_NODES];
int stylewarningcount = 0;
int stylewarningnext = 1;
float g_maxdiscardedlight = 0;
float3_array g_maxdiscardedpos{ 0, 0, 0 };

// =====================================================================================
//  GetParamsFromEnt
//      this function is called from parseentity when it encounters the
//      info_compile_parameters entity. each tool should have its own
//      version of this to handle its own specific settings.
// =====================================================================================
void GetParamsFromEnt(entity_t* mapent) {
	int iTmp;
	float flTmp;
	char szTmp[256]; // lightdata
	char const * pszTmp;

	Log("\nCompile Settings detected from info_compile_parameters entity\n"
	);

	// verbose(choices) : "Verbose compile messages" : 0 = [ 0 : "Off" 1 :
	// "On" ]
	iTmp = IntForKey(mapent, u8"verbose");
	if (iTmp == 1) {
		g_verbose = true;
	} else if (iTmp == 0) {
		g_verbose = false;
	}
	Log("%30s [ %-9s ]\n", "Compile Option", "setting");
	Log("%30s [ %-9s ]\n",
		"Verbose Compile Messages",
		g_verbose ? "on" : "off");

	// estimate(choices) :"Estimate Compile Times?" : 0 = [ 0: "Yes" 1: "No"
	// ]
	if (IntForKey(mapent, u8"estimate")) {
		g_estimate = true;
	} else {
		g_estimate = false;
	}
	Log("%30s [ %-9s ]\n",
		"Estimate Compile Times",
		g_estimate ? "on" : "off");

	// priority(choices) : "Priority Level" : 0 = [	0 : "Normal" 1 : "High"
	// -1 : "Low" ]
	std::int32_t const priorityFromEnt = IntForKey(mapent, u8"priority");
	if (priorityFromEnt == 1) {
		g_threadpriority = q_threadpriority::eThreadPriorityHigh;
		Log("%30s [ %-9s ]\n", "Thread Priority", "high");
	} else if (priorityFromEnt == -1) {
		g_threadpriority = q_threadpriority::eThreadPriorityLow;
		Log("%30s [ %-9s ]\n", "Thread Priority", "low");
	}

	// bounce(integer) : "Number of radiosity bounces" : 0
	iTmp = IntForKey(mapent, u8"bounce");
	if (iTmp) {
		g_numbounce = abs(iTmp);
		Log("%30s [ %-9s ]\n",
			"Number of radiosity bounces",
			(char const *) ValueForKey(mapent, u8"bounce"));
	}

	iTmp = IntForKey(mapent, u8"customshadowwithbounce");
	if (iTmp) {
		g_customshadow_with_bouncelight = true;
		Log("%30s [ %-9s ]\n",
			"Custom Shadow with Bounce Light",
			(char const *) ValueForKey(mapent, u8"customshadowwithbounce"));
	}
	iTmp = IntForKey(mapent, u8"rgbtransfers");
	if (iTmp) {
		g_rgb_transfers = true;
		Log("%30s [ %-9s ]\n",
			"RGB Transfers",
			(char const *) ValueForKey(mapent, u8"rgbtransfers"));
	}

	// ambient(string) : "Ambient world light (0.0 to 1.0, R G B)" : "0 0 0"
	pszTmp = (char const *) ValueForKey(mapent, u8"ambient");
	if (pszTmp) {
		float red = 0, green = 0, blue = 0;
		if (sscanf(pszTmp, "%f %f %f", &red, &green, &blue)) {
			if (red < 0 || red > 1 || green < 0 || green > 1 || blue < 0
				|| blue > 1) {
				Error(
					"info_compile_parameters: Ambient World Light (ambient) all 3 values must be within the range of 0.0 to 1.0\n"
					"Parsed values:\n"
					"    red [ %1.3f ] %s\n"
					"  green [ %1.3f ] %s\n"
					"   blue [ %1.3f ] %s\n",
					red,
					(red < 0 || red > 1) ? "OUT OF RANGE" : "",
					green,
					(green < 0 || green > 1) ? "OUT OF RANGE" : "",
					blue,
					(blue < 0 || blue > 1) ? "OUT OF RANGE" : ""
				);
			}

			if (red == 0 && green == 0 && blue == 0) {
			} // dont bother setting null values
			else {
				g_ambient[0] = red * 128;
				g_ambient[1] = green * 128;
				g_ambient[2] = blue * 128;
				Log("%30s [ %1.3f %1.3f %1.3f ]\n",
					"Ambient world light (R G B)",
					red,
					green,
					blue);
			}
		} else {
			Error(
				"info_compile_parameters: Ambient World Light (ambient) has unrecognised value\n"
				"This keyvalue accepts 3 numeric values from 0.000 to 1.000, use \"0 0 0\" if in doubt"
			);
		}
	}

	// smooth(integer) : "Smoothing threshold (in degrees)" : 0
	flTmp = float_for_key(*mapent, u8"smooth");
	if (flTmp) {
		/*g_smoothing_threshold = flTmp;*/
		g_smoothing_threshold = cos(
			g_smoothing_value * (std::numbers::pi_v<double> / 180.0)
		); // --vluzacn
		Log("%30s [ %-9s ]\n",
			"Smoothing threshold",
			(char const *) ValueForKey(mapent, u8"smooth"));
	}

	// dscale(integer) : "Direct Lighting Scale" : 1
	flTmp = float_for_key(*mapent, u8"dscale");
	if (flTmp) {
		g_direct_scale = flTmp;
		Log("%30s [ %-9s ]\n",
			"Direct Lighting Scale",
			(char const *) ValueForKey(mapent, u8"dscale"));
	}

	// chop(integer) : "Chop Size" : 64
	iTmp = IntForKey(mapent, u8"chop");
	if (iTmp) {
		g_chop = iTmp;
		Log("%30s [ %-9s ]\n",
			"Chop Size",
			(char const *) ValueForKey(mapent, u8"chop"));
	}

	// texchop(integer) : "Texture Light Chop Size" : 32
	flTmp = float_for_key(*mapent, u8"texchop");
	if (flTmp) {
		g_texchop = flTmp;
		Log("%30s [ %-9s ]\n",
			"Texture Light Chop Size",
			(char const *) ValueForKey(mapent, u8"texchop"));
	}

	/*
	hlrad(choices) : "HLRAD" : 0 =
	[
		0 : "Off"
		1 : "Normal"
		2 : "Extra"
	]
	*/
	iTmp = IntForKey(mapent, u8"hlrad");
	if (iTmp == 0) {
		Fatal(
			assume_TOOL_CANCEL,
			"%s flag was not checked in info_compile_parameters entity, execution of %s cancelled",
			g_Program,
			g_Program
		);
		CheckFatal();
	} else if (iTmp == 1) {
		g_extra = false;
	} else if (iTmp == 2) {
		g_extra = true;
	}
	Log("%30s [ %-9s ]\n", "Extra RAD", g_extra ? "on" : "off");

	/*
	sparse(choices) : "Vismatrix Method" : 2 =
	[
		0 : "No Vismatrix"
		1 : "Sparse Vismatrix"
		2 : "Normal"
	]
	*/
	iTmp = IntForKey(mapent, u8"sparse");
	if (iTmp == 1) {
		g_method = vis_method::sparse_vismatrix;
	} else if (iTmp == 0) {
		g_method = vis_method::no_vismatrix;
	} else if (iTmp == 2) {
		g_method = vis_method::vismatrix;
	}
	Log("%30s [ %-9s ]\n",
		"Sparse Vismatrix",
		g_method == vis_method::sparse_vismatrix ? "on" : "off");
	Log("%30s [ %-9s ]\n",
		"NoVismatrix",
		g_method == vis_method::no_vismatrix ? "on" : "off");

	/*
	circus(choices) : "Circus RAD lighting" : 0 =
	[
		0 : "Off"
		1 : "On"
	]
	*/
	iTmp = IntForKey(mapent, u8"circus");
	if (iTmp == 0) {
		g_circus = false;
	} else if (iTmp == 1) {
		g_circus = true;
	}

	Log("%30s [ %-9s ]\n", "Circus Lighting Mode", g_circus ? "on" : "off");

	////////////////////
	Log("\n");
}

// =====================================================================================
//  MakeParents
//      blah
// =====================================================================================
static void MakeParents(int const nodenum, int const parent) {
	int i;
	int j;
	dnode_t* node;

	nodeparents[nodenum] = parent;
	node = g_dnodes.data() + nodenum;

	for (i = 0; i < 2; i++) {
		j = node->children[i];
		if (j < 0) {
			leafparents[-j - 1] = nodenum;
		} else {
			MakeParents(j, nodenum);
		}
	}
}

// =====================================================================================
//
//    TEXTURE LIGHT VALUES
//
// =====================================================================================

// misc
struct texlight_t {
	wad_texture_name name{};
	float3_array value{};
	char const * filename{
		nullptr
	}; // Either info_texlights or lights.rad filename
};

static std::vector<texlight_t> s_texlights;
typedef std::vector<texlight_t>::iterator texlight_i;

std::vector<minlight_t> s_minlights;

// =====================================================================================
//  ReadLightFile
// =====================================================================================
static void ReadLightFile(char const * const filename) {
	FILE* f;
	char scan[MAXTOKEN];
	short argCnt;
	unsigned int file_texlights = 0;

	f = fopen(filename, "r");
	if (!f) {
		Warning("Could not open texlight file %s", filename);
		return;
	} else {
		Log("Reading texlights from '%s'\n", filename);
	}

	while (fgets(scan, sizeof(scan), f)) {
		char* comment;
		char szTexlight[_MAX_PATH];
		float r, g, b, i = 1;

		comment = strstr(scan, "//");
		if (comment) {
			// Newline and Null terminate the string early if there is a c++
			// style single line comment
			comment[0] = '\n';
			comment[1] = 0;
		}

		argCnt = sscanf(scan, "%s %f %f %f %f", szTexlight, &r, &g, &b, &i);

		if (argCnt == 2) {
			// With 1+1 args, the R,G,B values are all equal to the first
			// value
			g = b = r;
		} else if (argCnt == 5) {
			// With 1+4 args, the R,G,B values are "scaled" by the fourth
			// numeric value i;
			r *= i / 255.0;
			g *= i / 255.0;
			b *= i / 255.0;
		} else if (argCnt != 4) {
			if (strlen(scan) > 4) {
				Warning("ignoring bad texlight '%s' in %s", scan, filename);
			}
			continue;
		}

		auto it = std::ranges::find_if(
			s_texlights,
			[&szTexlight](texlight_t const & tl) {
				return tl.name == (char8_t const *) szTexlight;
			}
		);
		if (it != s_texlights.end()) {
			if (strcmp(it->filename, filename) == 0) {
				Warning(
					"Duplication of texlight '%s' in file '%s'!",
					it->name.c_str(),
					it->filename
				);
			} else if (it->value[0] != r || it->value[1] != g
					   || it->value[2] != b) {
				Warning(
					"Overriding '%s' from '%s' with '%s'!",
					it->name.c_str(),
					it->filename,
					filename
				);
			} else {
				Warning(
					"Redundant '%s' def in '%s' AND '%s'!",
					it->name.c_str(),
					it->filename,
					filename
				);
			}
			s_texlights.erase(it);
		}

		std::optional<wad_texture_name> const maybeTextureName
			= wad_texture_name::make_if_legal_name(
				(char8_t const *) szTexlight
			);
		if (!maybeTextureName) {
			Warning("Ignoring bad texlight '%s' in light file", szTexlight);
			continue;
		}

		texlight_t texlight;
		texlight.name = maybeTextureName.value();
		texlight.value[0] = r;
		texlight.value[1] = g;
		texlight.value[2] = b;
		texlight.filename = filename;
		file_texlights++;
		s_texlights.push_back(texlight);
	}
	fclose(f); //--vluzacn
	Log("%u texlights parsed (%s)\n",
		file_texlights,
		filename); // readded //seedee
}

static float3_array LightForTexture(wad_texture_name name) {
	for (texlight_i it = s_texlights.begin(); it != s_texlights.end();
		 it++) {
		if (name == it->name) {
			float3_array result{};
			VectorCopy(it->value, result);
			return result;
		}
	}
	return float3_array{};
}

// =====================================================================================
//
//    MAKE FACES
//
// =====================================================================================

static float3_array BaseLightForFace(dface_t const * const f) {
	int fn = f - g_dfaces.data();
	if (g_face_texlights[fn]) {
		double r, g, b, scaler;
		switch (sscanf(
			(char const *) ValueForKey(g_face_texlights[fn], u8"_light"),
			"%lf %lf %lf %lf",
			&r,
			&g,
			&b,
			&scaler
		)) {
			case -1:
			case 0:
				r = 0.0;
			case 1:
				g = b = r;
			case 3:
				break;
			case 4:
				r *= scaler / 255.0;
				g *= scaler / 255.0;
				b *= scaler / 255.0;
				break;
			default:
				float3_array origin{
					get_float3_for_key(*g_face_texlights[fn], u8"origin")
				};
				Log("light at (%f,%f,%f) has bad or missing '_light' value : '%s'\n",
					origin[0],
					origin[1],
					origin[2],
					(char const *)
						ValueForKey(g_face_texlights[fn], u8"_light"));
				r = g = b = 0;
				break;
		}
		float3_array light{};
		light[0] = r > 0 ? r : 0;
		light[1] = g > 0 ? g : 0;
		light[2] = b > 0 ? b : 0;
		return light;
	}
	texinfo_t* tx;
	miptex_t* mt;
	int ofs;

	//
	// check for light emited by texture
	//
	tx = &g_texinfo[f->texinfo];

	ofs = ((dmiptexlump_t*) g_dtexdata.data())->dataofs[tx->miptex];
	mt = (miptex_t*) ((byte*) g_dtexdata.data() + ofs);

	return LightForTexture(mt->name);
}

// =====================================================================================
//  IsSpecial
// =====================================================================================
static bool IsSpecial(dface_t const * const f) {
	return g_texinfo[f->texinfo].flags & TEX_SPECIAL;
}

// =====================================================================================
//  PlacePatchInside
// =====================================================================================
static bool PlacePatchInside(patch_t* patch) {
	dplane_t const * plane;
	float3_array const & face_offset = g_face_offset[patch->faceNumber];

	plane = getPlaneFromFaceNumber(patch->faceNumber);

	float pointsfound;
	float pointstested;
	pointsfound = pointstested = 0;
	bool found;
	float3_array bestpoint{};
	float bestdist = -1.0;
	float3_array point;
	float dist;

	float3_array center = patch->winding->getCenter();
	found = false;

	point = vector_fma(plane->normal, PATCH_HUNT_OFFSET, center);
	pointstested++;
	if (HuntForWorld(point, face_offset, plane, 4, 0.2, PATCH_HUNT_OFFSET)
		|| HuntForWorld(
			point, face_offset, plane, 4, 0.8, PATCH_HUNT_OFFSET
		)) {
		pointsfound++;
		float3_array v;
		VectorSubtract(point, center, v);
		dist = vector_length(v);
		if (!found || dist < bestdist) {
			found = true;
			bestpoint = point;
			bestdist = dist;
		}
	}
	{
		for (int i = 0; i < patch->winding->size(); i++) {
			float const * p1;
			float const * p2;
			p1 = patch->winding->m_Points[i].data();
			p2 = patch->winding->m_Points[(i + 1) % patch->winding->size()]
					 .data();
			VectorAdd(p1, p2, point);
			VectorAdd(point, center, point);
			VectorScale(point, 1.0 / 3.0, point);
			point = vector_fma(plane->normal, PATCH_HUNT_OFFSET, point);
			pointstested++;
			if (HuntForWorld(
					point, face_offset, plane, 4, 0.2, PATCH_HUNT_OFFSET
				)
				|| HuntForWorld(
					point, face_offset, plane, 4, 0.8, PATCH_HUNT_OFFSET
				)) {
				pointsfound++;
				float3_array v;
				VectorSubtract(point, center, v);
				dist = vector_length(v);
				if (!found || dist < bestdist) {
					found = true;
					bestpoint = point;
					bestdist = dist;
				}
			}
		}
	}
	patch->exposure = pointsfound / pointstested;

	if (found) {
		patch->origin = bestpoint;
		patch->flags = ePatchFlagNull;
		return true;
	} else {
		patch->origin = vector_fma(
			plane->normal, PATCH_HUNT_OFFSET, center
		);
		patch->flags = ePatchFlagOutside;
		Developer(
			developer_level::fluff,
			"Patch @ (%4.3f %4.3f %4.3f) outside world\n",
			patch->origin[0],
			patch->origin[1],
			patch->origin[2]
		);
		return false;
	}
}

static void UpdateEmitterInfo(patch_t* patch) {
	static_assert(
		SKYLEVELMAX >= ACCURATEBOUNCE_DEFAULT_SKYLEVEL + 3,
		"Please raise SKYLEVELMAX"
	);

	float const * origin = patch->origin.data();
	fast_winding const * winding = patch->winding;
	float radius = ON_EPSILON;
	for (int x = 0; x < winding->size(); x++) {
		float3_array delta;
		float dist;
		VectorSubtract(winding->m_Points[x], origin, delta);
		dist = vector_length(delta);
		if (dist > radius) {
			radius = dist;
		}
	}
	int skylevel = ACCURATEBOUNCE_DEFAULT_SKYLEVEL;
	float area = winding->getArea();
	float size = 0.8f;
	if (area < size * radius * radius) // the shape is too thin
	{
		skylevel++;
		size *= 0.25f;
		if (area < size * radius * radius) {
			skylevel++;
			size *= 0.25f;
			if (area < size * radius * radius) {
				// stop here
				radius = sqrt(area / size);
				// just decrease the range to limit the use of the new
				// method. because when the area is small, the new method
				// becomes randomized and unstable.
			}
		}
	}
	patch->emitter_range = ACCURATEBOUNCE_THRESHOLD * radius;
	if (g_noemitterrange) {
		patch->emitter_range = 0.0;
	}
	patch->emitter_skylevel = skylevel;
}

// =====================================================================================
//
//    SUBDIVIDE PATCHES
//
// =====================================================================================

// misc
#define MAX_SUBDIVIDE 16384
static fast_winding* windingArray[MAX_SUBDIVIDE];
static unsigned g_numwindings = 0;

// =====================================================================================
//  cutWindingWithGrid
//      Caller must free this returned value at some point
// =====================================================================================
static void cutWindingWithGrid(
	patch_t* patch, dplane_t const * plA, dplane_t const * plB
)
// This function has been rewritten because the original one is not totally
// correct and may fail to do what it claims.
{
	// patch->winding->size() must > 0
	// plA->dist and plB->dist will not be used
	fast_winding* winding = nullptr;
	float chop;
	float epsilon;
	int const max_gridsize = 64;
	float gridstartA;
	float gridstartB;
	int gridsizeA;
	int gridsizeB;
	float gridchopA;
	float gridchopB;
	int numstrips;

	winding = new fast_winding(*patch->winding
	); // perform all the operations on the copy
	chop = patch->chop;
	chop = std::max((float) 1.0, chop);
	epsilon = 0.6;

	// optimize the grid
	{
		float minA;
		float maxA;
		float minB;
		float maxB;

		minA = minB = hlrad_bogus_range;
		maxA = maxB = -hlrad_bogus_range;
		for (int x = 0; x < winding->size(); x++) {
			float* point;
			float dotA;
			float dotB;
			point = winding->m_Points[x].data();
			dotA = DotProduct(point, plA->normal);
			minA = std::min(minA, dotA);
			maxA = std::max(maxA, dotA);
			dotB = DotProduct(point, plB->normal);
			minB = std::min(minB, dotB);
			maxB = std::max(maxB, dotB);
		}

		gridchopA = chop;
		gridsizeA = (int) ceil((maxA - minA - 2 * epsilon) / gridchopA);
		gridsizeA = std::max(1, gridsizeA);
		if (gridsizeA > max_gridsize) {
			gridsizeA = max_gridsize;
			gridchopA = (maxA - minA) / (float) gridsizeA;
		}
		gridstartA = (minA + maxA) / 2.0 - (gridsizeA / 2.0) * gridchopA;

		gridchopB = chop;
		gridsizeB = (int) ceil((maxB - minB - 2 * epsilon) / gridchopB);
		gridsizeB = std::max(1, gridsizeB);
		if (gridsizeB > max_gridsize) {
			gridsizeB = max_gridsize;
			gridchopB = (maxB - minB) / (float) gridsizeB;
		}
		gridstartB = (minB + maxB) / 2.0 - (gridsizeB / 2.0) * gridchopB;
	}

	// cut the winding by the direction of plane A and save into
	// windingArray
	{
		g_numwindings = 0;
		for (int i = 1; i < gridsizeA; i++) {
			float dist;
			fast_winding front;
			fast_winding back;

			dist = gridstartA + i * gridchopA;
			winding->Clip(plA->normal, dist, front, back);

			if (!front
				|| front.WindingOnPlaneSide(plA->normal, dist, epsilon)
					== face_side::on) // ended
			{
				break;
			}
			if (!back
				|| back.WindingOnPlaneSide(plA->normal, dist, epsilon)
					== face_side::on) // didn't begin
			{
				continue;
			}

			*winding = std::move(front);
			windingArray[g_numwindings] = new fast_winding(std::move(back));
			g_numwindings++;
		}

		windingArray[g_numwindings] = winding;
		g_numwindings++;
		winding = nullptr;
	}

	// cut by the direction of plane B
	{
		numstrips = g_numwindings;
		for (int i = 0; i < numstrips; i++) {
			fast_winding* strip = windingArray[i];
			windingArray[i] = nullptr;

			for (int j = 1; j < gridsizeB; j++) {
				float dist;
				fast_winding front;
				fast_winding back;

				dist = gridstartB + j * gridchopB;
				strip->Clip(plB->normal, dist, front, back);

				if (!front
					|| front.WindingOnPlaneSide(plB->normal, dist, epsilon)
						== face_side::on) // ended
				{
					break;
				}
				if (!back
					|| back.WindingOnPlaneSide(plB->normal, dist, epsilon)
						== face_side::on) // didn't begin
				{
					continue;
				}

				*strip = std::move(front);
				windingArray[g_numwindings] = new fast_winding(
					std::move(back)
				);
				g_numwindings++;
			}

			windingArray[g_numwindings] = strip;
			g_numwindings++;
			strip = nullptr;
		}
	}

	delete patch->winding;
	patch->winding = nullptr;
}

// =====================================================================================
//  getGridPlanes
//      From patch, determine perpindicular grid planes to subdivide with
//      (returned in planeA and planeB) assume S and T is perpindicular
//      (they SHOULD be in worldcraft 3.3 but aren't always . . .)
// =====================================================================================
static void getGridPlanes(patch_t const * const p, dplane_t* const pl) {
	patch_t const * patch = p;
	dplane_t* planes = pl;
	dface_t const * f = &g_dfaces[patch->faceNumber];
	texinfo_t* tx = &g_texinfo[f->texinfo];
	dplane_t* plane = planes;
	dplane_t const * faceplane = getPlaneFromFaceNumber(patch->faceNumber);
	int x;

	for (x = 0; x < 2; x++, plane++) {
		// cut the patch along texel grid planes
		float val;
		val = DotProduct(faceplane->normal, tx->vecs[!x]);
		VectorMA(tx->vecs[!x], -val, faceplane->normal, plane->normal);
		normalize_vector(plane->normal);
		plane->dist = DotProduct(plane->normal, patch->origin);
	}
}

// =====================================================================================
//  SubdividePatch
// =====================================================================================
static void SubdividePatch(patch_t* patch) {
	dplane_t planes[2];
	dplane_t* plA = &planes[0];
	dplane_t* plB = &planes[1];
	fast_winding** winding;
	unsigned x;
	patch_t* new_patch;

	memset(windingArray, 0, sizeof(windingArray));
	g_numwindings = 0;

	getGridPlanes(patch, planes);
	cutWindingWithGrid(patch, plA, plB);

	x = 0;
	patch->next = nullptr;
	winding = windingArray;
	while (*winding == nullptr) {
		winding++;
		x++;
	}
	patch->winding = *winding;
	winding++;
	x++;
	patch->area = patch->winding->getArea();
	patch->origin = patch->winding->getCenter();
	PlacePatchInside(patch);
	UpdateEmitterInfo(patch);

	new_patch = g_patches + g_num_patches;
	for (; x < g_numwindings; x++, winding++) {
		if (*winding) {
			memcpy(new_patch, patch, sizeof(patch_t));

			new_patch->winding = *winding;
			new_patch->area = new_patch->winding->getArea();
			new_patch->origin = new_patch->winding->getCenter();
			PlacePatchInside(new_patch);
			UpdateEmitterInfo(new_patch);

			new_patch++;
			g_num_patches++;
			hlassume(g_num_patches < MAX_PATCHES, assume_MAX_PATCHES);
		}
	}

	// ATTENTION: We let SortPatches relink all the ->next correctly!
	// instead of doing it here too which is somewhat complicated
}

// =====================================================================================
//  MakePatchForFace
static float totalarea = 0;
// =====================================================================================

std::unique_ptr<float[]> chopscales; //[nummiptex]

void ReadCustomChopValue() {
	int num;
	int i, k;
	entity_t* mapent;

	num = ((dmiptexlump_t*) g_dtexdata.data())->nummiptex;
	chopscales = std::make_unique<float[]>(num);
	for (i = 0; i < num; i++) {
		chopscales[i] = 1.0;
	}
	for (k = 0; k < g_numentities; k++) {
		mapent = &g_entities[k];
		if (!classname_is(mapent, u8"info_chopscale")) {
			continue;
		}
		Developer(
			developer_level::message, "info_chopscale entity detected.\n"
		);
		for (i = 0; i < num; i++) {
			wad_texture_name const texname{
				((miptex_t*) (g_dtexdata.data()
							  + ((dmiptexlump_t*) g_dtexdata.data())
									->dataofs[i]))
					->name
			};
			std::u8string_view value{ value_for_key(mapent, texname) };
			if (value.empty()) {
				continue;
			}
			if (texname.is_origin()) {
				continue;
			}
			if (atof((char const *) value.data()) <= 0) {
				continue;
			}
			chopscales[i] = atof((char const *) value.data());
			Developer(
				developer_level::message,
				"info_chopscale: %s = %f\n",
				texname.c_str(),
				chopscales[i]
			);
		}
	}
}

float ChopScaleForTexture(int facenum) {
	return chopscales[g_texinfo[g_dfaces[facenum].texinfo].miptex];
}

float* g_smoothvalues; //[nummiptex]

void ReadCustomSmoothValue() {
	int num;
	int i, k;
	entity_t* mapent;

	num = ((dmiptexlump_t*) g_dtexdata.data())->nummiptex;
	g_smoothvalues = (float*) malloc(num * sizeof(float));
	for (i = 0; i < num; i++) {
		g_smoothvalues[i] = g_smoothing_threshold;
	}
	for (k = 0; k < g_numentities; k++) {
		mapent = &g_entities[k];
		if (!classname_is(mapent, u8"info_smoothvalue")) {
			continue;
		}
		Developer(
			developer_level::message, "info_smoothvalue entity detected.\n"
		);
		for (i = 0; i < num; i++) {
			wad_texture_name const texname{
				((miptex_t*) (g_dtexdata.data()
							  + ((dmiptexlump_t*) g_dtexdata.data())
									->dataofs[i]))
					->name
			};
			std::u8string_view value{ value_for_key(mapent, texname) };
			if (value.empty()) {
				continue;
			}
			if (texname.is_origin()) {
				continue;
			}
			g_smoothvalues[i] = cos(
				atof((char const *) value.data())
				* (std::numbers::pi_v<double> / 180.0)
			);
			Developer(
				developer_level::message,
				"info_smoothvalue: %s = %f\n",
				texname.c_str(),
				atof((char const *) value.data())
			);
		}
	}
}

void ReadTranslucentTextures() {
	int num;
	int i, k;
	entity_t* mapent;

	num = ((dmiptexlump_t*) g_dtexdata.data())->nummiptex;
	g_translucenttextures = std::make_unique<float3_array[]>(num);
	for (k = 0; k < g_numentities; k++) {
		mapent = &g_entities[k];
		if (!key_value_is(mapent, u8"classname", u8"info_translucent")) {
			continue;
		}
		Developer(
			developer_level::message, "info_translucent entity detected.\n"
		);
		for (i = 0; i < num; i++) {
			wad_texture_name const texname{
				((miptex_t*) (g_dtexdata.data()
							  + ((dmiptexlump_t*) g_dtexdata.data())
									->dataofs[i]))
					->name
			};
			std::u8string_view value{ value_for_key(mapent, texname) };
			if (value.empty()) {
				continue;
			}
			if (texname.is_origin()) {
				continue;
			}
			double r, g, b;
			int count;
			count = sscanf(
				(char const *) value.data(), "%lf %lf %lf", &r, &g, &b
			);
			if (count == 1) {
				g = b = r;
			} else if (count != 3) {
				Warning(
					"ignore bad translucent value '%s'",
					(char const *) value.data()
				);
				continue;
			}
			if (r < 0.0 || r > 1.0 || g < 0.0 || g > 1.0 || b < 0.0
				|| b > 1.0) {
				Warning("translucent value should be 0.0-1.0");
				continue;
			}
			g_translucenttextures[i][0] = r;
			g_translucenttextures[i][1] = g;
			g_translucenttextures[i][2] = b;
			Developer(
				developer_level::message,
				"info_translucent: %s = %f %f %f\n",
				texname.c_str(),
				r,
				g,
				b
			);
		}
	}
}

std::unique_ptr<lighting_cone_power_and_scale[]>
	g_lightingconeinfo; // size == nummiptex

static float DefaultScaleForPower(float power) {
	float scale;
	// scale = Pi / Integrate [2 Pi * Sin [x] * Cos[x] ^ power, {x, 0, Pi /
	// 2}]
	scale = (1 + power) / 2.0;
	return scale;
}

void ReadLightingCone() {
	int num;
	int i, k;
	entity_t* mapent;

	num = ((dmiptexlump_t*) g_dtexdata.data())->nummiptex;
	g_lightingconeinfo = std::make_unique<lighting_cone_power_and_scale[]>(
		num
	);
	for (k = 0; k < g_numentities; k++) {
		mapent = &g_entities[k];
		if (!classname_is(mapent, u8"info_angularfade")) {
			continue;
		}
		Developer(
			developer_level::message, "info_angularfade entity detected.\n"
		);
		for (i = 0; i < num; i++) {
			wad_texture_name const texname{
				((miptex_t*) (g_dtexdata.data()
							  + ((dmiptexlump_t*) g_dtexdata.data())
									->dataofs[i]))
					->name
			};

			std::u8string_view value{ value_for_key(mapent, texname) };
			if (value.empty()) {
				continue;
			}
			if (texname.is_origin()) {
				continue;
			}
			double power{};
			double scale{};
			int count;
			count = sscanf(
				(char const *) value.data(), "%lf %lf", &power, &scale
			);
			if (count == 1) {
				scale = 1.0;
			} else if (count != 2) {
				Warning(
					"ignore bad angular fade value '%s'",
					(char const *) value.data()
				);
				continue;
			}
			if (power < 0.0 || scale < 0.0) {
				Warning(
					"ignore disallowed angular fade value '%s'",
					(char const *) value.data()
				);
				continue;
			}
			scale *= DefaultScaleForPower(power);
			g_lightingconeinfo[i].power = power;
			g_lightingconeinfo[i].scale = scale;
			Developer(
				developer_level::message,
				"info_angularfade: %s = %f %f\n",
				texname.c_str(),
				power,
				scale
			);
		}
	}
}

static float getScale(patch_t const * const patch) {
	dface_t* f = &g_dfaces[patch->faceNumber];
	texinfo_t* tx = &g_texinfo[f->texinfo];

	if (g_texscale) {
		dplane_t const * faceplane = getPlaneFromFace(f);
		std::array<float3_array, 2> vecs_perpendicular;
		std::array<float, 2> scale;
		float dot;

		// snap texture "vecs" to faceplane without affecting texture
		// alignment
		for (std::size_t x = 0; x < 2; ++x) {
			dot = DotProduct(faceplane->normal, tx->vecs[x]);
			VectorMA(
				tx->vecs[x], -dot, faceplane->normal, vecs_perpendicular[x]
			);
		}

		scale[0] = 1
			/ std::max(NORMAL_EPSILON,
					   vector_length(vecs_perpendicular[0]));
		scale[1] = 1
			/ std::max(NORMAL_EPSILON,
					   vector_length(vecs_perpendicular[1]));

		// don't care about the angle between vecs[0] and vecs[1] (given the
		// length of "vecs", smaller angle = larger texel area), because
		// gridplanes will have the same angle (also smaller angle = larger
		// patch area)

		return sqrt(scale[0] * scale[1]);
	} else {
		return 1.0;
	}
}

// =====================================================================================
//  getChop
// =====================================================================================
static bool getEmitMode(patch_t const * patch) {
	float value = DotProduct(patch->baselight, patch->texturereflectivity)
		/ 3;
	if (g_face_texlights[patch->faceNumber]) {
		if (has_key_value(
				g_face_texlights[patch->faceNumber], u8"_scale"
			)) {
			value *= float_for_key(
				*g_face_texlights[patch->faceNumber], u8"_scale"
			);
		}
	}
	bool emitmode = value > 0.0 && value >= g_dlight_threshold;
	if (g_face_texlights[patch->faceNumber]) {
		switch (IntForKey(g_face_texlights[patch->faceNumber], u8"_fast")) {
			case 1:
				emitmode = false;
				break;
			case 2:
				emitmode = true;
				break;
		}
	}
	return emitmode;
}

static float getChop(patch_t const * const patch) {
	float rval;

	if (g_face_texlights[patch->faceNumber]) {
		if (has_key_value(g_face_texlights[patch->faceNumber], u8"_chop")) {
			rval = float_for_key(
				*g_face_texlights[patch->faceNumber], u8"_chop"
			);
			if (rval < 1.0) {
				rval = 1.0;
			}
			return rval;
		}
	}
	if (!patch->emitmode) {
		rval = g_chop * getScale(patch);
	} else {
		rval = g_texchop * getScale(patch);
		// we needn't do this now, so let's save our compile time.
	}

	rval *= ChopScaleForTexture(patch->faceNumber);
	return rval;
}

// =====================================================================================
//  MakePatchForFace
// =====================================================================================
static void MakePatchForFace(
	int const fn, fast_winding* w, int style, int bouncestyle
) // LRC
{
	dface_t const * f = &g_dfaces[fn];

	// No g_patches at all for the sky!
	if (IsSpecial(f)) {
		return;
	}
	if (g_face_texlights[fn]) {
		style = IntForKey(g_face_texlights[fn], u8"style");
		if (style < 0) {
			style = -style;
		}
		style = (unsigned char) style;
		if (style >= ALLSTYLES) {
			Error(
				"invalid light style: style (%d) >= ALLSTYLES (%d)",
				style,
				ALLSTYLES
			);
		}
	}
	patch_t* patch;
	int numpoints = w->size();

	if (numpoints < 3) // WTF! (Actually happens in real-world maps too)
	{
		Developer(
			developer_level::warning,
			"Face %d only has %d points on winding\n",
			fn,
			numpoints
		);
		return;
	}
	if (numpoints > MAX_POINTS_ON_WINDING) {
		Error("numpoints %d > MAX_POINTS_ON_WINDING", numpoints);
		return;
	}

	patch = &g_patches[g_num_patches];
	hlassume(g_num_patches < MAX_PATCHES, assume_MAX_PATCHES);
	memset(patch, 0, sizeof(patch_t));

	patch->winding = w;

	patch->area = patch->winding->getArea();
	patch->origin = patch->winding->getCenter();
	patch->faceNumber = fn;

	totalarea += patch->area;

	patch->baselight = BaseLightForFace(f);

	patch->emitstyle = style;

	VectorCopy(
		g_textures[g_texinfo[f->texinfo].miptex].reflectivity,
		patch->texturereflectivity
	);
	if (g_face_texlights[fn]
		&& has_key_value(g_face_texlights[fn], u8"_texcolor")) {
		float3_array texturereflectivity;
		float3_array texturecolor{
			get_float3_for_key(*g_face_texlights[fn], u8"_texcolor")
		};
		for (int k = 0; k < 3; k++) {
			texturecolor[k] = floor(texturecolor[k] + 0.001);
		}
		if (VectorMinimum(texturecolor) < -0.001
			|| VectorMaximum(texturecolor) > 255.001) {
			float3_array const origin{
				get_float3_for_key(*g_face_texlights[fn], u8"origin")
			};
			Error(
				"light_surface entity at (%g,%g,%g): texture color (%g,%g,%g) must be numbers between 0 and 255.",
				origin[0],
				origin[1],
				origin[2],
				texturecolor[0],
				texturecolor[1],
				texturecolor[2]
			);
		}
		VectorScale(texturecolor, 1.0 / 255.0, texturereflectivity);
		for (int k = 0; k < 3; k++) {
			texturereflectivity[k] = pow(
				texturereflectivity[k], g_texreflectgamma
			);
		}
		VectorScale(
			texturereflectivity, g_texreflectscale, texturereflectivity
		);
		if (VectorMaximum(texturereflectivity) > 1.0 + NORMAL_EPSILON) {
			Warning(
				"Texture '%s': reflectivity (%f,%f,%f) greater than 1.0.",
				g_textures[g_texinfo[f->texinfo].miptex].name.c_str(),
				texturereflectivity[0],
				texturereflectivity[1],
				texturereflectivity[2]
			);
		}
		VectorCopy(texturereflectivity, patch->texturereflectivity);
	}
	{
		float opacity = 0.0;
		if (g_face_entity[fn] == g_entities.data()) {
			opacity = 1.0;
		} else {
			int x;
			for (x = 0; x < g_opaque_face_list.size(); x++) {
				opaqueList_t* op = &g_opaque_face_list[x];
				if (&g_entities[op->entitynum] == g_face_entity[fn]) {
					opacity = 1.0;
					if (op->transparency) {
						opacity = 1.0
							- vector_average(op->transparency_scale);
						opacity = opacity > 1.0 ? 1.0
							: opacity < 0.0		? 0.0
												: opacity;
					}
					if (op->style != -1) {		 // toggleable opaque entity
						if (bouncestyle == -1) { // by default
							opacity = 0.0;		 // doesn't reflect light
						}
					}
					break;
				}
			}
			if (x == g_opaque_face_list.size()) { // not opaque
				if (bouncestyle != -1) {		  // with light_bounce
					opacity = 1.0;				  // reflects light
				}
			}
		}
		VectorScale(
			patch->texturereflectivity, opacity, patch->bouncereflectivity
		);
	}
	patch->bouncestyle = bouncestyle;
	if (bouncestyle == 0) {		 // there is an unnamed light_bounce
		patch->bouncestyle = -1; // reflects light normally
	}
	patch->emitmode = getEmitMode(patch);
	patch->scale = getScale(patch);
	patch->chop = getChop(patch);
	VectorCopy(
		g_translucenttextures[g_texinfo[f->texinfo].miptex],
		patch->translucent_v
	);
	patch->translucent_b = !vectors_almost_same(
		patch->translucent_v, float3_array{}
	);
	PlacePatchInside(patch);
	UpdateEmitterInfo(patch);

	g_face_patches[fn] = patch;
	g_num_patches++;

	float3_array centroid{};

	// Per-face data
	{
		int j;

		// Centroid of face for nudging samples in direct lighting pass
		for (j = 0; j < f->numedges; j++) {
			int edge = g_dsurfedges[f->firstedge + j];

			if (edge > 0) {
				VectorAdd(
					g_dvertexes[g_dedges[edge].v[0]].point,
					centroid,
					centroid
				);
				VectorAdd(
					g_dvertexes[g_dedges[edge].v[1]].point,
					centroid,
					centroid
				);
			} else {
				VectorAdd(
					g_dvertexes[g_dedges[-edge].v[1]].point,
					centroid,
					centroid
				);
				VectorAdd(
					g_dvertexes[g_dedges[-edge].v[0]].point,
					centroid,
					centroid
				);
			}
		}

		// Fixup centroid for anything with an altered origin (rotating
		// models/turrets mostly) Save them for moving direct lighting
		// points towards the face center
		VectorScale(centroid, 1.0 / (f->numedges * 2), centroid);
		VectorAdd(centroid, g_face_offset[fn], g_face_centroids[fn]);
	}

	{
		if (g_subdivide) {
			float amt;
			float length;
			float3_array delta;

			bounding_box bounds = patch->winding->getBounds();
			VectorSubtract(
				to_float3(bounds.maxs), to_float3(bounds.mins), delta
			);
			length = vector_length(delta);
			amt = patch->chop;

			if (length > amt) {
				if (patch->area < 1.0) {
					Developer(
						developer_level::warning,
						"Patch at (%4.3f %4.3f %4.3f) (face %d) tiny area (%4.3f) not subdividing \n",
						patch->origin[0],
						patch->origin[1],
						patch->origin[2],
						patch->faceNumber,
						patch->area
					);
				} else {
					SubdividePatch(patch);
				}
			}
		}
	}
}

static void AddFaceToOpaqueList(
	int entitynum,
	int modelnum,
	float3_array const & origin,
	std::optional<float3_array> const & transparency_scale,
	int style,
	bool block
) {
	opaqueList_t opaque{};

	if (transparency_scale) {
		if (style != -1) {
			Warning(
				"Dynamic shadow is not allowed in entity with custom shadow.\n"
			);
			style = -1;
		}
		opaque.transparency_scale = transparency_scale.value();
		opaque.transparency = true;
	}
	opaque.entitynum = entitynum;
	opaque.modelnum = modelnum;
	VectorCopy(origin, opaque.origin);
	opaque.style = style;
	opaque.block = block;
	g_opaque_face_list.push_back(std::move(opaque));
}

// =====================================================================================
//  FreeOpaqueFaceList
// =====================================================================================
static void FreeOpaqueFaceList() {
	g_opaque_face_list.clear();
	g_opaque_face_list.shrink_to_fit();
}

static void LoadOpaqueEntities() {
	int modelnum, entnum;

	for (modelnum = 0; modelnum < g_nummodels;
		 modelnum++) // Loop through brush models
	{
		dmodel_t* model = &g_dmodels[modelnum]; // Get current model
		std::array<char8_t, 16> stringmodel;
		snprintf(
			(char*) stringmodel.data(), sizeof(stringmodel), "*%i", modelnum
		); // Model number to string

		for (entnum = 0; entnum < g_numentities;
			 entnum++) // Loop through map ents
		{
			entity_t const & ent
				= g_entities[entnum]; // Get the current ent

			if (!key_value_is(
					&ent, u8"model", stringmodel.data()
				)) { // Skip ents that don't match the current model
				continue;
			}
			float3_array origin;
			{
				origin = get_float3_for_key(
					ent, u8"origin"
				); // Get origin vector of the ent

				// If the entity has a light_origin and model_center,
				// calculate a new origin
				if (has_key_value(&ent, u8"light_origin")
					&& has_key_value(&ent, u8"model_center")) {
					auto maybeEnt2 = find_target_entity(
						value_for_key(&ent, u8"light_origin")
					);

					if (maybeEnt2) {
						float3_array light_origin = get_float3_for_key(
							maybeEnt2.value(), u8"origin"
						);
						float3_array model_center = get_float3_for_key(
							ent, u8"model_center"
						);
						VectorSubtract(
							light_origin, model_center, origin
						); // New origin
					}
				}
			}
			bool opaque = false;
			{
				if (g_allow_opaques
					&& (IntForKey(&ent, u8"zhlt_lightflags")
						& eModelLightmodeOpaque
					)) { // If -noopaque is off, and if the entity has
						 // opaque light flag
					opaque = true;
				}
			}

			std::optional<float3_array> transparency;
			{
				// If the entity has a custom shadow (transparency) value
				std::u8string_view transparencyString = value_for_key(
					&ent, u8"zhlt_customshadow"
				);
				if (!transparencyString.empty()) {
					float r, g, b;
					// Try to read RGB values
					if (sscanf(
							(char const *) transparencyString.data(),
							"%f %f %f",
							&r,
							&g,
							&b
						)
						== 3) {
						r = std::max(0.0f, r);
						g = std::max(0.0f, g);
						b = std::max(0.0f, b);
						transparency = { r, g, b };
					} else if (sscanf(
								   (char const *) transparencyString.data(),
								   "%f",
								   &r
							   )
							   == 1) {
						// Greyscale version
						// Set transparency values to the same greyscale
						// value
						r = std::max(0.0f, r);
						transparency = { r, r, r };
					}
				}
				if (transparency == float3_array{ 0.0f, 0.0f, 0.0f }) {
					// 0 transparency is no transparency
					transparency = std::nullopt;
				}
			}
			int opaquestyle = -1;
			{
				int j;

				for (j = 0; j < g_numentities;
					 j++) // Loop to find a matching light_shadow entity
				{
					entity_t* lightent = &g_entities[j];

					if (classname_is(
							lightent, u8"light_shadow"
						) // If light_shadow targeting the current entity
						&& has_key_value(lightent, u8"target")
						&& key_value_is(
							lightent,
							u8"target",
							value_for_key(&ent, u8"targetname")
						)) {
						opaquestyle = IntForKey(
							lightent, u8"style"
						); // Get the style number and validate it

						if (opaquestyle < 0) {
							opaquestyle = -opaquestyle;
						}
						opaquestyle = (unsigned char) opaquestyle;

						if (opaquestyle >= ALLSTYLES) {
							Error(
								"invalid light style: style (%d) >= ALLSTYLES (%d)",
								opaquestyle,
								ALLSTYLES
							);
						}
						break;
					}
				}
			}
			bool block = false;
			{
				if (g_blockopaque) // If opaque blocking is enabled
				{
					block = true;

					if (IntForKey(&ent, u8"zhlt_lightflags")
						& eModelLightmodeNonsolid) { // If entity non-solid
													 // or has transparency
													 // or a specific style,
													 // which would prevent
													 // it from blocking
						block = false;
					}
					if (transparency) {
						block = false;
					}
					if (opaquestyle != -1) {
						block = false;
					}
				}
			}
			if (opaque) // If opaque add it to the opaque list with its
						// properties
			{
				AddFaceToOpaqueList(
					entnum,
					modelnum,
					origin,
					transparency,
					opaquestyle,
					block
				);
			}
		}
	}
	{
		Log("%zu opaque models\n", g_opaque_face_list.size());
		int i, facecount;

		for (facecount = 0, i = 0; i < g_opaque_face_list.size(); i++) {
			facecount += CountOpaqueFaces(g_opaque_face_list[i].modelnum);
		}
		Log("%i opaque faces\n", facecount);
	}
}

// =====================================================================================
//  MakePatches
// =====================================================================================
static entity_t* FindTexlightEntity(int facenum) {
	dface_t* face = &g_dfaces[facenum];
	dplane_t const * dplane = getPlaneFromFace(face);
	wad_texture_name const texname{ get_texture_by_number(face->texinfo) };
	entity_t* faceent = g_face_entity[facenum];
	float3_array centroid{ fast_winding(*face).getCenter() };
	VectorAdd(centroid, g_face_offset[facenum], centroid);

	entity_t* found = nullptr;
	float bestdist = -1;
	for (int i = 0; i < g_numentities; i++) {
		entity_t& ent = g_entities[i];
		if (!classname_is(&ent, u8"light_surface")) {
			continue;
		}
		if (!key_value_is(&ent, u8"_tex", texname)) {
			continue;
		}
		float3_array delta{ get_float3_for_key(ent, u8"origin") };
		VectorSubtract(delta, centroid, delta);
		float dist = vector_length(delta);
		if (has_key_value(&ent, u8"_frange")) {
			if (dist > float_for_key(ent, u8"_frange")) {
				continue;
			}
		}
		if (has_key_value(&ent, u8"_fdist")) {
			if (fabs(DotProduct(delta, dplane->normal))
				> float_for_key(ent, u8"_fdist")) {
				continue;
			}
		}
		if (has_key_value(&ent, u8"_fclass")) {
			if (value_for_key(faceent, u8"classname")
				!= value_for_key(&ent, u8"_fclass")) {
				continue;
			}
		}
		if (has_key_value(&ent, u8"_fname")) {
			if (value_for_key(faceent, u8"targetname")
				!= value_for_key(&ent, u8"_fname")) {
				continue;
			}
		}
		if (bestdist >= 0 && dist > bestdist) {
			continue;
		}
		found = &ent;
		bestdist = dist;
	}
	return found;
}

static void MakePatches() {
	int i;
	int j;
	unsigned int k;
	dface_t* f;
	int fn;
	fast_winding* w;
	dmodel_t* mod;
	entity_t* ent;
	eModelLightmodes lightmode;

	int style; // LRC

	Log("%i faces\n", g_numfaces);

	Log("Create Patches : ");
	g_patches = new patch_t[MAX_PATCHES]();

	for (i = 0; i < g_nummodels; i++) {
		lightmode = eModelLightmodeNull;

		mod = g_dmodels.data() + i;
		ent = EntityForModel(i);

		std::u8string_view zhltLightFlagsString = value_for_key(
			ent, u8"zhlt_lightflags"
		);
		if (!zhltLightFlagsString.empty()) {
			lightmode = (eModelLightmodes
			) atoi((char const *) zhltLightFlagsString.data());
		}

		float3_array origin{};
		std::u8string_view originString = value_for_key(ent, u8"origin");
		// models with origin brushes need to be offset into their in-use
		// position
		if (!originString.empty()) {
			double v1, v2, v3;

			if (sscanf(
					(char const *) originString.data(),
					"%lf %lf %lf",
					&v1,
					&v2,
					&v3
				)
				== 3) {
				origin[0] = v1;
				origin[1] = v2;
				origin[2] = v3;
			}
		}

		std::optional<float3_array> lightOrigin;
		std::u8string_view lightOriginString = value_for_key(
			ent, u8"light_origin"
		);
		// Allow models to be lit in an alternate location (pt1)
		if (!lightOriginString.empty()) {
			auto maybeE = find_target_entity(lightOriginString);
			if (maybeE) {
				std::u8string_view targetOriginString{
					value_for_key(&maybeE.value().get(), u8"origin")
				};
				if (!targetOriginString.empty()) {
					float v1, v2, v3;
					if (sscanf(
							(char const *) targetOriginString.data(),
							"%f %f %f",
							&v1,
							&v2,
							&v3
						)
						== 3) {
						lightOrigin = { v1, v2, v3 };
					}
				}
			}
		}

		std::optional<float3_array> modelCenter;
		std::u8string_view modelCenterString = value_for_key(
			ent, u8"model_center"
		);
		// Allow models to be lit in an alternate location (pt2)
		if (!modelCenterString.empty()) {
			float v1, v2, v3;
			if (sscanf(
					(char const *) modelCenterString.data(),
					"%f %f %f",
					&v1,
					&v2,
					&v3
				)
				== 3) {
				modelCenter = { v1, v2, v3 };
			}
		}

		// Allow models to be lit in an alternate location (pt3)
		if (lightOrigin && modelCenter) {
			VectorSubtract(
				lightOrigin.value(), modelCenter.value(), origin
			);
		}

		// LRC:
		std::u8string_view styleString = value_for_key(ent, u8"style");
		if (!styleString.empty()) {
			style = atoi((char const *) styleString.data());
			if (style < 0) {
				style = -style;
			}
		} else {
			style = 0;
		}
		// LRC (ends)
		style = (unsigned char) style;
		if (style >= ALLSTYLES) {
			Error(
				"invalid light style: style (%d) >= ALLSTYLES (%d)",
				style,
				ALLSTYLES
			);
		}
		int bouncestyle = -1;
		{
			int j;
			for (j = 0; j < g_numentities; j++) {
				entity_t* lightent = &g_entities[j];
				if (classname_is(lightent, u8"light_bounce")
					&& has_key_value(lightent, u8"target")
					&& key_value_is(
						lightent,
						u8"target",
						value_for_key(ent, u8"targetname")
					)) {
					bouncestyle = IntForKey(lightent, u8"style");
					if (bouncestyle < 0) {
						bouncestyle = -bouncestyle;
					}
					bouncestyle = (unsigned char) bouncestyle;
					if (bouncestyle >= ALLSTYLES) {
						Error(
							"invalid light style: style (%d) >= ALLSTYLES (%d)",
							bouncestyle,
							ALLSTYLES
						);
					}
					break;
				}
			}
		}

		for (j = 0; j < mod->numfaces; j++) {
			fn = mod->firstface + j;
			g_face_entity[fn] = ent;
			VectorCopy(origin, g_face_offset[fn]);
			g_face_texlights[fn] = FindTexlightEntity(fn);
			g_face_lightmode[fn] = lightmode;
			f = &g_dfaces[fn];
			w = new fast_winding(*f);
			for (k = 0; k < w->size(); k++) {
				VectorAdd(w->m_Points[k], origin, w->m_Points[k]);
			}
			MakePatchForFace(fn, w, style, bouncestyle); // LRC
		}
	}

	Log("%i base patches\n", g_num_patches);
	Log("%i square feet [%.2f square inches]\n",
		(int) (totalarea / 144),
		totalarea);
}

// =====================================================================================
//  patch_sorter
// =====================================================================================
static int patch_sorter(void const * p1, void const * p2) {
	patch_t* patch1 = (patch_t*) p1;
	patch_t* patch2 = (patch_t*) p2;

	if (patch1->faceNumber < patch2->faceNumber) {
		return -1;
	} else if (patch1->faceNumber > patch2->faceNumber) {
		return 1;
	} else {
		return 0;
	}
}

// =====================================================================================
//  patch_sorter
//      This sorts the patches by facenumber, which makes their runs
//      compress even better
// =====================================================================================
static void SortPatches() {
	// SortPatches is the ideal place to do this, because the address of the
	// patches are going to be invalidated.
	patch_t* old_patches = g_patches;
	g_patches = new patch_t[(g_num_patches + 1)](
	); // allocate one extra slot considering how terribly the code were
	   // written
	memcpy(g_patches, old_patches, g_num_patches * sizeof(patch_t));
	delete[] old_patches;
	qsort(
		(void*) g_patches,
		(size_t) g_num_patches,
		sizeof(patch_t),
		patch_sorter
	);

	// Fixup g_face_patches & Fixup patch->next
	memset(g_face_patches, 0, sizeof(g_face_patches));
	{
		unsigned x;
		patch_t* patch = g_patches + 1;
		patch_t* prev = g_patches;

		g_face_patches[prev->faceNumber] = prev;

		for (x = 1; x < g_num_patches; x++, patch++) {
			if (patch->faceNumber != prev->faceNumber) {
				prev->next = nullptr;
				g_face_patches[patch->faceNumber] = patch;
			} else {
				prev->next = patch;
			}
			prev = patch;
		}
	}
	for (unsigned x = 0; x < g_num_patches; x++) {
		patch_t* patch = &g_patches[x];
		patch->leafnum = PointInLeaf(patch->origin) - g_dleafs.data();
	}
}

// =====================================================================================
//  FreePatches
// =====================================================================================
static void FreePatches() {
	unsigned x;
	patch_t* patch = g_patches;

	// AJM EX
	// Log("patches: %i of %i (%2.2lf percent)\n", g_num_patches,
	// MAX_PATCHES, (double)((double)g_num_patches / (double)MAX_PATCHES));

	for (x = 0; x < g_num_patches; x++, patch++) {
		delete patch->winding;
	}
	memset(g_patches, 0, sizeof(patch_t) * g_num_patches);
	delete[] g_patches;
	g_patches = nullptr;
}

//=====================================================================

// =====================================================================================
//  WriteWorld
// =====================================================================================
static void WriteWorld(char const * const name) {
	unsigned i;
	unsigned j;
	FILE* out;
	patch_t* patch;
	fast_winding* w;

	out = fopen(name, "w");

	if (!out) {
		Error("Couldn't open %s", name);
	}

	for (j = 0, patch = g_patches; j < g_num_patches; j++, patch++) {
		w = patch->winding;
		Log("%zu\n", w->size());
		for (i = 0; i < w->size(); i++) {
			Log("%5.2f %5.2f %5.2f %5.3f %5.3f %5.3f\n",
				w->m_Points[i][0],
				w->m_Points[i][1],
				w->m_Points[i][2],
				patch->totallight[0][0] / 256,
				patch->totallight[0][1] / 256,
				patch->totallight[0][2] / 256); // LRC
		}
		Log("\n");
	}

	fclose(out);
}

// =====================================================================================
//  CollectLight
// =====================================================================================
static void CollectLight() {
	unsigned j; // LRC
	unsigned i;
	patch_t* patch;

	for (i = 0, patch = g_patches; i < g_num_patches; i++, patch++) {
		std::array<float3_array, MAXLIGHTMAPS> newtotallight;
		for (j = 0; j < MAXLIGHTMAPS && newstyles[i][j] != 255; j++) {
			VectorClear(newtotallight[j]);
			int k;
			for (k = 0; k < MAXLIGHTMAPS && patch->totalstyle[k] != 255;
				 k++) {
				if (patch->totalstyle[k] == newstyles[i][j]) {
					VectorCopy(patch->totallight[k], newtotallight[j]);
					break;
				}
			}
		}
		for (j = 0; j < MAXLIGHTMAPS; j++) {
			if (newstyles[i][j] != 255) {
				patch->totalstyle[j] = newstyles[i][j];
				VectorCopy(newtotallight[j], patch->totallight[j]);
				VectorCopy(addlight[i][j], emitlight[i][j]);
			} else {
				patch->totalstyle[j] = 255;
			}
		}
	}
}

// =====================================================================================
//  GatherLight
//      Get light from other g_patches
//      Run multi-threaded
// =====================================================================================

static void GatherLight(int threadnum) {
	int j;
	patch_t* patch;

	unsigned k, m;

	unsigned iIndex;
	transfer_data_t* tData;
	transfer_index_t* tIndex;
	std::array<float3_array, ALLSTYLES> adds;
	unsigned int fastfind_index = 0;

	while (1) {
		j = GetThreadWork();
		if (j == -1) {
			break;
		}
		adds = {};

		patch = &g_patches[j];

		tData = patch->tData;
		tIndex = patch->tIndex;
		iIndex = patch->iIndex;

		for (m = 0; m < MAXLIGHTMAPS && patch->totalstyle[m] != 255; m++) {
			VectorAdd(
				adds[patch->totalstyle[m]],
				patch->totallight[m],
				adds[patch->totalstyle[m]]
			);
		}

		for (k = 0; k < iIndex; k++, tIndex++) {
			unsigned l;
			unsigned size = (tIndex->size + 1);
			unsigned patchnum = tIndex->index;

			for (l = 0; l < size; l++,
				tData += float_size[(std::size_t) g_transfer_compress_type],
				patchnum++) {
				// LRC:
				patch_t* emitpatch = &g_patches[patchnum];
				unsigned emitstyle;
				int opaquestyle = -1;
				GetStyle(j, patchnum, opaquestyle, fastfind_index);
				float const f = float_decompress(
					g_transfer_compress_type, (std::byte*) tData
				);

				// for each style on the emitting patch
				for (emitstyle = 0; emitstyle < MAXLIGHTMAPS
					 && emitpatch->directstyle[emitstyle] != 255;
					 emitstyle++) {
					float3_array v = vector_multiply(
						vector_scale(emitpatch->directlight[emitstyle], f),
						emitpatch->bouncereflectivity
					);
					if (is_point_finite(v)) [[likely]] {
						int addstyle = emitpatch->directstyle[emitstyle];
						if (emitpatch->bouncestyle != -1) {
							if (addstyle == 0
								|| addstyle == emitpatch->bouncestyle) {
								addstyle = emitpatch->bouncestyle;
							} else {
								continue;
							}
						}
						if (opaquestyle != -1) {
							if (addstyle == 0 || addstyle == opaquestyle) {
								addstyle = opaquestyle;
							} else {
								continue;
							}
						}
						VectorAdd(adds[addstyle], v, adds[addstyle]);
					}
				}
				for (emitstyle = 0; emitstyle < MAXLIGHTMAPS
					 && emitpatch->totalstyle[emitstyle] != 255;
					 emitstyle++) {
					float3_array v = vector_multiply(
						vector_scale(emitlight[patchnum][emitstyle], f),
						emitpatch->bouncereflectivity
					);
					if (is_point_finite(v)) [[likely]] {
						int addstyle = emitpatch->totalstyle[emitstyle];
						if (emitpatch->bouncestyle != -1) {
							if (addstyle == 0
								|| addstyle == emitpatch->bouncestyle) {
								addstyle = emitpatch->bouncestyle;
							} else {
								continue;
							}
						}
						if (opaquestyle != -1) {
							if (addstyle == 0 || addstyle == opaquestyle) {
								addstyle = opaquestyle;
							} else {
								continue;
							}
						}
						VectorAdd(adds[addstyle], v, adds[addstyle]);
					} else {
						Verbose(
							"GatherLight, v (%4.3f %4.3f %4.3f)@(%4.3f %4.3f %4.3f)\n",
							v[0],
							v[1],
							v[2],
							patch->origin[0],
							patch->origin[1],
							patch->origin[2]
						);
					}
				}
				// LRC (ends)
			}
		}

		float maxlights[ALLSTYLES];
		for (std::size_t style = 0; style < ALLSTYLES; style++) {
			maxlights[style] = VectorMaximum(adds[style]);
		}
		for (m = 0; m < MAXLIGHTMAPS; m++) {
			unsigned char beststyle = 255;
			if (m == 0) {
				beststyle = 0;
			} else {
				float bestmaxlight = 0;
				for (std::size_t style = 1; style < ALLSTYLES; style++) {
					if (maxlights[style] > bestmaxlight + NORMAL_EPSILON) {
						bestmaxlight = maxlights[style];
						beststyle = style;
					}
				}
			}
			if (beststyle != 255) {
				maxlights[beststyle] = 0;
				newstyles[j][m] = beststyle;
				VectorCopy(adds[beststyle], addlight[j][m]);
			} else {
				newstyles[j][m] = 255;
			}
		}
		for (std::size_t style = 1; style < ALLSTYLES; style++) {
			if (maxlights[style] > g_maxdiscardedlight + NORMAL_EPSILON) {
				ThreadLock();
				if (maxlights[style]
					> g_maxdiscardedlight + NORMAL_EPSILON) {
					g_maxdiscardedlight = maxlights[style];
					g_maxdiscardedpos = patch->origin;
				}
				ThreadUnlock();
			}
		}
	}
}

// RGB Transfer version
static void GatherRGBLight(int threadnum) {
	int j;
	patch_t* patch;

	unsigned k, m;

	unsigned iIndex;
	rgb_transfer_data_t* tRGBData;
	transfer_index_t* tIndex;
	float3_array f;
	std::array<float3_array, ALLSTYLES> adds;
	int style;
	unsigned int fastfind_index = 0;

	while (1) {
		j = GetThreadWork();
		if (j == -1) {
			break;
		}
		adds = {};

		patch = &g_patches[j];

		tRGBData = patch->tRGBData;
		tIndex = patch->tIndex;
		iIndex = patch->iIndex;

		for (m = 0; m < MAXLIGHTMAPS && patch->totalstyle[m] != 255; m++) {
			VectorAdd(
				adds[patch->totalstyle[m]],
				patch->totallight[m],
				adds[patch->totalstyle[m]]
			);
		}

		for (k = 0; k < iIndex; k++, tIndex++) {
			unsigned l;
			unsigned size = (tIndex->size + 1);
			unsigned patchnum = tIndex->index;
			for (l = 0; l < size;
				 l++,
				tRGBData
				 += vector_size[(std::size_t) g_rgbtransfer_compress_type],
				patchnum++) {
				// LRC:
				patch_t* emitpatch = &g_patches[patchnum];
				unsigned emitstyle;
				int opaquestyle = -1;
				GetStyle(j, patchnum, opaquestyle, fastfind_index);
				vector_decompress(
					g_rgbtransfer_compress_type,
					tRGBData,
					&f[0],
					&f[1],
					&f[2]
				);

				// for each style on the emitting patch
				for (emitstyle = 0; emitstyle < MAXLIGHTMAPS
					 && emitpatch->directstyle[emitstyle] != 255;
					 emitstyle++) {
					float3_array v = vector_multiply(
						vector_multiply(emitlight[patchnum][emitstyle], f),
						emitpatch->bouncereflectivity
					);
					if (is_point_finite(v)) [[likely]] {
						int addstyle = emitpatch->directstyle[emitstyle];
						if (emitpatch->bouncestyle != -1) {
							if (addstyle == 0
								|| addstyle == emitpatch->bouncestyle) {
								addstyle = emitpatch->bouncestyle;
							} else {
								continue;
							}
						}
						if (opaquestyle != -1) {
							if (addstyle == 0 || addstyle == opaquestyle) {
								addstyle = opaquestyle;
							} else {
								continue;
							}
						}
						VectorAdd(adds[addstyle], v, adds[addstyle]);
					}
				}
				for (emitstyle = 0; emitstyle < MAXLIGHTMAPS
					 && emitpatch->totalstyle[emitstyle] != 255;
					 emitstyle++) {
					float3_array v = vector_multiply(
						vector_multiply(emitlight[patchnum][emitstyle], f),
						emitpatch->bouncereflectivity
					);
					if (is_point_finite(v)) [[likely]] {
						int addstyle = emitpatch->totalstyle[emitstyle];
						if (emitpatch->bouncestyle != -1) {
							if (addstyle == 0
								|| addstyle == emitpatch->bouncestyle) {
								addstyle = emitpatch->bouncestyle;
							} else {
								continue;
							}
						}
						if (opaquestyle != -1) {
							if (addstyle == 0 || addstyle == opaquestyle) {
								addstyle = opaquestyle;
							} else {
								continue;
							}
						}
						VectorAdd(adds[addstyle], v, adds[addstyle]);
					} else {
						Verbose(
							"GatherLight, v (%4.3f %4.3f %4.3f)@(%4.3f %4.3f %4.3f)\n",
							v[0],
							v[1],
							v[2],
							patch->origin[0],
							patch->origin[1],
							patch->origin[2]
						);
					}
				}
				// LRC (ends)
			}
		}

		float maxlights[ALLSTYLES];
		for (style = 0; style < ALLSTYLES; style++) {
			maxlights[style] = VectorMaximum(adds[style]);
		}
		for (m = 0; m < MAXLIGHTMAPS; m++) {
			unsigned char beststyle = 255;
			if (m == 0) {
				beststyle = 0;
			} else {
				float bestmaxlight = 0;
				for (style = 1; style < ALLSTYLES; style++) {
					if (maxlights[style] > bestmaxlight + NORMAL_EPSILON) {
						bestmaxlight = maxlights[style];
						beststyle = style;
					}
				}
			}
			if (beststyle != 255) {
				maxlights[beststyle] = 0;
				newstyles[j][m] = beststyle;
				VectorCopy(adds[beststyle], addlight[j][m]);
			} else {
				newstyles[j][m] = 255;
			}
		}
		for (style = 1; style < ALLSTYLES; style++) {
			if (maxlights[style] > g_maxdiscardedlight + NORMAL_EPSILON) {
				ThreadLock();
				if (maxlights[style]
					> g_maxdiscardedlight + NORMAL_EPSILON) {
					g_maxdiscardedlight = maxlights[style];
					g_maxdiscardedpos = patch->origin;
				}
				ThreadUnlock();
			}
		}
	}
}

// =====================================================================================
//  BounceLight
// =====================================================================================
static void BounceLight() {
	// these arrays are only used in CollectLight, GatherLight and
	// BounceLight
	emitlight
		= new std::array<float3_array, MAXLIGHTMAPS>[g_num_patches + 1]();
	addlight
		= new std::array<float3_array, MAXLIGHTMAPS>[g_num_patches + 1]();
	newstyles
		= new std::array<unsigned char, MAXLIGHTMAPS>[g_num_patches + 1]();

	unsigned i;
	char name[64];

	unsigned j; // LRC

	for (i = 0; i < g_num_patches; i++) {
		patch_t* patch = &g_patches[i];
		for (j = 0; j < MAXLIGHTMAPS && patch->totalstyle[j] != 255; j++) {
			VectorCopy(patch->totallight[j], emitlight[i][j]);
		}
	}

	for (i = 0; i < g_numbounce; i++) {
		Log("Bounce %u ", i + 1);
		if (g_rgb_transfers) {
			NamedRunThreadsOn(g_num_patches, g_estimate, GatherRGBLight);
		} else {
			NamedRunThreadsOn(g_num_patches, g_estimate, GatherLight);
		}
		CollectLight();

		if (g_dumppatches) {
			snprintf(name, sizeof(name), "bounce%u.txt", i);
			WriteWorld(name);
		}
	}
	for (i = 0; i < g_num_patches; i++) {
		patch_t* patch = &g_patches[i];
		for (j = 0; j < MAXLIGHTMAPS && patch->totalstyle[j] != 255; j++) {
			VectorCopy(emitlight[i][j], patch->totallight[j]);
		}
	}

	delete[] (emitlight);
	emitlight = nullptr;
	delete[] (addlight);
	addlight = nullptr;
	delete[] newstyles;
	newstyles = nullptr;
}

// =====================================================================================
//  CheckMaxPatches
// =====================================================================================
static void CheckMaxPatches() {
	switch (g_method) {
		case vis_method::vismatrix:
			hlassume(
				g_num_patches <= MAX_VISMATRIX_PATCHES, assume_MAX_PATCHES
			);
			break;
		case vis_method::sparse_vismatrix:
			hlassume(
				g_num_patches <= MAX_SPARSE_VISMATRIX_PATCHES,
				assume_MAX_PATCHES
			);
			break;
		case vis_method::no_vismatrix:
			hlassume(g_num_patches <= MAX_PATCHES, assume_MAX_PATCHES);
			break;
	}
}

// =====================================================================================
//  MakeScalesStub
// =====================================================================================
static void MakeScalesStub() {
	switch (g_method) {
		case vis_method::vismatrix:
			MakeScalesVismatrix();
			break;
		case vis_method::sparse_vismatrix:
			MakeScalesSparseVismatrix();
			break;
		case vis_method::no_vismatrix:
			MakeScalesNoVismatrix();
			break;
	}
}

// =====================================================================================
//  FreeTransfers
// =====================================================================================
static void FreeTransfers() {
	patch_t* patch = g_patches;
	for (std::size_t x = 0; x < g_num_patches; x++, patch++) {
		if (patch->tData) {
			delete[] patch->tData;
			patch->tData = nullptr;
		}
		if (patch->tRGBData) {
			delete[] patch->tRGBData;
			patch->tRGBData = nullptr;
		}
		if (patch->tIndex) {
			delete[] patch->tIndex;
			patch->tIndex = nullptr;
		}
	}
}

static void ExtendLightmapBuffer() {
	int maxsize;
	int i;
	int j;
	int ofs;
	dface_t* f;

	maxsize = 0;
	for (i = 0; i < g_numfaces; i++) {
		f = &g_dfaces[i];
		if (f->lightofs >= 0) {
			ofs = f->lightofs;
			for (j = 0; j < MAXLIGHTMAPS && f->styles[j] != 255; j++) {
				ofs += (MAX_SURFACE_EXTENT + 1) * (MAX_SURFACE_EXTENT + 1)
					* 3;
			}
			if (ofs > maxsize) {
				maxsize = ofs;
			}
		}
	}
	if (maxsize >= g_dlightdata.size()) {
		hlassume(maxsize <= g_max_map_lightdata, assume_MAX_MAP_LIGHTING);

		g_dlightdata.resize(maxsize, std::byte(0));
	}
}

std::array<float3_array, 15> const pos{
	float3_array{ 0, 0, 0 },  float3_array{ 1, 0, 0 },
	float3_array{ 0, 1, 0 },  float3_array{ -1, 0, 0 },
	float3_array{ 0, -1, 0 }, float3_array{ 1, 0, 0 },
	float3_array{ 0, 0, 1 },  float3_array{ -1, 0, 0 },
	float3_array{ 0, 0, -1 }, float3_array{ 0, -1, 0 },
	float3_array{ 0, 0, 1 },  float3_array{ 0, 1, 0 },
	float3_array{ 0, 0, -1 }, float3_array{ 1, 0, 0 },
	float3_array{ 0, 0, 0 }
};

// =====================================================================================
//  RadWorld
// =====================================================================================
static void RadWorld() {
	MakeBackplanes();
	MakeParents(0, -1);
	MakeTnodes(&g_dmodels[0]);
	CreateOpaqueNodes();
	LoadOpaqueEntities();

	// turn each face into a single patch
	MakePatches();
	if (g_drawpatch) {
		char name[_MAX_PATH + 20];
		snprintf(name, sizeof(name), "%s_patch.pts", g_Mapname);
		Log("Writing '%s' ...\n", name);
		FILE* f;
		f = fopen(name, "w");
		if (f) {
			patch_t const * patch = g_patches;
			for (std::size_t j = 0; j < g_num_patches; j++, patch++) {
				if (patch->flags == ePatchFlagOutside) {
					continue;
				}

				float3_array const v{ patch->origin };
				for (float3_array const & p : pos) {
					fprintf(
						f,
						"%g %g %g\n",
						v[0] + p[0],
						v[1] + p[1],
						v[2] + p[2]
					);
				}
			}
			fclose(f);
			Log("OK.\n");
		} else {
			Log("Error.\n");
		}
	}
	CheckMaxPatches(); // Check here for exceeding max patches, to prevent a
					   // lot of work from occuring before an error occurs
	SortPatches(); // Makes the runs in the Transfer Compression really good
	PairEdges();
	if (g_drawedge) {
		char name[_MAX_PATH + 20];
		snprintf(name, sizeof(name), "%s_edge.pts", g_Mapname);
		Log("Writing '%s' ...\n", name);
		FILE* f;
		f = fopen(name, "w");
		if (f) {
			edgeshare_t const * es{ g_edgeshare };
			for (std::size_t j = 0; j < MAX_MAP_EDGES; j++, es++) {
				if (es->smooth) {
					int v0 = g_dedges[j].v[0], v1 = g_dedges[j].v[1];
					float3_array v;
					VectorAdd(
						g_dvertexes[v0].point, g_dvertexes[v1].point, v
					);
					VectorScale(v, 0.5, v);
					VectorAdd(v, es->interface_normal, v);
					VectorAdd(
						v, g_face_offset[es->faces[0] - g_dfaces.data()], v
					);
					for (float3_array const & p : pos) {
						fprintf(
							f,
							"%g %g %g\n",
							v[0] + p[0],
							v[1] + p[1],
							v[2] + p[2]
						);
					}
				}
			}
			fclose(f);
			Log("OK.\n");
		} else {
			Log("Error.\n");
		}
	}

	BuildDiffuseNormals();
	// create directlights out of g_patches and lights
	CreateDirectLights();
	LoadStudioModels(); // seedee
	Log("\n");

	// generate a position map for each face
	NamedRunThreadsOnIndividual(g_numfaces, g_estimate, FindFacePositions);

	// build initial facelights
	NamedRunThreadsOnIndividual(g_numfaces, g_estimate, BuildFacelights);

	FreePositionMaps();

	// free up the direct lights now that we have facelights
	DeleteDirectLights();

	if (g_numbounce > 0) {
		// build transfer lists
		MakeScalesStub();

		// spread light around
		BounceLight();
	}

	FreeTransfers();
	FreeStyleArrays();

	NamedRunThreadsOnIndividual(
		g_numfaces, g_estimate, CreateTriangulations
	);

	// blend bounced light into direct light and save
	PrecompLightmapOffsets();

	ScaleDirectLights();

	{
		CreateFacelightDependencyList();

		NamedRunThreadsOnIndividual(g_numfaces, g_estimate, AddPatchLights);

		FreeFacelightDependencyList();
	}

	FreeTriangulations();

	NamedRunThreadsOnIndividual(g_numfaces, g_estimate, FinalLightFace);
	if (g_maxdiscardedlight > 0.01) {
		Verbose(
			"Maximum brightness loss (too many light styles on a face) = %f @(%f, %f, %f)\n",
			g_maxdiscardedlight,
			g_maxdiscardedpos[0],
			g_maxdiscardedpos[1],
			g_maxdiscardedpos[2]
		);
	}
	MdlLightHack();
	ReduceLightmap();
	if (g_dlightdata.empty()) {
		g_dlightdata.push_back(std::byte(0));
	}
	ExtendLightmapBuffer(
	); // expand the size of lightdata array (for a few KB) to ensure that
	   // game engine reads within its valid range
}

// =====================================================================================
//  Usage
// =====================================================================================
static void Usage() {
	Banner();

	Log("\n-= %s Options =-\n\n", g_Program);
	Log("    -waddir folder  : Search this folder for wad files.\n");
	Log("    -fast           : Fast rad\n");
	Log("    -vismatrix value: Set vismatrix method to normal, sparse or off.\n"
	);
	Log("    -pre25          : Optimize compile for pre-Half-Life 25th anniversary update.\n"
	);
	Log("    -extra          : Improve lighting quality by doing 9 point oversampling\n"
	);
	Log("    -bounce #       : Set number of radiosity bounces\n");
	Log("    -ambient r g b  : Set ambient world light (0.0 to 1.0, r g b)\n"
	);
	Log("    -limiter #      : Set light clipping threshold (-1=None)\n");
	Log("    -circus         : Enable 'circus' mode for locating unlit lightmaps\n"
	);
	Log("    -nospread       : Disable sunlight spread angles for this compile\n"
	);
	Log("    -nopaque        : Disable the opaque zhlt_lightflags for this compile\n\n"
	);
	Log("    -nostudioshadow : Disable opaque studiomodels, ignore zhlt_studioshadow for this compile\n\n"
	);
	Log("    -smooth #       : Set smoothing threshold for blending (in degrees)\n"
	);
	Log("    -smooth2 #      : Set smoothing threshold between different textures\n"
	);
	Log("    -chop #         : Set radiosity patch size for normal textures\n"
	);
	Log("    -texchop #      : Set radiosity patch size for texture light faces\n\n"
	);
	Log("    -notexscale     : Do not scale radiosity patches with texture scale\n"
	);
	Log("    -coring #       : Set lighting threshold before blackness\n");
	Log("    -dlight #       : Set direct lighting threshold\n");
	Log("    -nolerp         : Disable radiosity interpolation, nearest point instead\n\n"
	);
	Log("    -fade #         : Set global fade (larger values = shorter lights)\n"
	);
	Log("    -texlightgap #  : Set global gap distance for texlights\n");
	Log("    -scale #        : Set global light scaling value\n");
	Log("    -gamma #        : Set global gamma value\n\n");
	Log("    -sky #          : Set ambient sunlight contribution in the shade outside\n"
	);
	Log("    -lights file    : Manually specify a lights.rad file to use\n"
	);
	Log("    -noskyfix       : Disable light_environment being global\n");
	Log("    -incremental    : Use or create an incremental transfer list file\n\n"
	);
	Log("    -dump           : Dumps light patches to a file for hlrad debugging info\n\n"
	);
	Log("    -texdata #      : Alter maximum texture memory limit (in kb)\n"
	);
	Log("    -chart          : display bsp statitics\n");
	Log("    -low | -high    : run program an altered priority level\n");
	Log("    -nolog          : Do not generate the compile logfiles\n");
	Log("    -threads #      : manually specify the number of threads to run\n"
	);
#ifdef SYSTEM_WIN32
	Log("    -estimate       : display estimated time during compile\n");
#endif
#ifdef SYSTEM_POSIX
	Log("    -noestimate     : Do not display continuous compile time estimates\n"
	);
#endif
	Log("    -verbose        : compile with verbose messages\n");
	Log("    -noinfo         : Do not show tool configuration information\n"
	);
	Log("    -dev #          : compile with developer message\n\n");

	// ------------------------------------------------------------------------
	// Changes by Adam Foster - afoster@compsoc.man.ac.uk

	// AJM: we dont need this extra crap
	// Log("-= Unofficial features added by Adam Foster
	// (afoster@compsoc.man.ac.uk) =-\n\n");
	Log("   -colourgamma r g b  : Sets different gamma values for r, g, b\n"
	);
	Log("   -colourscale r g b  : Sets different lightscale values for r, g ,b\n"
	);
	Log("   -colourjitter r g b : Adds noise, independent colours, for dithering\n"
	);
	Log("   -jitter r g b       : Adds noise, monochromatic, for dithering\n"
	);
	// Log("-= End of unofficial features! =-\n\n" );

	// ------------------------------------------------------------------------

	Log("   -customshadowwithbounce : Enables custom shadows with bounce light\n"
	);
	Log("   -rgbtransfers           : Enables RGB Transfers (for custom shadows)\n\n"
	);

	Log("   -minlight #    : Minimum final light (integer from 0 to 255)\n"
	);
	{
		int i;
		Log("   -compress #    : compress tranfer (");
		for (i = 0; i < float_type_count; i++) {
			Log(" %d=%s", i, (char const *) float_type_string[i].data());
		}
		Log(" )\n");
		Log("   -rgbcompress # : compress rgbtranfer (");
		for (i = 0; i < vector_type_count; i++) {
			Log(" %d=%s", i, vector_type_string[i]);
		}
		Log(" )\n");
	}
	Log("   -softsky #     : Smooth skylight.(0=off 1=on)\n");
	Log("   -depth #       : Thickness of translucent objects.\n");
	Log("   -blockopaque # : Remove the black areas around opaque entities.(0=off 1=on)\n"
	);
	Log("   -notextures    : Don't load textures.\n");
	Log("   -texreflectgamma # : Gamma that relates reflectivity to texture color bits.\n"
	);
	Log("   -texreflectscale # : Reflectivity for 255-white texture.\n");
	Log("   -blur #        : Enlarge lightmap sample to blur the lightmap.\n"
	);
	Log("   -noemitterrange: Don't fix pointy texlights.\n");
	Log("   -nobleedfix    : Don't fix wall bleeding problem for large blur value.\n"
	);
	Log("   -drawpatch     : Export light patch positions to file 'mapname_patch.pts'.\n"
	);
	Log("   -drawsample x y z r    : Export light sample positions in an area to file 'mapname_sample.pts'.\n"
	);
	Log("   -drawedge      : Export smooth edge positions to file 'mapname_edge.pts'.\n"
	);
	Log("   -drawlerp      : Show bounce light triangulation status.\n");
	Log("   -drawnudge     : Show nudged samples.\n");
	Log("   -drawoverload  : Highlight fullbright spots\n");

	Log("    mapfile       : The mapfile to compile\n\n");

	exit(1);
}

// =====================================================================================
//  Settings
// =====================================================================================
static void Settings() {
	char const * tmp;
	char buf1[1024];
	char buf2[1024];

	if (!g_info) {
		return;
	}

	Log("\n-= Current %s Settings =-\n", g_Program);
	Log("Name                | Setting             | Default\n"
		"--------------------|---------------------|-------------------------\n"
	);

	// ZHLT Common Settings
	Log("threads             [ %17td ] [  Varies ]\n", g_numthreads);
	Log("verbose              [ %17s ] [ %17s ]\n",
		g_verbose ? "on" : "off",
		cli_option_defaults::verbose ? "on" : "off");
	Log("log                  [ %17s ] [ %17s ]\n",
		g_log ? "on" : "off",
		cli_option_defaults::log ? "on" : "off");
	Log("developer            [ %17d ] [ %17d ]\n",
		(int) g_developer,
		(int) cli_option_defaults::developer);
	Log("chart                [ %17s ] [ %17s ]\n",
		g_chart ? "on" : "off",
		cli_option_defaults::chart ? "on" : "off");
	Log("estimate             [ %17s ] [ %17s ]\n",
		g_estimate ? "on" : "off",
		cli_option_defaults::estimate ? "on" : "off");
	Log("max texture memory   [ %17td ] [ %17td ]\n",
		g_max_map_miptex,
		cli_option_defaults::max_map_miptex);

	switch (g_threadpriority) {
		case q_threadpriority::eThreadPriorityNormal:
		default:
			tmp = "Normal";
			break;
		case q_threadpriority::eThreadPriorityLow:
			tmp = "Low";
			break;
		case q_threadpriority::eThreadPriorityHigh:
			tmp = "High";
			break;
	}
	Log("priority             [ %17s ] [ %17s ]\n", tmp, "Normal");
	Log("\n");

	Log("fast rad             [ %17s ] [ %17s ]\n",
		g_fastmode ? "on" : "off",
		DEFAULT_FASTMODE ? "on" : "off");
	Log("vismatrix algorithm  [ %17s ] [ %17s ]\n",
		g_method == vis_method::vismatrix			   ? "Original"
			: g_method == vis_method::sparse_vismatrix ? "Sparse"
			: g_method == vis_method::no_vismatrix	   ? "NoMatrix"
													   : "Unknown",
		cli_option_defaults::visMethod == vis_method::vismatrix ? "Original"
			: cli_option_defaults::visMethod == vis_method::sparse_vismatrix
			? "Sparse"
			: cli_option_defaults::visMethod == vis_method::no_vismatrix
			? "NoMatrix"
			: "Unknown");
	Log("pre-25th anniversary [ %17s ] [ %17s ]\n",
		g_pre25update ? "on" : "off",
		DEFAULT_PRE25UPDATE ? "on" : "off");
	Log("oversampling (-extra)[ %17s ] [ %17s ]\n",
		g_extra ? "on" : "off",
		DEFAULT_EXTRA ? "on" : "off");
	Log("bounces              [ %17d ] [ %17d ]\n",
		g_numbounce,
		DEFAULT_BOUNCE);

	safe_snprintf(
		buf1,
		sizeof(buf1),
		"%1.3f %1.3f %1.3f",
		g_ambient[0],
		g_ambient[1],
		g_ambient[2]
	);
	safe_snprintf(
		buf2,
		sizeof(buf2),
		"%1.3f %1.3f %1.3f",
		DEFAULT_AMBIENT_RED,
		DEFAULT_AMBIENT_GREEN,
		DEFAULT_AMBIENT_BLUE
	);
	Log("ambient light        [ %17s ] [ %17s ]\n", buf1, buf2);
	safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_limitthreshold);
	safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_LIMITTHRESHOLD);
	Log("light limit threshold[ %17s ] [ %17s ]\n",
		g_limitthreshold >= 0 ? buf1 : "None",
		buf2);
	Log("circus mode          [ %17s ] [ %17s ]\n",
		g_circus ? "on" : "off",
		DEFAULT_CIRCUS ? "on" : "off");

	Log("\n");

	safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_smoothing_value);
	safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_SMOOTHING_VALUE);
	Log("smoothing threshold  [ %17s ] [ %17s ]\n", buf1, buf2);
	safe_snprintf(
		buf1,
		sizeof(buf1),
		g_smoothing_value_2 < 0 ? "no change" : "%3.3f",
		g_smoothing_value_2
	);
#if DEFAULT_SMOOTHING2_VALUE
	safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_SMOOTHING2_VALUE);
#else
	safe_snprintf(buf2, sizeof(buf2), "no change");
#endif
	Log("smoothing threshold 2[ %17s ] [ %17s ]\n", buf1, buf2);
	safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_dlight_threshold);
	safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_DLIGHT_THRESHOLD);
	Log("direct threshold     [ %17s ] [ %17s ]\n", buf1, buf2);
	safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_direct_scale);
	safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_DLIGHT_SCALE);
	Log("direct light scale   [ %17s ] [ %17s ]\n", buf1, buf2);
	safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_coring);
	safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_CORING);
	Log("coring threshold     [ %17s ] [ %17s ]\n", buf1, buf2);
	Log("patch interpolation  [ %17s ] [ %17s ]\n",
		g_lerp_enabled ? "on" : "off",
		DEFAULT_LERP_ENABLED ? "on" : "off");

	Log("\n");

	Log("texscale             [ %17s ] [ %17s ]\n",
		g_texscale ? "on" : "off",
		DEFAULT_TEXSCALE ? "on" : "off");
	Log("patch subdividing    [ %17s ] [ %17s ]\n",
		g_subdivide ? "on" : "off",
		DEFAULT_SUBDIVIDE ? "on" : "off");
	safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_chop);
	safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_CHOP);
	Log("chop value           [ %17s ] [ %17s ]\n", buf1, buf2);
	safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_texchop);
	safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_TEXCHOP);
	Log("texchop value        [ %17s ] [ %17s ]\n", buf1, buf2);
	Log("\n");

	safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_fade);
	safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_FADE);
	Log("global fade          [ %17s ] [ %17s ]\n", buf1, buf2);
	safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_texlightgap);
	safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_TEXLIGHTGAP);
	Log("global texlight gap  [ %17s ] [ %17s ]\n", buf1, buf2);

	// ------------------------------------------------------------------------
	// Changes by Adam Foster - afoster@compsoc.man.ac.uk
	// replaces the old stuff for displaying current values for gamma and
	// lightscale
	safe_snprintf(
		buf1,
		sizeof(buf1),
		"%1.3f %1.3f %1.3f",
		g_colour_lightscale[0],
		g_colour_lightscale[1],
		g_colour_lightscale[2]
	);
	safe_snprintf(
		buf2,
		sizeof(buf2),
		"%1.3f %1.3f %1.3f",
		DEFAULT_COLOUR_LIGHTSCALE_RED,
		DEFAULT_COLOUR_LIGHTSCALE_GREEN,
		DEFAULT_COLOUR_LIGHTSCALE_BLUE
	);
	Log("global light scale   [ %17s ] [ %17s ]\n", buf1, buf2);

	safe_snprintf(
		buf1,
		sizeof(buf1),
		"%1.3f %1.3f %1.3f",
		g_colour_qgamma[0],
		g_colour_qgamma[1],
		g_colour_qgamma[2]
	);
	safe_snprintf(
		buf2,
		sizeof(buf2),
		"%1.3f %1.3f %1.3f",
		DEFAULT_COLOUR_GAMMA_RED,
		DEFAULT_COLOUR_GAMMA_GREEN,
		DEFAULT_COLOUR_GAMMA_BLUE
	);
	Log("global gamma         [ %17s ] [ %17s ]\n", buf1, buf2);
	// ------------------------------------------------------------------------

	safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_lightscale);
	safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_LIGHTSCALE);
	Log("global light scale   [ %17s ] [ %17s ]\n", buf1, buf2);

	safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_indirect_sun);
	safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_INDIRECT_SUN);
	Log("global sky diffusion [ %17s ] [ %17s ]\n", buf1, buf2);

	Log("\n");
	Log("spread angles        [ %17s ] [ %17s ]\n",
		g_allow_spread ? "on" : "off",
		DEFAULT_ALLOW_SPREAD ? "on" : "off");
	Log("opaque brush models  [ %17s ] [ %17s ]\n",
		g_allow_opaques ? "on" : "off",
		DEFAULT_ALLOW_OPAQUES ? "on" : "off");
	Log("opaque studio models [ %17s ] [ %17s ]\n",
		g_studioshadow ? "on" : "off",
		DEFAULT_STUDIOSHADOW ? "on" : "off");
	Log("sky lighting fix     [ %17s ] [ %17s ]\n",
		g_sky_lighting_fix ? "on" : "off",
		DEFAULT_SKY_LIGHTING_FIX ? "on" : "off");
	Log("incremental          [ %17s ] [ %17s ]\n",
		g_incremental ? "on" : "off",
		DEFAULT_INCREMENTAL ? "on" : "off");
	Log("dump                 [ %17s ] [ %17s ]\n",
		g_dumppatches ? "on" : "off",
		DEFAULT_DUMPPATCHES ? "on" : "off");

	// ------------------------------------------------------------------------
	// Changes by Adam Foster - afoster@compsoc.man.ac.uk
	// displays information on all the brand-new features :)

	Log("\n");
	safe_snprintf(
		buf1,
		sizeof(buf1),
		"%3.1f %3.1f %3.1f",
		g_colour_jitter_hack[0],
		g_colour_jitter_hack[1],
		g_colour_jitter_hack[2]
	);
	safe_snprintf(
		buf2,
		sizeof(buf2),
		"%3.1f %3.1f %3.1f",
		DEFAULT_COLOUR_JITTER_HACK_RED,
		DEFAULT_COLOUR_JITTER_HACK_GREEN,
		DEFAULT_COLOUR_JITTER_HACK_BLUE
	);
	Log("colour jitter        [ %17s ] [ %17s ]\n", buf1, buf2);
	safe_snprintf(
		buf1,
		sizeof(buf1),
		"%3.1f %3.1f %3.1f",
		g_jitter_hack[0],
		g_jitter_hack[1],
		g_jitter_hack[2]
	);
	safe_snprintf(
		buf2,
		sizeof(buf2),
		"%3.1f %3.1f %3.1f",
		DEFAULT_JITTER_HACK_RED,
		DEFAULT_JITTER_HACK_GREEN,
		DEFAULT_JITTER_HACK_BLUE
	);
	Log("monochromatic jitter [ %17s ] [ %17s ]\n", buf1, buf2);

	// ------------------------------------------------------------------------

	Log("\n");
	Log("custom shadows with bounce light\n"
		"                     [ %17s ] [ %17s ]\n",
		g_customshadow_with_bouncelight ? "on" : "off",
		DEFAULT_CUSTOMSHADOW_WITH_BOUNCELIGHT ? "on" : "off");
	Log("rgb transfers        [ %17s ] [ %17s ]\n",
		g_rgb_transfers ? "on" : "off",
		DEFAULT_RGB_TRANSFERS ? "on" : "off");

	Log("minimum final light  [ %17d ] [ %17d ]\n",
		(int) g_minlight,
		(int) cli_option_defaults::minLight);
	snprintf(
		buf1,
		sizeof(buf1),
		"%zu (%s)",
		(std::size_t) g_transfer_compress_type,
		(char const *)
			float_type_string[(std::size_t) g_transfer_compress_type]
				.data()
	);
	snprintf(
		buf2,
		sizeof(buf2),
		"%zu (%s)",
		(std::size_t) cli_option_defaults::transferCompressType,
		(char const *)
			float_type_string[(std::size_t
							  ) cli_option_defaults::transferCompressType]
				.data()
	);
	Log("size of transfer     [ %17s ] [ %17s ]\n", buf1, buf2);
	snprintf(
		buf1,
		sizeof(buf1),
		"%zu (%s)",
		(std::size_t) g_rgbtransfer_compress_type,
		vector_type_string[(std::size_t) g_rgbtransfer_compress_type]
	);
	snprintf(
		buf2,
		sizeof(buf2),
		"%zu (%s)",
		(std::size_t) cli_option_defaults::rgbTransferCompressType,
		vector_type_string[(std::size_t
		) cli_option_defaults::rgbTransferCompressType]
	);
	Log("size of rgbtransfer  [ %17s ] [ %17s ]\n", buf1, buf2);
	Log("soft sky             [ %17s ] [ %17s ]\n",
		g_softsky ? "on" : "off",
		DEFAULT_SOFTSKY ? "on" : "off");
	safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_translucentdepth);
	safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_TRANSLUCENTDEPTH);
	Log("translucent depth    [ %17s ] [ %17s ]\n", buf1, buf2);
	Log("block opaque         [ %17s ] [ %17s ]\n",
		g_blockopaque ? "on" : "off",
		cli_option_defaults::blockOpaque ? "on" : "off");
	Log("ignore textures      [ %17s ] [ %17s ]\n",
		g_notextures ? "on" : "off",
		DEFAULT_NOTEXTURES ? "on" : "off");
	safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_texreflectgamma);
	safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_TEXREFLECTGAMMA);
	Log("reflectivity gamma   [ %17s ] [ %17s ]\n", buf1, buf2);
	safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_texreflectscale);
	safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_TEXREFLECTSCALE);
	Log("reflectivity scale   [ %17s ] [ %17s ]\n", buf1, buf2);
	safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_blur);
	safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_BLUR);
	Log("blur size            [ %17s ] [ %17s ]\n", buf1, buf2);
	Log("no emitter range     [ %17s ] [ %17s ]\n",
		g_noemitterrange ? "on" : "off",
		DEFAULT_NOEMITTERRANGE ? "on" : "off");
	Log("wall bleeding fix    [ %17s ] [ %17s ]\n",
		g_bleedfix ? "on" : "off",
		DEFAULT_BLEEDFIX ? "on" : "off");

	Log("\n\n");
}

// AJM: added in
// add minlights entity //seedee
// =====================================================================================
//  ReadInfoTexAndMinlights
//      try and parse texlight info from the info_texlights entity
// =====================================================================================
void ReadInfoTexAndMinlights() {
	int k;
	int values;
	float r, g, b, i, min;
	minlight_t minlight;

	for (k = 0; k < g_numentities; k++) {
		entity_t* mapent = &g_entities[k];
		bool foundMinlights = false;
		bool foundTexlights = false;

		if (classname_is(mapent, u8"info_minlights")) {
			Log("Reading per-tex minlights from info_minlights map entity\n"
			);

			for (entity_key_value const & kv : mapent->keyValues) {
				if (kv.key() == u8"classname" || kv.key() == u8"origin") {
					continue; // we dont care about these keyvalues
				}
				if (sscanf((char const *) kv.value().data(), "%f", &min)
					!= 1) {
					Warning(
						"Ignoring bad minlight '%s' in info_minlights entity",
						(char const *) kv.key().data()
					);
					continue;
				}
				std::optional<wad_texture_name> const maybeTextureName
					= wad_texture_name::make_if_legal_name(kv.key());
				if (!maybeTextureName) {
					Warning(
						"Ignoring bad minlight '%s' in info_minlights entity",
						(char const *) kv.key().data()
					);
					continue;
				}
				minlight.name = maybeTextureName.value();
				minlight.value = min;
				s_minlights.push_back(minlight);
			}

		} else if (classname_is(mapent, u8"info_texlights")) {
			Log("Reading texlights from info_texlights map entity\n");

			for (entity_key_value const & kv : mapent->keyValues) {
				if (kv.key() == u8"classname" || kv.key() == u8"origin") {
					continue; // we dont care about these keyvalues
				}

				values = sscanf(
					(char const *) kv.value().data(),
					"%f %f %f %f",
					&r,
					&g,
					&b,
					&i
				);

				if (values == 1) {
					g = b = r;
				} else if (values == 4) // use brightness value.
				{
					r *= i / 255.0;
					g *= i / 255.0;
					b *= i / 255.0;
				} else if (values != 3) {
					Warning(
						"Ignoring bad texlight '%s' in info_texlights entity",
						(char const *) kv.key().data()
					);
					continue;
				}
				std::optional<wad_texture_name> const maybeTextureName
					= wad_texture_name::make_if_legal_name(kv.key());
				if (!maybeTextureName) {
					Warning(
						"Ignoring bad texlight '%s' in info_texlights entity",
						(char const *) kv.key().data()
					);
					continue;
				}
				texlight_t texlight;
				texlight.name = maybeTextureName.value();
				texlight.value[0] = r;
				texlight.value[1] = g;
				texlight.value[2] = b;
				texlight.filename = "info_texlights";
				s_texlights.push_back(texlight);
			}
			foundTexlights = true;
		}
		if (foundMinlights && foundTexlights) {
			break;
		}
	}
}

char const * lights_rad = "lights.rad";
char const * ext_rad = ".rad";

// =====================================================================================
//  LoadRadFiles
// =====================================================================================
void LoadRadFiles(
	char const * const mapname, char const * const user_rad, char* argv0
) {
	char mapname_lights[_MAX_PATH];

	char mapfile[_MAX_PATH];

	// Get application directory. Try looking in the directory we were run
	// from
	std::filesystem::path appDir = get_path_to_directory_with_executable(
		&argv0
	);

	// Get map directory
	std::filesystem::path mapDir
		= std::filesystem::path(mapname).parent_path();
	ExtractFile(mapname, mapfile);

	// Look for lights.rad in mapdir
	std::filesystem::path globalLights = mapDir / lights_rad;
	if (std::filesystem::exists(globalLights)) {
		ReadLightFile(globalLights.c_str());
	} else {
		// Look for lights.rad in appdir
		globalLights = appDir / lights_rad;
		if (std::filesystem::exists(globalLights)) {
			ReadLightFile(globalLights.c_str());
		} else {
			// Look for lights.rad in current working directory
			globalLights = lights_rad;
			if (std::filesystem::exists(globalLights)) {
				ReadLightFile(globalLights.c_str());
			}
		}
	}

	// Look for mapname.rad in mapdir
	safe_strncpy(mapname_lights, mapDir.c_str(), _MAX_PATH);
	safe_strncat(mapname_lights, mapfile, _MAX_PATH);
	safe_strncat(mapname_lights, ext_rad, _MAX_PATH);
	if (std::filesystem::exists(mapname_lights)) {
		ReadLightFile(mapname_lights);
	}

	if (user_rad) {
		char user_lights[_MAX_PATH];
		char userfile[_MAX_PATH];

		ExtractFile(user_rad, userfile);

		// Look for user.rad from command line (raw)
		safe_strncpy(user_lights, user_rad, _MAX_PATH);
		if (std::filesystem::exists(user_lights)) {
			ReadLightFile(user_lights);
		} else {
			// Try again with .rad enforced as extension
			DefaultExtension(user_lights, ext_rad);
			if (std::filesystem::exists(user_lights)) {
				ReadLightFile(user_lights);
			} else {
				// Look for user.rad in mapdir
				safe_strncpy(user_lights, mapDir.c_str(), _MAX_PATH);
				safe_strncat(user_lights, userfile, _MAX_PATH);
				DefaultExtension(user_lights, ext_rad);
				if (std::filesystem::exists(user_lights)) {
					ReadLightFile(user_lights);
				} else {
					// Look for user.rad in appdir
					safe_strncpy(user_lights, appDir.c_str(), _MAX_PATH);
					safe_strncat(user_lights, userfile, _MAX_PATH);
					DefaultExtension(user_lights, ext_rad);
					if (std::filesystem::exists(user_lights)) {
						ReadLightFile(user_lights);
					} else {
						// Look for user.rad in current working directory
						safe_strncpy(user_lights, userfile, _MAX_PATH);
						DefaultExtension(user_lights, ext_rad);
						if (std::filesystem::exists(user_lights)) {
							ReadLightFile(user_lights);
						}
					}
				}
			}
		}
	}
	ReadInfoTexAndMinlights(); // AJM + seedee
}

// =====================================================================================
//  main
// =====================================================================================
int main(int const argc, char** argv) {
	g_opaque_face_list.reserve(1024
	); // Just for the performance improvement

	int i;
	char const * mapname_from_arg = nullptr;
	char const * user_lights = nullptr;
	char temp[_MAX_PATH]; // seedee

	g_Program = "HLRAD";

	int argcold = argc;
	char** argvold = argv;
	{
		int argc;
		char** argv;
		ParseParamFile(argcold, argvold, argc, argv);
		{
			if (argc == 1) {
				Usage();
			}

			for (i = 1; i < argc; i++) {
				if (!strcasecmp(argv[i], "-dump")) {
					g_dumppatches = true;
				} else if (!strcasecmp(argv[i], "-extra")) {
					g_extra = true;

					if (g_numbounce < 12) {
						g_numbounce = 12;
					}
				} else if (!strcasecmp(argv[i], "-bounce")) {
					if (i + 1 < argc) // added "1" .--vluzacn
					{
						g_numbounce = atoi(argv[++i]);

						if (g_numbounce > 1000) {
							Log("Unexpectedly large value (>1000) for '-bounce'\n"
							);
							Usage();
						}
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-dev"
						   )) {
					if (i + 1 < argc) // added "1" .--vluzacn
					{
						g_developer = (developer_level) atoi(argv[++i]);
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-verbose"
						   )) {
					g_verbose = true;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-noinfo"
						   )) {
					g_info = false;
				} else if (argv[i] == "-threads"sv) {
					if (i + 1 < argc) // added "1" .--vluzacn
					{
						g_numthreads = atoi(argv[++i]);

						if (std::cmp_greater(g_numthreads, MAX_THREADS)) {
							Log("Expected value below %zu for '-threads'\n",
								MAX_THREADS);
							Usage();
						}
					} else {
						Usage();
					}
				}
#ifdef SYSTEM_WIN32
				else if (strings_equal_with_ascii_case_insensitivity(
							 argv[i], u8"-estimate"
						 )) {
					g_estimate = true;
				}
#endif
#ifdef SYSTEM_POSIX
				else if (strings_equal_with_ascii_case_insensitivity(
							 argv[i], u8"-noestimate"
						 )) {
					g_estimate = false;
				}
#endif
				else if (strings_equal_with_ascii_case_insensitivity(
							 argv[i], u8"-fast"
						 )) {
					g_fastmode = true;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-nolerp"
						   )) {
					g_lerp_enabled = false;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-chop"
						   )) {
					if (i + 1 < argc) // added "1" .--vluzacn
					{
						g_chop = atof(argv[++i]);
						if (g_chop < 1) {
							Log("expected value greater than 1 for '-chop'\n"
							);
							Usage();
						}
						if (g_chop < 32) {
							Log("Warning: Chop values below 32 are not recommended."
							);
						}
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-texchop"
						   )) {
					if (i + 1 < argc) // added "1" .--vluzacn
					{
						g_texchop = atof(argv[++i]);
						if (g_texchop < 1) {
							Log("expected value greater than 1 for '-texchop'\n"
							);
							Usage();
						}
						if (g_texchop < 32) {
							Log("Warning: texchop values below 16 are not recommended."
							);
						}
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-notexscale"
						   )) {
					g_texscale = false;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-nosubdivide"
						   )) {
					if (i < argc) {
						g_subdivide = false;
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-scale"
						   )) {
					if (i + 1 < argc) // added "1" .--vluzacn
					{
						// ------------------------------------------------------------------------
						// Changes by Adam Foster -
						// afoster@compsoc.man.ac.uk Munge monochrome
						// lightscale into colour one
						i++;
						g_colour_lightscale[0] = (float) atof(argv[i]);
						g_colour_lightscale[1] = (float) atof(argv[i]);
						g_colour_lightscale[2] = (float) atof(argv[i]);
						// ------------------------------------------------------------------------
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-fade"
						   )) {
					if (i + 1 < argc) // added "1" .--vluzacn
					{
						g_fade = (float) atof(argv[++i]);
						if (g_fade < 0.0) {
							Log("-fade must be a positive number\n");
							Usage();
						}
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-ambient"
						   )) {
					if (i + 3 < argc) {
						g_ambient[0] = (float) atof(argv[++i]) * 128;
						g_ambient[1] = (float) atof(argv[++i]) * 128;
						g_ambient[2] = (float) atof(argv[++i]) * 128;
					} else {
						Error(
							"Expected three color values after '-ambient'\n"
						);
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-limiter"
						   )) {
					if (i + 1 < argc) //"1" was added to check if there is
									  // another argument afterwards
									  //(expected value) //seedee
					{
						g_limitthreshold = atof(argv[++i]);
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-drawoverload"
						   )) {
					g_drawoverload = true;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-lights"
						   )) {
					if (i + 1 < argc) // added "1" .--vluzacn
					{
						user_lights = argv[++i];
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-circus"
						   )) {
					g_circus = true;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-noskyfix"
						   )) {
					g_sky_lighting_fix = false;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-incremental"
						   )) {
					g_incremental = true;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-chart"
						   )) {
					g_chart = true;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-low"
						   )) {
					g_threadpriority = q_threadpriority::eThreadPriorityLow;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-high"
						   )) {
					g_threadpriority
						= q_threadpriority::eThreadPriorityHigh;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-nolog"
						   )) {
					g_log = false;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-gamma"
						   )) {
					if (i + 1 < argc) // added "1" .--vluzacn
					{
						// ------------------------------------------------------------------------
						// Changes by Adam Foster -
						// afoster@compsoc.man.ac.uk Munge values from
						// original, monochrome gamma into colour gamma
						i++;
						g_colour_qgamma[0] = (float) atof(argv[i]);
						g_colour_qgamma[1] = (float) atof(argv[i]);
						g_colour_qgamma[2] = (float) atof(argv[i]);
						// ------------------------------------------------------------------------
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-dlight"
						   )) {
					if (i + 1 < argc) // added "1" .--vluzacn
					{
						g_dlight_threshold = (float) atof(argv[++i]);
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-sky"
						   )) {
					if (i + 1 < argc) // added "1" .--vluzacn
					{
						g_indirect_sun = (float) atof(argv[++i]);
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-smooth"
						   )) {
					if (i + 1 < argc) // added "1" .--vluzacn
					{
						g_smoothing_value = atof(argv[++i]);
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-smooth2"
						   )) {
					if (i + 1 < argc) {
						g_smoothing_value_2 = atof(argv[++i]);
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-coring"
						   )) {
					if (i + 1 < argc) // added "1" .--vluzacn
					{
						g_coring = (float) atof(argv[++i]);
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-texdata"
						   )) {
					if (i + 1 < argc) // added "1" .--vluzacn
					{
						int x = atoi(argv[++i]) * 1024;

						// if (x > g_max_map_miptex) //--vluzacn
						{ g_max_map_miptex = x; }
					} else {
						Usage();
					}
				} else if (!strcasecmp(argv[i], "-vismatrix")) {
					if (i + 1 < argc) {
						char const * value = argv[++i];
						if (!strcasecmp(value, "normal")) {
							g_method = vis_method::vismatrix;
						} else if (!strcasecmp(value, "sparse")) {
							g_method = vis_method::sparse_vismatrix;
						} else if (!strcasecmp(value, "off")) {
							g_method = vis_method::no_vismatrix;
						} else {
							Error("Unknown vismatrix type: '%s'", value);
						}
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-nospread"
						   )) {
					g_allow_spread = false;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-nopaque"
						   )
						   || strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-noopaque"
						   )) //--vluzacn
				{
					g_allow_opaques = false;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-dscale"
						   )) {
					if (i + 1 < argc) // added "1" .--vluzacn
					{
						g_direct_scale = (float) atof(argv[++i]);
					} else {
						Usage();
					}
				}

				// ------------------------------------------------------------------------
				// Changes by Adam Foster - afoster@compsoc.man.ac.uk
				else if (strings_equal_with_ascii_case_insensitivity(
							 argv[i], u8"-colourgamma"
						 )) {
					if (i + 3 < argc) {
						g_colour_qgamma[0] = (float) atof(argv[++i]);
						g_colour_qgamma[1] = (float) atof(argv[++i]);
						g_colour_qgamma[2] = (float) atof(argv[++i]);
					} else {
						Error(
							"expected three color values after '-colourgamma'\n"
						);
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-colourscale"
						   )) {
					if (i + 3 < argc) {
						g_colour_lightscale[0] = (float) atof(argv[++i]);
						g_colour_lightscale[1] = (float) atof(argv[++i]);
						g_colour_lightscale[2] = (float) atof(argv[++i]);
					} else {
						Error(
							"expected three color values after '-colourscale'\n"
						);
					}
				}

				else if (strings_equal_with_ascii_case_insensitivity(
							 argv[i], u8"-colourjitter"
						 )) {
					if (i + 3 < argc) {
						g_colour_jitter_hack[0] = (float) atof(argv[++i]);
						g_colour_jitter_hack[1] = (float) atof(argv[++i]);
						g_colour_jitter_hack[2] = (float) atof(argv[++i]);
					} else {
						Error(
							"expected three color values after '-colourjitter'\n"
						);
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-jitter"
						   )) {
					if (i + 3 < argc) {
						g_jitter_hack[0] = (float) atof(argv[++i]);
						g_jitter_hack[1] = (float) atof(argv[++i]);
						g_jitter_hack[2] = (float) atof(argv[++i]);
					} else {
						Error(
							"expected three color values after '-jitter'\n"
						);
					}
				}

				// ------------------------------------------------------------------------

				else if (strings_equal_with_ascii_case_insensitivity(
							 argv[i], u8"-customshadowwithbounce"
						 )) {
					g_customshadow_with_bouncelight = true;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-rgbtransfers"
						   )) {
					g_rgb_transfers = true;
				}

				else if (strings_equal_with_ascii_case_insensitivity(
							 argv[i], u8"-bscale"
						 )) {
					Error("'-bscale' is obsolete.");
					if (i + 1 < argc) {
						g_transtotal_hack = (float) atof(argv[++i]);
					} else {
						Usage();
					}
				}

				else if (strings_equal_with_ascii_case_insensitivity(
							 argv[i], u8"-minlight"
						 )) {
					if (i + 1 < argc) {
						int v = atoi(argv[++i]);
						v = std::max(0, std::min(v, 255));
						g_minlight = (unsigned char) v;
					} else {
						Usage();
					}
				}

				else if (strings_equal_with_ascii_case_insensitivity(
							 argv[i], u8"-softsky"
						 )) {
					if (i + 1 < argc) {
						g_softsky = (bool) atoi(argv[++i]);
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-nostudioshadow"
						   )) {
					g_studioshadow = false;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-drawpatch"
						   )) {
					g_drawpatch = true;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-drawsample"
						   )) {
					g_drawsample = true;
					if (i + 4 < argc) {
						g_drawsample_origin[0] = atof(argv[++i]);
						g_drawsample_origin[1] = atof(argv[++i]);
						g_drawsample_origin[2] = atof(argv[++i]);
						g_drawsample_radius = atof(argv[++i]);
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-drawedge"
						   )) {
					g_drawedge = true;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-drawlerp"
						   )) {
					g_drawlerp = true;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-drawnudge"
						   )) {
					g_drawnudge = true;
				}

				else if (strings_equal_with_ascii_case_insensitivity(
							 argv[i], u8"-compress"
						 )) {
					if (i + 1 < argc) {
						int value = atoi(argv[++i]);
						if (!is_valid_float_type(value)) {
							Usage();
						}
						g_transfer_compress_type = (float_type) value;
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-rgbcompress"
						   )) {
					if (i + 1 < argc) {
						int value = atoi(argv[++i]);
						if (!is_valid_vector_type(value)) {
							Usage();
						}
						g_rgbtransfer_compress_type = (vector_type) value;
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-depth"
						   )) {
					if (i + 1 < argc) {
						g_translucentdepth = atof(argv[++i]);
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-blockopaque"
						   )) {
					if (i + 1 < argc) {
						g_blockopaque = atoi(argv[++i]);
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-waddir"
						   )) {
					if (i + 1 < argc) {
						AddWadFolder(std::filesystem::path(
							argv[++i], std::filesystem::path::auto_format
						));
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-notextures"
						   )) {
					g_notextures = true;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-texreflectgamma"
						   )) {
					if (i + 1 < argc) {
						g_texreflectgamma = atof(argv[++i]);
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-texreflectscale"
						   )) {
					if (i + 1 < argc) {
						g_texreflectscale = atof(argv[++i]);
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-blur"
						   )) {
					if (i + 1 < argc) {
						g_blur = atof(argv[++i]);
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-noemitterrange"
						   )) {
					g_noemitterrange = true;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-nobleedfix"
						   )) {
					g_bleedfix = false;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-texlightgap"
						   )) {
					if (i + 1 < argc) {
						g_texlightgap = atof(argv[++i]);
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-pre25"
						   )) // Pre25 should be after everything else to
							  // override
				{
					g_pre25update = true;
					g_limitthreshold = 188.0;
				} else if (argv[i][0] == '-') {
					Log("Unknown option \"%s\"\n", argv[i]);
					Usage();
				} else if (!mapname_from_arg) {
					mapname_from_arg = argv[i];
				} else {
					Log("Unknown option \"%s\"\n", argv[i]);
					Usage();
				}
			}

			if (!mapname_from_arg) {
				Log("No mapname specified\n");
				Usage();
			}

			g_smoothing_threshold = (float
			) cos(g_smoothing_value * (std::numbers::pi_v<double> / 180.0));

			safe_strncpy(g_Mapname, mapname_from_arg, _MAX_PATH);
			FlipSlashes(g_Mapname);
			ExtractFilePath(g_Mapname, temp); // skip mapname
			ExtractFilePath(temp, g_Wadpath);
			StripExtension(g_Mapname);
			OpenLog(g_clientid);
			atexit(CloseLog);
			ThreadSetDefault();
			ThreadSetPriority(g_threadpriority);
			LogStart(argcold, argvold);
			log_arguments(argc, argv);

			CheckForErrorLog();

			compress_compatability_test();

			hlassume(CalcFaceExtents_test(), assume_first);
			dtexdata_init();
			atexit(dtexdata_free);
			// END INIT

			// BEGIN RAD
			double const start = I_FloatTime();

			// normalise maxlight

			safe_snprintf(g_source, _MAX_PATH, "%s.bsp", g_Mapname);
			LoadBSPFile(g_source);
			ParseEntities();
			if (g_fastmode) {
				g_numbounce = 0;
				g_softsky = false;
			}
			Settings();
			DeleteEmbeddedLightmaps();
			LoadTextures();
			LoadRadFiles(g_Mapname, user_lights, argv[0]);
			ReadCustomChopValue();
			ReadCustomSmoothValue();
			ReadTranslucentTextures();
			ReadLightingCone();
			g_smoothing_threshold_2 = g_smoothing_value_2 < 0
				? g_smoothing_threshold
				: (float) cos(
					  g_smoothing_value_2
					  * (std::numbers::pi_v<double> / 180.0)
				  );
			{
				g_corings[0] = 0;
				std::fill(&g_corings[1], &g_corings[ALLSTYLES], g_coring);
			}
			if (g_direct_scale != 1.0) {
				Warning(
					"dscale value should be 1.0 for final compile.\nIf you need to adjust the bounced light, use the '-texreflectscale' and '-texreflectgamma' options instead."
				);
			}
			if (g_colour_lightscale[0] != 2.0
				|| g_colour_lightscale[1] != 2.0
				|| g_colour_lightscale[2] != 2.0) {
				Warning(
					"light scale value should be 2.0 for final compile.\nValues other than 2.0 will result in incorrect interpretation of light_environment's brightness when the engine loads the map."
				);
			}
			if (g_drawlerp) {
				g_direct_scale = 0.0;
			}

			if (!g_visdatasize) {
				Warning("No VIS information.");
			}
			if (g_blur < 1.0) {
				g_blur = 1.0;
			}
			RadWorld();
			FreeStudioModels(); // seedee
			FreeOpaqueFaceList();
			FreePatches();
			DeleteOpaqueNodes();

			EmbedLightmapInTextures();
			if (g_chart) {
				print_bsp_file_sizes(bspGlobals);
			}

			WriteBSPFile(g_source);

			double const end = I_FloatTime();
			LogTimeElapsed(end - start);
			// END RAD
		}
	}
	return 0;
}
