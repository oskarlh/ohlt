#include "hlbsp.h"

#include "cli_option_defaults.h"
#include "cmdlinecfg.h"
#include "filelib.h"
#include "hull_size.h"
#include "log.h"
#include "time_counter.h"
#include "utf8.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>
#include <utility>

using namespace std::literals;

hull_sizes g_hull_size{ standard_hull_sizes };

static FILE* polyfiles[NUM_HULLS];
static FILE* brushfiles[NUM_HULLS];
hull_count g_hullnum = 0;

///// TODO: Make it a vector or vector_inplace
static face_t* validfaces[MAX_INTERNAL_MAP_PLANES];

std::filesystem::path g_bspfilename;
std::filesystem::path g_pointfilename;
std::filesystem::path g_linefilename;
std::filesystem::path g_portfilename;
std::filesystem::path g_extentfilename;

// command line flags
bool g_noopt = DEFAULT_NOOPT; // don't optimize BSP on write
bool g_noclipnodemerge = DEFAULT_NOCLIPNODEMERGE;
bool g_nofill = DEFAULT_NOFILL; // dont fill "-nofill"
bool g_noinsidefill = DEFAULT_NOINSIDEFILL;
bool g_notjunc = DEFAULT_NOTJUNC;
bool g_nobrink = DEFAULT_NOBRINK;
bool g_noclip = DEFAULT_NOCLIP;			   // no clipping hull "-noclip"
bool g_chart = cli_option_defaults::chart; // print out chart? "-chart"
bool g_estimate
	= cli_option_defaults::estimate; // estimate mode "-estimate"
bool g_info = cli_option_defaults::info;
bool g_bLeakOnly = DEFAULT_LEAKONLY; // leakonly mode "-leakonly"
bool g_bLeaked = false;
int g_subdivide_size = DEFAULT_SUBDIVIDE_SIZE;

bool g_bUseNullTex = cli_option_defaults::nulltex; // "-nonulltex"

bool g_nohull2 = false;

bool g_viewportal = false;

vector_inplace<mapplane_t, MAX_INTERNAL_MAP_PLANES> g_mapPlanes;

// =====================================================================================
//  GetParamsFromEnt
//      this function is called from parseentity when it encounters the
//      info_compile_parameters entity. each tool should have its own
//      version of this to handle its own specific settings.
// =====================================================================================
void GetParamsFromEnt(entity_t* mapent) {
	int iTmp;

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

	/*
	hlbsp(choices) : "HLBSP" : 0 =
	[
	   0 : "Off"
	   1 : "Normal"
	   2 : "Leakonly"
	]
	*/
	iTmp = IntForKey(mapent, u8"hlbsp");
	if (iTmp == 0) {
		Fatal(
			assume_TOOL_CANCEL,
			"%s was set to \"Off\" (0) in info_compile_parameters entity, execution cancelled",
			(char const *) g_Program.data()
		);
		CheckFatal();
	} else if (iTmp == 1) {
		g_bLeakOnly = false;
	} else if (iTmp == 2) {
		g_bLeakOnly = true;
	}
	Log("%30s [ %-9s ]\n", "Leakonly Mode", g_bLeakOnly ? "on" : "off");

	iTmp = IntForKey(mapent, u8"noopt");
	if (iTmp == 0) {
		g_noopt = false;
	} else {
		g_noopt = true;
	}

	/*
	nocliphull(choices) : "Generate clipping hulls" : 0 =
	[
		0 : "Yes"
		1 : "No"
	]
	*/
	iTmp = IntForKey(mapent, u8"nocliphull");
	if (iTmp == 0) {
		g_noclip = false;
	} else if (iTmp == 1) {
		g_noclip = true;
	}
	Log("%30s [ %-9s ]\n",
		"Clipping Hull Generation",
		g_noclip ? "off" : "on");

	//////////////////
	Verbose("\n");
}

// =====================================================================================
//  NewFaceFromFace
//      Duplicates the non point information of a face, used by SplitFace
//      and MergeFace.
// =====================================================================================
face_t NewFaceFromFace(face_t const & in) {
	face_t newf{};

	newf.planenum = in.planenum;
	newf.texturenum = in.texturenum;
	newf.original = in.original;
	newf.contents = in.contents;
	newf.facestyle = in.facestyle;
	newf.detailLevel = in.detailLevel;

	return newf;
}

// =====================================================================================
//  SplitFaceTmp
//      blah
// =====================================================================================
static void SplitFaceTmp(
	face_t* in,
	mapplane_t const * const split,
	face_t** front,
	face_t** back
) {
	double dists[MAXEDGES + 1];
	face_side sides[MAXEDGES + 1];
	int counts[3];
	double dot;
	int i;
	int j;
	face_t* newf;
	face_t* new2;

	if (in->freed) {
		Error("SplitFace: freed face");
	}
	counts[0] = counts[1] = counts[2] = 0;

	double dotSum = 0.0;
	// This again... We have code like this in accurate_winding repeated
	// several times determine sides for each point
	for (i = 0; i < in->pts.size(); i++) {
		dot = dot_product(in->pts[i], split->normal);
		dot -= split->dist;
		dotSum += dot;
		dists[i] = dot;
		if (dot > ON_EPSILON) {
			sides[i] = face_side::front;
		} else if (dot < -ON_EPSILON) {
			sides[i] = face_side::back;
		} else {
			sides[i] = face_side::on;
		}
		counts[std::size_t(sides[i])]++;
	}
	sides[i] = sides[0];
	dists[i] = dists[0];

	if (!counts[0] && !counts[1]) {
		if (in->detailLevel) {
			// put front face in front node, and back face in back node.
			mapplane_t const & faceplane = g_mapPlanes[in->planenum];
			if (dot_product(faceplane.normal, split->normal)
				> NORMAL_EPSILON) // usually near 1.0 or -1.0
			{
				*front = in;
				*back = nullptr;
			} else {
				*front = nullptr;
				*back = in;
			}
		} else {
			// not func_detail. front face and back face need to pair.
			if (dotSum > NORMAL_EPSILON) {
				*front = in;
				*back = nullptr;
			} else {
				*front = nullptr;
				*back = in;
			}
		}
		return;
	}
	if (!counts[0]) {
		*front = nullptr;
		*back = in;
		return;
	}
	if (!counts[1]) {
		*front = in;
		*back = nullptr;
		return;
	}

	*back = newf = new face_t{ NewFaceFromFace(*in) };
	*front = new2 = new face_t{ NewFaceFromFace(*in) };

	// distribute the points and generate splits

	for (i = 0; i < in->pts.size(); i++) {
		double3_array const & p1{ in->pts[i] };

		if (sides[i] == face_side::on) {
			newf->pts.emplace_back(p1);
			new2->pts.emplace_back(p1);
			continue;
		}

		if (sides[i] == face_side::front) {
			new2->pts.emplace_back(p1);
		} else {
			newf->pts.emplace_back(p1);
		}

		if (sides[i + 1] == face_side::on || sides[i + 1] == sides[i]) {
			continue;
		}

		// generate a split point
		double3_array const & p2{ in->pts[(i + 1) % in->pts.size()] };

		double3_array mid;
		dot = dists[i] / (dists[i] - dists[i + 1]);
		for (j = 0; j < 3; j++) { // avoid round off error when possible
			if (split->normal[j] == 1) {
				mid[j] = split->dist;
			} else if (split->normal[j] == -1) {
				mid[j] = -split->dist;
			} else {
				mid[j] = p1[j] + dot * (p2[j] - p1[j]);
			}
		}

		newf->pts.emplace_back(mid);
		new2->pts.emplace_back(mid);
	}

	{
		accurate_winding wd{ std::span{ newf->pts } };
		wd.RemoveColinearPoints();
		newf->pts.assign_range(wd.points());
		if (newf->pts.empty()) {
			free(*back);
			*back = nullptr;
		}
	}

	{
		accurate_winding wd{ std::span{ new2->pts } };
		wd.RemoveColinearPoints();
		new2->pts.assign_range(wd.points());
		if (new2->pts.empty()) {
			free(*front);
			*front = nullptr;
		}
	}
}

// =====================================================================================
//  SplitFace
//      blah
// =====================================================================================
void SplitFace(
	face_t* in,
	mapplane_t const * const split,
	face_t** front,
	face_t** back
) {
	SplitFaceTmp(in, split, front, back);

	// free the original face now that is is represented by the fragments
	if (*front && *back) {
		delete in;
	}
}

// =====================================================================================
//  AllocFace
// =====================================================================================
face_t* AllocFace() {
	face_t* f;

	f = (face_t*) calloc(1, sizeof(face_t));
	*f = {};

	return f;
}

// =====================================================================================
//  AllocPortal
// =====================================================================================
portal_t* AllocPortal() {
	portal_t* p;

	p = (portal_t*) malloc(sizeof(portal_t));
	*p = {};

	return p;
}

// =====================================================================================
//  FreePortal
// =====================================================================================
void FreePortal(portal_t* p) // consider: inline
{
	free(p);
}

side_t* NewSideFromSide(side_t const * s) {
	side_t* news;
	news = new side_t{};
	news->plane = s->plane;
	news->wind = accurate_winding(s->wind);
	return news;
}

brush_t* AllocBrush() {
	brush_t* b;
	b = (brush_t*) malloc(sizeof(brush_t));
	*b = {};
	return b;
}

void FreeBrush(brush_t* b) {
	if (b->sides) {
		side_t *s, *next;
		for (s = b->sides; s; s = next) {
			next = s->next;
			delete s;
		}
	}
	free(b);
	return;
}

brush_t* NewBrushFromBrush(brush_t const * b) {
	brush_t* newb;
	newb = AllocBrush();
	side_t *s, **pnews;
	for (s = b->sides, pnews = &newb->sides; s;
		 s = s->next, pnews = &(*pnews)->next) {
		*pnews = NewSideFromSide(s);
	}
	return newb;
}

void ClipBrush(brush_t** b, mapplane_t const * split, double epsilon) {
	side_t *s{}, **pnext{};
	for (pnext = &(*b)->sides, s = *pnext; s; s = *pnext) {
		if (s->wind.mutating_clip(
				split->normal, split->dist, false, epsilon
			)) {
			pnext = &s->next;
		} else {
			*pnext = s->next;
			delete s;
		}
	}
	if (!(*b)->sides) { // empty brush
		FreeBrush(*b);
		*b = nullptr;
		return;
	}
	accurate_winding wind{ *split };
	for (s = (*b)->sides; s; s = s->next) {
		if (!wind.mutating_clip(
				s->plane.normal, s->plane.dist, false, epsilon
			)) {
			break;
		}
	}
	if (!wind.empty()) {
		s = new side_t{};
		s->plane = *split;
		s->wind = std::move(wind);
		s->next = (*b)->sides;
		(*b)->sides = s;
	}
	return;
}

void SplitBrush(
	brush_t* in, mapplane_t const * split, brush_t** front, brush_t** back
)
// 'in' will be freed
{
	in->next = nullptr;
	bool onfront;
	bool onback;
	onfront = false;
	onback = false;
	side_t* s;
	for (s = in->sides; s; s = s->next) {
		switch (s->wind.WindingOnPlaneSide(
			split->normal, split->dist, 2 * ON_EPSILON
		)) {
			case face_side::cross:
				onfront = true;
				onback = true;
				break;
			case face_side::front:
				onfront = true;
				break;
			case face_side::back:
				onback = true;
				break;
			case face_side::on:
				break;
		}
		if (onfront && onback) {
			break;
		}
	}
	if (!onfront && !onback) {
		FreeBrush(in);
		*front = nullptr;
		*back = nullptr;
		return;
	}
	if (!onfront) {
		*front = nullptr;
		*back = in;
		return;
	}
	if (!onback) {
		*front = in;
		*back = nullptr;
		return;
	}
	*front = in;
	*back = NewBrushFromBrush(in);
	mapplane_t frontclip = *split;
	mapplane_t backclip = *split;
	backclip.normal = negate_vector(backclip.normal);
	backclip.dist = -backclip.dist;
	ClipBrush(front, &frontclip, NORMAL_EPSILON);
	ClipBrush(back, &backclip, NORMAL_EPSILON);
	return;
}

brush_t*
BrushFromBox(double3_array const & mins, double3_array const & maxs) {
	brush_t* b = AllocBrush();
	mapplane_t planes[6];
	for (int k = 0; k < 3; k++) {
		planes[k].normal.fill(0.0);
		planes[k].normal[k] = 1.0;
		planes[k].dist = mins[k];
		planes[k + 3].normal.fill(0.0);
		planes[k + 3].normal[k] = -1.0;
		planes[k + 3].dist = -maxs[k];
	}
	b->sides = new side_t{};
	b->sides->plane = planes[0];
	b->sides->wind = accurate_winding(planes[0]);
	for (int k = 1; k < 6; k++) {
		ClipBrush(&b, &planes[k], NORMAL_EPSILON);
		if (b == nullptr) {
			break;
		}
	}
	return b;
}

void CalcBrushBounds(
	brush_t const * b, double3_array& mins, double3_array& maxs
) {
	mins.fill(hlbsp_bogus_range);
	maxs.fill(-hlbsp_bogus_range);
	for (side_t* s = b->sides; s; s = s->next) {
		bounding_box const bounds = s->wind.getBounds();
		mins = vector_minimums(mins, bounds.mins);
		maxs = vector_maximums(maxs, bounds.maxs);
	}
}

// =====================================================================================
//  AllocNode
//      blah
// =====================================================================================
node_t* AllocNode() {
	node_t* n;

	n = (node_t*) malloc(sizeof(node_t));
	*n = {};

	return n;
}

// =====================================================================================
//  AddPointToBounds
// =====================================================================================
static void AddPointToBounds(
	double3_array const & v, double3_array& mins, double3_array& maxs
) {
	int i;
	double val;

	for (i = 0; i < 3; i++) {
		val = v[i];
		if (val < mins[i]) {
			mins[i] = val;
		}
		if (val > maxs[i]) {
			maxs[i] = val;
		}
	}
}

// =====================================================================================
//  AddFaceToBounds
// =====================================================================================
static void AddFaceToBounds(
	face_t const * const f, double3_array& mins, double3_array& maxs
) {
	for (int i = 0; i < f->pts.size(); i++) {
		AddPointToBounds(f->pts[i], mins, maxs);
	}
}

// =====================================================================================
//  ClearBounds
// =====================================================================================
static void ClearBounds(double3_array& mins, double3_array& maxs) {
	mins[0] = mins[1] = mins[2] = 99999;
	maxs[0] = maxs[1] = maxs[2] = -99999;
}

// =====================================================================================
//  SurflistFromValidFaces
//      blah
// =====================================================================================
static surfchain_t* SurflistFromValidFaces() {
	surface_t* n;
	int i;
	face_t* f;
	face_t* next;
	surfchain_t* sc;

	sc = (surfchain_t*) malloc(sizeof(*sc));
	ClearBounds(sc->mins, sc->maxs);
	sc->surfaces = nullptr;

	// grab planes from both sides
	for (i = 0; i < g_numplanes; i += 2) {
		if (!validfaces[i] && !validfaces[i + 1]) {
			continue;
		}
		n = new surface_t{};
		n->next = sc->surfaces;
		sc->surfaces = n;
		ClearBounds(n->mins, n->maxs);
		// The surface's detailLevel is the minimum of its faces
		n->detailLevel = std::numeric_limits<detail_level>::max();
		n->planenum = i;

		n->faces = nullptr;
		for (f = validfaces[i]; f; f = next) {
			next = f->next;
			f->next = n->faces;
			n->faces = f;
			AddFaceToBounds(f, n->mins, n->maxs);
			n->detailLevel = std::min(n->detailLevel, f->detailLevel);
		}
		for (f = validfaces[i + 1]; f; f = next) {
			next = f->next;
			f->next = n->faces;
			n->faces = f;
			AddFaceToBounds(f, n->mins, n->maxs);
			n->detailLevel = std::min(n->detailLevel, f->detailLevel);
		}

		AddPointToBounds(n->mins, sc->mins, sc->maxs);
		AddPointToBounds(n->maxs, sc->mins, sc->maxs);

		validfaces[i] = nullptr;
		validfaces[i + 1] = nullptr;
	}

	// merge all possible polygons

	MergeAll(sc->surfaces);

	return sc;
}

// Returns true if the passed face should have fasestyle null
bool should_face_have_facestyle_null(
	wad_texture_name textureName, contents_t faceContents
) noexcept {
	if (faceContents == contents_t::SKY) {
		// An old comment said this is "for env_rain"...
		// might this be for CZDS, where
		// "any exterior faces of skybox brushes must be textured
		// with SKYCULL for env_rain and env_snow to work properly"??
		if (!textureName.is_ordinary_sky()) {
			return true;
		}
	}
	if (g_bUseNullTex) {
		// NULL faces are only of facetype face_null if we are using NULL
		// texture stripping
		return g_bUseNullTex && textureName.is_ordinary_null();
	}
	// Otherwise, under normal cases, NULL-textured faces should have
	// facestyle face_normal
	return false;
}

static facestyle_e set_face_style(face_t& f) {
	wad_texture_name const textureName{ get_texture_by_number(f.texturenum
	) };

	facestyle_e style = face_normal;
	if (textureName.is_ordinary_hint()) {
		style = face_hint;
	} else if (textureName.is_skip()) {
		style = face_skip;
	} else if (should_face_have_facestyle_null(textureName, f.contents)) {
		style = face_null;
	} else if (textureName.is_bevel_hint() || textureName.is_solid_hint()) {
		style = face_discardable;
	} else if (textureName.is_env_sky()) {
		style = face_null;
	}
	f.facestyle = style;
	return style;
}

// =====================================================================================
//  read_surfaces
// =====================================================================================
static surfchain_t* read_surfaces(FILE* file) {
	detail_level detailLevel;
	int planenum, numpoints;
	texinfo_count g_texinfo;
	std::underlying_type_t<contents_t> contents;
	double3_array v;
	int line = 0;
	double inaccuracy, inaccuracy_count = 0.0, inaccuracy_total = 0.0,
					   inaccuracy_max = 0.0;

	// read in the polygons
	while (1) {
		if (file == polyfiles[2] && g_nohull2) {
			break;
		}
		line++;
		int r = fscanf(
			file,
			"%hu %i %hu %i %i\n",
			&detailLevel,
			&planenum,
			&g_texinfo,
			&contents,
			&numpoints
		);
		if (r == 0 || r == -1) {
			return nullptr;
		}
		if (planenum == -1) // end of model
		{
			Developer(
				developer_level::megaspam,
				"inaccuracy: average %.8f max %.8f\n",
				inaccuracy_total / inaccuracy_count,
				inaccuracy_max
			);
			break;
		}
		if (r != 5) {
			Error("read_surfaces (line %i): scanf failure", line);
		}
		if (numpoints > MAXPOINTS) {
			Error(
				"read_surfaces (line %i): %i > MAXPOINTS\nThis is caused by a face with too many verticies (typically found on end-caps of high-poly cylinders)\n",
				line,
				numpoints
			);
		}
		if (planenum > g_numplanes) {
			Error(
				"read_surfaces (line %i): %i > g_numplanes\n",
				line,
				planenum
			);
		}
		if (g_texinfo != no_texinfo && g_texinfo > g_numtexinfo) {
			Error(
				"read_surfaces (line %i): %i > g_numtexinfo",
				line,
				g_texinfo
			);
		}

		if ((get_texture_by_number(g_texinfo)).is_skip()) {
			Verbose("read_surfaces (line %i): skipping a surface", line);

			for (int i = 0; i < numpoints; i++) {
				line++;
				// Verbose("skipping line %d", line);
				r = fscanf(file, "%lf %lf %lf\n", &v[0], &v[1], &v[2]);
				if (r != 3) {
					Error(
						"::read_surfaces (face_skip), fscanf of points failed at line %i",
						line
					);
				}
			}
			fscanf(file, "\n");
			continue;
		}

		face_t* f = new face_t{};
		f->detailLevel = detailLevel;
		f->planenum = planenum;
		f->texturenum = g_texinfo;
		f->contents = contents_t{ contents };
		f->next = validfaces[planenum];
		validfaces[planenum] = f;

		set_face_style(*f);

		for (int i = 0; i < numpoints; i++) {
			line++;
			r = fscanf(file, "%lf %lf %lf\n", &v[0], &v[1], &v[2]);
			if (r != 3) {
				Error(
					"::read_surfaces (face_normal), fscanf of points failed at line %i",
					line
				);
			}
			f->pts.emplace_back(v);
			if (developer_level::megaspam <= g_developer) {
				mapplane_t const & plane = g_mapPlanes[f->planenum];
				inaccuracy = fabs(
					dot_product(f->pts[i], plane.normal) - plane.dist
				);
				inaccuracy_count++;
				inaccuracy_total += inaccuracy;
				inaccuracy_max = std::max(inaccuracy, inaccuracy_max);
			}
		}
		fscanf(file, "\n");
	}

	return SurflistFromValidFaces();
}

static brush_t* ReadBrushes(FILE* file) {
	brush_t* brushes = nullptr;
	while (1) {
		if (file == brushfiles[2] && g_nohull2) {
			break;
		}
		int r;
		int brushinfo;
		r = fscanf(file, "%i\n", &brushinfo);
		if (r == 0 || r == -1) {
			if (brushes == nullptr) {
				Error("ReadBrushes: no more models");
			} else {
				Error("ReadBrushes: file end");
			}
		}
		if (brushinfo == -1) {
			break;
		}
		brush_t* b;
		b = AllocBrush();
		b->next = brushes;
		brushes = b;
		side_t** psn;
		psn = &b->sides;
		while (1) {
			int planenum;
			int numpoints;
			r = fscanf(file, "%i %u\n", &planenum, &numpoints);
			if (r != 2) {
				Error("ReadBrushes: get side failed");
			}
			if (planenum == -1) {
				break;
			}
			side_t* s = new side_t{};
			s->plane = g_mapPlanes[planenum ^ 1];
			s->wind = accurate_winding{};
			s->wind.reserve_point_storage(numpoints);
			for (int x = 0; x < numpoints; x++) {
				double3_array v;
				r = fscanf(file, "%lf %lf %lf\n", &v[0], &v[1], &v[2]);
				if (r != 3) {
					Error("ReadBrushes: get point failed");
				}
				s->wind.push_point(v);
			}
			s->wind.reverse_points(
			); // TODO: Why? Can this be removed if we also replace
			   // `planenum ^ 1` with `planenum`?
			s->next = nullptr;
			*psn = s;
			psn = &s->next;
		}
	}
	return brushes;
}

// =====================================================================================
//  ProcessModel
// =====================================================================================
static bool ProcessModel(bsp_data& bspData) {
	brush_t* detailbrushes;
	node_t* nodes;
	dmodel_t* model;
	int startleafs;

	surfchain_t* surfs = read_surfaces(polyfiles[0]);

	if (!surfs) {
		return false; // all models are done
	}
	detailbrushes = ReadBrushes(brushfiles[0]);

	hlassume(
		bspData.mapModelsLength < MAX_MAP_MODELS, assume_MAX_MAP_MODELS
	);

	startleafs = g_numleafs;
	int modnum = bspData.mapModelsLength;
	model = &bspData.mapModels[modnum];
	g_nummodels++;

	//    Log("ProcessModel: %i (%i f)\n", modnum, model->numfaces);

	g_hullnum = 0;
	model->mins.fill(99999);
	model->maxs.fill(-99999);
	{
		if (surfs->mins[0] > surfs->maxs[0]) {
			Developer(
				developer_level::fluff,
				"model %d hull %d empty\n",
				modnum,
				g_hullnum
			);
		} else {
			double3_array mins = vector_subtract(
				surfs->mins, g_hull_size[g_hullnum][0]
			);
			double3_array maxs = vector_subtract(
				surfs->maxs, g_hull_size[g_hullnum][1]
			);
			for (std::size_t i = 0; i < 3; ++i) {
				if (mins[i] > maxs[i]) {
					double tmp;
					tmp = (mins[i] + maxs[i]) / 2;
					mins[i] = tmp;
					maxs[i] = tmp;
				}
			}
			for (std::size_t i = 0; i < 3; ++i) {
				model->maxs[i] = std::max(model->maxs[i], (float) maxs[i]);
				model->mins[i] = std::min(model->mins[i], (float) mins[i]);
			}
		}
	}

	// SolidBSP generates a node tree
	nodes = SolidBSP(surfs, detailbrushes, modnum == 0);

	// build all the portals in the bsp tree
	// some portals are solid polygons, and some are paths to other leafs
	if (bspData.mapModelsLength == 1
		&& !g_nofill) // assume non-world bmodels are simple
	{
		if (!g_noinsidefill) {
			FillInside(nodes);
		}
		nodes = FillOutside(
			nodes, (g_bLeaked != true), 0
		); // make a leakfile if bad
	}

	FreePortals(nodes);

	// fix tjunctions
	tjunc(nodes);

	MakeFaceEdges();

	// emit the faces for the bsp file
	model->headnode[0] = g_numnodes;
	model->firstface = g_numfaces;
	bool novisiblebrushes = false;
	// model->headnode[0]<0 will crash HL, so must split it.
	if (nodes->is_leaf_node()) {
		novisiblebrushes = true;
		if (nodes->markfaces[0] != nullptr) {
			hlassume(false, assume_EmptySolid);
		}
		if (g_numplanes == 0) {
			Error("No valid planes.\n");
		}
		nodes->planenum = 0; // arbitrary plane
		nodes->children[0] = AllocNode();
		nodes->children[0]->planenum = -1;
		nodes->children[0]->contents = contents_t::EMPTY;
		nodes->children[0]->isdetail = false;
		nodes->children[0]->isportalleaf = true;
		nodes->children[0]->iscontentsdetail = false;
		nodes->children[0]->faces = nullptr;
		nodes->children[0]->markfaces = (face_t**) calloc(
			1, sizeof(face_t*)
		);
		nodes->children[0]->mins = {};
		nodes->children[0]->maxs = {};
		nodes->children[1] = AllocNode();
		nodes->children[1]->planenum = -1;
		nodes->children[1]->contents = contents_t::EMPTY;
		nodes->children[1]->isdetail = false;
		nodes->children[1]->isportalleaf = true;
		nodes->children[1]->iscontentsdetail = false;
		nodes->children[1]->faces = nullptr;
		nodes->children[1]->markfaces = (face_t**) calloc(
			1, sizeof(face_t*)
		);
		nodes->children[1]->mins = {};
		nodes->children[1]->maxs = {};
		nodes->contents = contents_t::DECISION_NODE;
		nodes->isdetail = false;
		nodes->isportalleaf = false;
		nodes->faces = nullptr;
		nodes->markfaces = nullptr;
		nodes->mins = {};
		nodes->maxs = {};
	}
	WriteDrawNodes(nodes);
	model->numfaces = g_numfaces - model->firstface;
	model->visleafs = g_numleafs - startleafs;

	bool skipClip = false;
	if (g_noclip) {
		// Store empty content type in headnode pointers to
		//  signify lack of clipping information in a way that doesn't crash
		// the game engine at runtime
		model->headnode[1] = std::to_underlying(contents_t::EMPTY);
		model->headnode[2] = std::to_underlying(contents_t::EMPTY);
		model->headnode[3] = std::to_underlying(contents_t::EMPTY);
		skipClip = true;
	}

	if (!skipClip) {
		// the clipping hulls are simpler
		for (g_hullnum = 1; g_hullnum < NUM_HULLS; g_hullnum++) {
			surfs = read_surfaces(polyfiles[g_hullnum]);
			detailbrushes = ReadBrushes(brushfiles[g_hullnum]);
			{
				int hullnum = g_hullnum;
				if (surfs->mins[0] > surfs->maxs[0]) {
					Developer(
						developer_level::message,
						"model %d hull %d empty\n",
						modnum,
						hullnum
					);
				} else {
					double3_array mins = vector_subtract(
						surfs->mins, g_hull_size[hullnum][0]
					);
					double3_array maxs = vector_subtract(
						surfs->maxs, g_hull_size[hullnum][1]
					);
					for (std::size_t i = 0; i < 3; ++i) {
						if (mins[i] > maxs[i]) {
							double tmp;
							tmp = (mins[i] + maxs[i]) / 2;
							mins[i] = tmp;
							maxs[i] = tmp;
						}
					}
					for (std::size_t i = 0; i < 3; ++i) {
						model->maxs[i] = std::max(
							model->maxs[i], (float) maxs[i]
						);
						model->mins[i] = std::min(
							model->mins[i], (float) mins[i]
						);
					}
				}
			}
			nodes = SolidBSP(surfs, detailbrushes, modnum == 0);
			if (g_nummodels == 1
				&& !g_nofill) // assume non-world bmodels are simple
			{
				nodes = FillOutside(nodes, (g_bLeaked != true), g_hullnum);
			}
			FreePortals(nodes);
			/*
				KGP 12/31/03 - need to test that the head clip node isn't
			empty; if it is we need to set model->headnode equal to the
			content type of the head, or create a trivial single-node case
			where the content type is the same for both leaves if setting
			the content type is invalid.
			*/
			if (nodes->is_leaf_node()) // empty!
			{
				model->headnode[g_hullnum] = std::to_underlying(
					nodes->contents
				);
			} else {
				model->headnode[g_hullnum] = g_numclipnodes;
				WriteClipNodes(nodes);
			}
		}
	}

	{
		entity_t* ent;
		ent = EntityForModel(modnum);
		if (ent != &g_entities[0]
			&& has_key_value(ent, u8"zhlt_minsmaxs")) {
			double3_array const origin = get_double3_for_key(
				*ent, u8"origin"
			);
			double3_array mins, maxs;
			if (sscanf(
					(char const *) value_for_key(ent, u8"zhlt_minsmaxs")
						.data(),
					"%lf %lf %lf %lf %lf %lf",
					&mins[0],
					&mins[1],
					&mins[2],
					&maxs[0],
					&maxs[1],
					&maxs[2]
				)
				== 6) {
				model->mins = to_float3(vector_subtract(mins, origin));
				model->maxs = to_float3(vector_subtract(maxs, origin));
			}
		}
	}
	Developer(
		developer_level::message,
		"model %d - mins=(%g,%g,%g) maxs=(%g,%g,%g)\n",
		modnum,
		model->mins[0],
		model->mins[1],
		model->mins[2],
		model->maxs[0],
		model->maxs[1],
		model->maxs[2]
	);
	if (model->mins[0] > model->maxs[0]) {
		entity_t* ent = EntityForModel(g_nummodels - 1);
		if (g_nummodels - 1 != 0 && ent == &g_entities[0]) {
			ent = nullptr;
		}
		Warning(
			"Empty solid entity: model %d (entity: classname \"%s\", origin \"%s\", targetname \"%s\")",
			g_nummodels - 1,
			(ent ? (char const *) get_classname(*ent).data() : "unknown"),
			(ent ? (char const *) value_for_key(ent, u8"origin").data()
				 : "unknown"),
			(ent ? (char const *) value_for_key(ent, u8"targetname").data()
				 : "unknown")
		);
		model->mins = {}; // Fix "backward minsmaxs" in HL
		model->maxs = {};
	} else if (novisiblebrushes) {
		entity_t* ent = EntityForModel(g_nummodels - 1);
		if (g_nummodels - 1 != 0 && ent == &g_entities[0]) {
			ent = nullptr;
		}
		Warning(
			"No visible brushes in solid entity: model %d (entity: classname \"%s\", origin \"%s\", targetname \"%s\", range (%.0f,%.0f,%.0f) - (%.0f,%.0f,%.0f))",
			g_nummodels - 1,
			(ent ? (char const *) get_classname(*ent).data() : "unknown"),
			(ent ? (char const *) value_for_key(ent, u8"origin").data()
				 : "unknown"),
			(ent ? (char const *) value_for_key(ent, u8"targetname").data()
				 : "unknown"),
			model->mins[0],
			model->mins[1],
			model->mins[2],
			model->maxs[0],
			model->maxs[1],
			model->maxs[2]
		);
	}
	return true;
}

// =====================================================================================
//  Usage
// =====================================================================================
static void Usage() {
	Banner();

	Log("\n-= %s Options =-\n\n", (char const *) g_Program.data());
	Log("    -leakonly      : Run BSP only enough to check for LEAKs\n");
	Log("    -subdivide #   : Sets the face subdivide size\n");
	Log("    -maxnodesize # : Sets the maximum portal node size\n\n");
	Log("    -notjunc       : Don't break edges on t-junctions     (not for final runs)\n"
	);
	Log("    -nobrink       : Don't smooth brinks                  (not for final runs)\n"
	);
	Log("    -noclip        : Don't process the clipping hull      (not for final runs)\n"
	);
	Log("    -nofill        : Don't fill outside (will mask LEAKs) (not for final runs)\n"
	);
	Log("    -noinsidefill  : Don't fill empty spaces\n");
	Log("    -noopt         : Don't optimize planes on BSP write   (not for final runs)\n"
	);
	Log("    -noclipnodemerge: Don't optimize clipnodes\n");
	Log("    -texdata #     : Alter maximum texture memory limit (in kb)\n"
	);
	Log("    -chart         : display bsp statitics\n");
	Log("    -low | -high   : run program an altered priority level\n");
	Log("    -nolog         : don't generate the compile logfiles\n");
	Log("    -threads #     : manually specify the number of threads to run\n"
	);
#ifdef SYSTEM_WIN32
	Log("    -estimate      : display estimated time during compile\n");
#endif
#ifdef SYSTEM_POSIX
	Log("    -noestimate    : do not display continuous compile time estimates\n"
	);
#endif

	Log("    -nonulltex     : Don't strip NULL faces\n");

	Log("    -nohull2       : Don't generate hull 2 (the clipping hull for large monsters and pushables)\n"
	);

	Log("    -viewportal    : Show portal boundaries in 'mapname_portal.pts' file\n"
	);

	Log("    -verbose       : compile with verbose messages\n");
	Log("    -noinfo        : Do not show tool configuration information\n"
	);
	Log("    -dev %s : compile with developer logging\n\n",
		(char const *) developer_level_options.data());
	Log("    mapfile        : The mapfile to compile\n\n");

	exit(1);
}

// =====================================================================================
//  Settings
// =====================================================================================
static void Settings() {
	char const * tmp;

	if (!g_info) {
		return;
	}

	Log("\nCurrent %s Settings\n", (char const *) g_Program.data());
	Log("Name               |  Setting  |  Default\n"
		"-------------------|-----------|-------------------------\n");

	// ZHLT Common Settings
	Log("threads             [ %7td ] [  Varies ]\n", g_numthreads);
	Log("verbose             [ %7s ] [ %7s ]\n",
		g_verbose ? "on" : "off",
		cli_option_defaults::verbose ? "on" : "off");
	Log("log                 [ %7s ] [ %7s ]\n",
		g_log ? "on" : "off",
		cli_option_defaults::log ? "on" : "off");
	Log("developer           [ %7s ] [ %7s ]\n",
		(char const *) name_of_developer_level(g_developer).data(),
		(char const *)
			name_of_developer_level(cli_option_defaults::developer)
				.data());
	Log("chart               [ %7s ] [ %7s ]\n",
		g_chart ? "on" : "off",
		cli_option_defaults::chart ? "on" : "off");
	Log("estimate            [ %7s ] [ %7s ]\n",
		g_estimate ? "on" : "off",
		cli_option_defaults::estimate ? "on" : "off");
	Log("max texture memory  [ %7td ] [ %7td ]\n",
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
	Log("priority            [ %7s ] [ %7s ]\n", tmp, "Normal");
	Log("\n");

	// HLBSP Specific Settings
	Log("noclip              [ %7s ] [ %7s ]\n",
		g_noclip ? "on" : "off",
		DEFAULT_NOCLIP ? "on" : "off");
	Log("nofill              [ %7s ] [ %7s ]\n",
		g_nofill ? "on" : "off",
		DEFAULT_NOFILL ? "on" : "off");
	Log("noinsidefill        [ %7s ] [ %7s ]\n",
		g_noinsidefill ? "on" : "off",
		DEFAULT_NOINSIDEFILL ? "on" : "off");
	Log("noopt               [ %7s ] [ %7s ]\n",
		g_noopt ? "on" : "off",
		DEFAULT_NOOPT ? "on" : "off");
	Log("no clipnode merging [ %7s ] [ %7s ]\n",
		g_noclipnodemerge ? "on" : "off",
		DEFAULT_NOCLIPNODEMERGE ? "on" : "off");
	Log("null tex. stripping [ %7s ] [ %7s ]\n",
		g_bUseNullTex ? "on" : "off",
		cli_option_defaults::nulltex ? "on" : "off");
	Log("notjunc             [ %7s ] [ %7s ]\n",
		g_notjunc ? "on" : "off",
		DEFAULT_NOTJUNC ? "on" : "off");
	Log("nobrink             [ %7s ] [ %7s ]\n",
		g_nobrink ? "on" : "off",
		DEFAULT_NOBRINK ? "on" : "off");
	Log("subdivide size      [ %7d ] [ %7zd ] (Min %d) (Max %d)\n",
		g_subdivide_size,
		DEFAULT_SUBDIVIDE_SIZE,
		MIN_SUBDIVIDE_SIZE,
		MAX_SUBDIVIDE_SIZE);
	Log("max node size       [ %7d ] [ %7d ] (Min %d) (Max %d)\n",
		g_maxnode_size,
		DEFAULT_MAXNODE_SIZE,
		MIN_MAXNODE_SIZE,
		MAX_MAXNODE_SIZE);
	Log("remove hull 2       [ %7s ] [ %7s ]\n",
		g_nohull2 ? "on" : "off",
		"off");
	Log("\n\n");
}

// =====================================================================================
//  ProcessFile
// =====================================================================================
static void
ProcessFile(std::filesystem::path const & mapBasePath, bsp_data& bspData) {
	// delete existing files
	g_portfilename = path_to_temp_file_with_extension(
		mapBasePath, u8".prt"
	);
	std::filesystem::remove(g_portfilename);

	g_pointfilename = path_to_temp_file_with_extension(
		mapBasePath, u8".pts"
	);
	std::filesystem::remove(g_pointfilename);

	g_linefilename = path_to_temp_file_with_extension(
		mapBasePath, u8".lin"
	);
	std::filesystem::remove(g_linefilename);

	g_extentfilename = path_to_temp_file_with_extension(
		mapBasePath, u8".ext"
	);
	std::filesystem::remove(g_extentfilename);
	// open the hull files

	for (hull_count i = 0; i < NUM_HULLS; ++i) {
		// mapname.p[0-3]
		std::filesystem::path polyFilePath{
			path_to_temp_file_with_extension(
				mapBasePath, polyFileExtensions[i]
			)
		};

		polyfiles[i] = fopen(polyFilePath.c_str(), "r");

		if (!polyfiles[i]) {
			Error("Can't open %s", polyFilePath.c_str());
		}

		std::filesystem::path brushFilePath{
			path_to_temp_file_with_extension(
				mapBasePath, brushFileExtensions[i]
			)
		};
		brushfiles[i] = fopen(brushFilePath.c_str(), "r");
		if (!brushfiles[i]) {
			Error("Can't open %s", brushFilePath.c_str());
		}
	}
	{
		std::filesystem::path filePath{
			path_to_temp_file_with_extension(mapBasePath, u8".hsz")
		};
		FILE* f = fopen(filePath.c_str(), "r");
		if (!f) {
			Warning("Couldn't open %s", filePath.c_str());
		} else {
			for (hull_count i = 0; i < NUM_HULLS; i++) {
				float x1, y1, z1;
				float x2, y2, z2;
				int const count = fscanf(
					f, "%f %f %f %f %f %f\n", &x1, &y1, &z1, &x2, &y2, &z2
				);
				if (count != 6) {
					Error("Load hull size (line %i): scanf failure", i + 1);
				}
				g_hull_size[i][0][0] = x1;
				g_hull_size[i][0][1] = y1;
				g_hull_size[i][0][2] = z1;
				g_hull_size[i][1][0] = x2;
				g_hull_size[i][1][1] = y2;
				g_hull_size[i][1][2] = z2;
			}
			fclose(f);
		}
	}

	g_bspfilename = path_to_temp_file_with_extension(mapBasePath, u8".bsp");
	// load the output of csg
	LoadBSPFile(g_bspfilename.c_str());
	parse_entities_from_bsp_file();

	Settings();

	{
		std::filesystem::path planeFilePath{
			path_to_temp_file_with_extension(mapBasePath, u8".pln")
		};
		FILE* planefile = fopen(planeFilePath.c_str(), "rb");
		if (!planefile) {
			Warning("Couldn't open %s", planeFilePath.c_str());
			g_mapPlanes.clear(); // Unnecessary?

			for (int i = 0; i < g_numplanes; i++) {
				mapplane_t& mp = g_mapPlanes.emplace_back();
				dplane_t const & dp = g_dplanes[i];
				mp.normal = to_double3(dp.normal);
				mp.dist = dp.dist;
				mp.type = dp.type;
			}
		} else if (q_filelength(planefile)
				   == g_numplanes * sizeof(mapplane_t)) {
			// TODO: The value initialization in .resize() here isn't
			// actually necessary, as the values are overwritten right
			// after. Consider adding a version of resize that doesn't
			// initialize values unless the element type requires it
			g_mapPlanes.resize(g_numplanes, {});
			SafeRead(
				planefile,
				g_mapPlanes.data(),
				g_numplanes * sizeof(mapplane_t)
			);
			fclose(planefile);
		} else {
			Error("Invalid plane data");
		}
	}
	// init the tables to be shared by all models
	BeginBSPFile();

	// process each model individually
	while (ProcessModel(bspData))
		;

	// write the updated bsp file out
	FinishBSPFile(bspData);

	// Because the bsp file has been updated, these polyfiles are no longer
	// valid.
	for (int i = 0; i < NUM_HULLS; i++) {
		fclose(polyfiles[i]);
		polyfiles[i] = nullptr;
		std::filesystem::remove(path_to_temp_file_with_extension(
			mapBasePath, polyFileExtensions[i]
		));
		fclose(brushfiles[i]);
		brushfiles[i] = nullptr;
		std::filesystem::remove(path_to_temp_file_with_extension(
			mapBasePath, brushFileExtensions[i]
		));
	}
	std::filesystem::remove(
		path_to_temp_file_with_extension(mapBasePath, u8".hsz")
	);
	std::filesystem::remove(
		path_to_temp_file_with_extension(mapBasePath, u8".pln")
	);
}

// =====================================================================================
//  main
// =====================================================================================
int main(int const argc, char** argv) {
	int i;
	char const * mapname_from_arg = nullptr;

	g_Program = u8"HLBSP";

	int argcold = argc;
	char** argvold = argv;
	{
		int argc;
		char** argv;
		ParseParamFile(argcold, argvold, argc, argv);
		{
			// if we dont have any command line argvars, print out usage and
			// die
			if (argc == 1) {
				Usage();
			}

			// check command line args
			for (i = 1; i < argc; i++) {
				if (argv[i] == "-threads"sv) {
					if (i + 1 < argc) // added "1" .--vluzacn
					{
						g_numthreads = string_to_number<std::ptrdiff_t>(
										   argv[++i]
						)
										   .value_or(-1);

						if (std::cmp_greater(g_numthreads, MAX_THREADS)) {
							Log("Expected value below %zu for '-threads'\n",
								MAX_THREADS);
							Usage();
						}
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-notjunc"
						   )) {
					g_notjunc = true;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-nobrink"
						   )) {
					g_nobrink = true;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-noclip"
						   )) {
					g_noclip = true;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-nofill"
						   )) {
					g_nofill = true;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-noinsidefill"
						   )) {
					g_noinsidefill = true;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-estimate"
						   )) {
					g_estimate = true;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-noestimate"
						   )) {
					g_estimate = false;
				}

				else if (strings_equal_with_ascii_case_insensitivity(
							 argv[i], u8"-dev"
						 )) {
					if (i + 1 < argc) // added "1" .--vluzacn
					{
						std::optional<developer_level> dl{
							developer_level_from_string((char8_t*) argv[++i]
							)
						};
						if (dl) {
							g_developer = dl.value();
						} else {
							Log("Invalid developer level");
							Usage();
						}
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
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-leakonly"
						   )) {
					g_bLeakOnly = true;
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
				}

				else if (strings_equal_with_ascii_case_insensitivity(
							 argv[i], u8"-nonulltex"
						 )) {
					g_bUseNullTex = false;
				}

				else if (strings_equal_with_ascii_case_insensitivity(
							 argv[i], u8"-nohull2"
						 )) {
					g_nohull2 = true;
				}

				else if (strings_equal_with_ascii_case_insensitivity(
							 argv[i], u8"-noopt"
						 )) {
					g_noopt = true;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-noclipnodemerge"
						   )) {
					g_noclipnodemerge = true;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-subdivide"
						   )) {
					if (i + 1 < argc) // added "1" .--vluzacn
					{
						g_subdivide_size = atoi(argv[++i]);
						if (g_subdivide_size > MAX_SUBDIVIDE_SIZE) {
							Warning(
								"Maximum value for subdivide size is %i, '-subdivide %i' ignored",
								MAX_SUBDIVIDE_SIZE,
								g_subdivide_size
							);
							g_subdivide_size = MAX_SUBDIVIDE_SIZE;
						} else if (g_subdivide_size < MIN_SUBDIVIDE_SIZE) {
							Warning(
								"Mininum value for subdivide size is %i, '-subdivide %i' ignored",
								MIN_SUBDIVIDE_SIZE,
								g_subdivide_size
							);
							g_subdivide_size
								= MIN_SUBDIVIDE_SIZE; // MAX_SUBDIVIDE_SIZE;
													  // //--vluzacn
						}
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-maxnodesize"
						   )) {
					if (i + 1 < argc) // added "1" .--vluzacn
					{
						g_maxnode_size = atoi(argv[++i]);
						if (g_maxnode_size > MAX_MAXNODE_SIZE) {
							Warning(
								"Maximum value for max node size is %i, '-maxnodesize %i' ignored",
								MAX_MAXNODE_SIZE,
								g_maxnode_size
							);
							g_maxnode_size = MAX_MAXNODE_SIZE;
						} else if (g_maxnode_size < MIN_MAXNODE_SIZE) {
							Warning(
								"Mininimum value for max node size is %i, '-maxnodesize %i' ignored",
								MIN_MAXNODE_SIZE,
								g_maxnode_size
							);
							g_maxnode_size
								= MIN_MAXNODE_SIZE; // MAX_MAXNODE_SIZE;
													// //vluzacn
						}
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-viewportal"
						   )) {
					g_viewportal = true;
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
				Log("No mapfile specified\n");
				Usage();
			}
			g_Mapname = std::filesystem::path(
				mapname_from_arg, std::filesystem::path::auto_format
			);
			g_Mapname.replace_extension(std::filesystem::path{});

			OpenLog();
			atexit(CloseLog);
			ThreadSetDefault();
			ThreadSetPriority(g_threadpriority);
			LogStart(argcold, argvold);
			log_arguments(argc, argv);

			CheckForErrorLog();

			hlassume(CalcFaceExtents_test(), assume_first);
			dtexdata_init();
			// Settings();
			//  END INIT

			// Load the .void files for allowable entities in the void
			{
				// try looking in the current directory
				std::filesystem::path strSystemEntitiesVoidFile{
					entitiesVoidFilename,
					std::filesystem::path::generic_format
				};
				if (!std::filesystem::exists(strSystemEntitiesVoidFile)) {
					// try looking in the directory we were run from
					strSystemEntitiesVoidFile
						= get_path_to_directory_with_executable(argv)
						/ strSystemEntitiesVoidFile;
				}

				// Load default entities.void
				LoadAllowableOutsideList(strSystemEntitiesVoidFile.c_str());

				// Load the optional level specific lights from
				// <mapname>.void
				LoadAllowableOutsideList(path_to_temp_file_with_extension(
											 g_Mapname, entitiesVoidExt
				)
											 .c_str());
			}

			// BEGIN BSP
			time_counter timeCounter;

			ProcessFile(g_Mapname, bspGlobals);

			LogTimeElapsed(timeCounter.get_total());
			// END BSP

			FreeAllowableOutsideList();
		}
	}
	return 0;
}
