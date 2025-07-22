/*

	VISIBLE INFORMATION SET    -aka-    V I S

	Code based on original code from Valve Software,
	Modified by Sean "Zoner" Cavanaugh (seanc@gearboxsoftware.com) with
   permission. Modified by Tony "Merl" Moore (merlinis@bigpond.net.au)
	Contains code by Skyler "Zipster" York (zipster89134@hotmail.com) -
   Included with permission. Modified by amckern (amckern@yahoo.com)
	Modified by vluzacn (vluzacn@163.com)
	Modified by seedee (cdaniel9000@gmail.com)
	Modified by Oskar Larsson Högfeldt (AKA Oskar Potatis) (oskar@oskar.pm)

*/

#include "vis.h"

#include "bsp_file_sizes.h"
#include "cli_option_defaults.h"
#include "time_counter.h"
#include "winding.h"

#include <algorithm>
#include <fstream>	//FixPrt
#include <iostream> //FixPrt
#include <string>
#include <utility>
#include <vector> //FixPrt

using namespace std::literals;

int g_numportals = 0;
unsigned g_portalleafs = 0;

portal_t* g_portals;

leaf_t* g_leafs;
int* g_leafstarts;
int* g_leafcounts;
int g_leafcount_all;

// AJM: MVD
//

static byte* vismap;
static byte* vismap_p;
static byte* vismap_end; // past visfile
static int originalvismapsize;

byte* g_uncompressed; // [bitbytes*portalleafs]

unsigned g_bitbytes; // (portalleafs+63)>>3
unsigned g_bitlongs;

bool g_fastvis = DEFAULT_FASTVIS;
bool g_fullvis = DEFAULT_FULLVIS;
bool g_nofixprt = DEFAULT_NOFIXPRT; // seedee
bool g_estimate = cli_option_defaults::estimate;
bool g_chart = cli_option_defaults::chart;
bool g_info = cli_option_defaults::info;

unsigned int g_maxdistance = DEFAULT_MAXDISTANCE_RANGE;

int const g_overview_max = MAX_MAP_ENTITIES;
overview_t g_overview[g_overview_max];
int g_overview_count = 0;
leafinfo_t* g_leafinfos = nullptr;

int const g_room_max = MAX_MAP_ENTITIES;
room_t g_room[g_room_max];
int g_room_count = 0;

static int totalvis = 0;

// AJM: addded in
// =====================================================================================
//  GetParamsFromEnt
//      this function is called from parseentity when it encounters the
//      info_compile_parameters entity. each tool should have its own
//      version of this to handle its own specific settings.
// =====================================================================================
void GetParamsFromEnt(entity_t* mapent) {
	Log("\nCompile Settings detected from info_compile_parameters entity\n"
	);

	// verbose(choices) : "Verbose compile messages" : 0 = [ 0 : "Off" 1 :
	// "On" ]
	std::int32_t iTmp = IntForKey(mapent, u8"verbose");
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
	hlvis(choices) : "HLVIS" : 2 =
	[
		0 : "Off"
		1 : "Fast"
		2 : "Normal"
		3 : "Full"
	]
	*/
	iTmp = IntForKey(mapent, u8"hlvis");
	if (iTmp == 0) {
		Fatal(
			assume_TOOL_CANCEL,
			"%s flag was not checked in info_compile_parameters entity, execution of %s cancelled",
			g_Program,
			g_Program
		);
		CheckFatal();
	} else if (iTmp == 1) {
		g_fastvis = true;
		g_fullvis = false;
	} else if (iTmp == 2) {
		g_fastvis = false;
		g_fullvis = false;
	} else if (iTmp == 3) {
		g_fullvis = true;
		g_fastvis = false;
	}
	Log("%30s [ %-9s ]\n", "Fast VIS", g_fastvis ? "on" : "off");
	Log("%30s [ %-9s ]\n", "Full VIS", g_fullvis ? "on" : "off");

	///////////////////
	Log("\n");
}

// =====================================================================================
//  PlaneFromWinding
// =====================================================================================
static void PlaneFromWinding(winding_t* w, hlvis_plane_t* plane) {
	// calc plane
	float3_array const v1 = vector_subtract(w->points[2], w->points[1]);
	float3_array const v2 = vector_subtract(w->points[0], w->points[1]);
	plane->normal = cross_product(v2, v1);
	normalize_vector(plane->normal);
	plane->dist = dot_product(w->points[0], plane->normal);
}

// =====================================================================================
//  NewWinding
// =====================================================================================
static winding_t* NewWinding(int const points) {
	winding_t* w;
	int size;

	if (points > MAX_POINTS_ON_WINDING) {
		Error("NewWinding: %i points > MAX_POINTS_ON_WINDING", points);
	}

	size = (int) (intptr_t) ((winding_t*) 0)->points[points].data();
	w = (winding_t*) calloc(1, size);

	return w;
}

//=============================================================================

// =====================================================================================
//  GetNextPortal
//      Returns the next portal for a thread to work on
//      Returns the portals from the least complex, so the later ones can
//      reuse the earlier information.
// =====================================================================================
static portal_t* GetNextPortal() {
	int j;
	portal_t* p;
	portal_t* tp;
	int min;

	{
		if (GetThreadWork() == -1) {
			return nullptr;
		}
		ThreadLock();

		min = 99999;
		p = nullptr;

		for (j = 0, tp = g_portals; j < g_numportals * 2; j++, tp++) {
			if (tp->nummightsee < min && tp->status == stat_none) {
				min = tp->nummightsee;
				p = tp;
			}
		}

		if (p) {
			p->status = stat_working;
		}

		ThreadUnlock();

		return p;
	}
}

// =====================================================================================
//  LeafThread
// =====================================================================================

static void LeafThread(int unused) {
	portal_t* p;

	while (1) {
		if (!(p = GetNextPortal())) {
			return;
		}

		PortalFlow(p);

		Verbose(
			"portal:%4i  mightsee:%4i  cansee:%4i\n",
			(int) (p - g_portals),
			p->nummightsee,
			p->numcansee
		);
	}
}

// Recursively add `add` to `current` visibility leaf.
std::unordered_map<int, bool> leaf_flow_add_exclude = {};

static void LeafFlowNeighborAddLeaf(
	int const current, int const add, int const neighbor
) {
	auto outbuffer = g_uncompressed + current * g_bitbytes;

	outbuffer[add >> 3] |= (1 << (add & 7));
	leaf_flow_add_exclude[current] = true;

	if (neighbor == 0) {
		return;
	}

	auto leaf = &g_leafs[current];

	for (int i = 0; i < leaf->numportals; i++) {
		auto p = leaf->portals[i];

		if (leaf_flow_add_exclude[p->leaf]) {
			// Log("leaf %d neighbor %d is excluded\n", current, p->leaf);
			continue;
		}

		LeafFlowNeighborAddLeaf(p->leaf, add, neighbor - 1);
	}
}

// =====================================================================================
//  LeafFlow
//      Builds the entire visibility list for a leaf
// =====================================================================================
static void LeafFlow(int const leafnum) {
	leaf_t* leaf;
	byte* outbuffer;
	byte compressed[MAX_MAP_LEAFS / 8]{};
	unsigned i;
	unsigned j;
	int k;
	int tmp;
	int numvis;
	byte* dest;
	portal_t* p;

	//
	// flow through all portals, collecting visible bits
	//
	outbuffer = g_uncompressed + leafnum * g_bitbytes;
	leaf = &g_leafs[leafnum];
	tmp = 0;

	unsigned const offset = leafnum >> 3;
	unsigned const bit = (1 << (leafnum & 7));

	for (i = 0; i < leaf->numportals; i++) {
		p = leaf->portals[i];
		if (p->status != stat_done) {
			Error("portal not done (leaf %d)", leafnum);
		}

		{
			byte* dst = outbuffer;
			byte* src = p->visbits;
			for (j = 0; j < g_bitbytes; j++, dst++, src++) {
				*dst |= *src;
			}
		}

		if ((tmp == 0) && (outbuffer[offset] & bit)) {
			tmp = 1;
			Warning("Leaf portals saw into leaf");
			Log("    Problem at portal between leaves %i and %i:\n   ",
				leafnum,
				p->leaf);
			for (k = 0; k < p->winding->numpoints; k++) {
				Log("    (%4.3f %4.3f %4.3f)\n",
					p->winding->points[k][0],
					p->winding->points[k][1],
					p->winding->points[k][2]);
			}
			Log("\n");
		}
	}

	outbuffer[offset] |= bit;

	if (g_leafinfos[leafnum].isoverviewpoint) {
		for (i = 0; i < g_portalleafs; i++) {
			outbuffer[i >> 3] |= (1 << (i & 7));
		}
	}
	for (i = 0; i < g_portalleafs; i++) {
		if (g_leafinfos[i].isskyboxpoint) {
			outbuffer[i >> 3] |= (1 << (i & 7));
		}
	}

	numvis = 0;
	for (i = 0; i < g_portalleafs; i++) {
		if (outbuffer[i >> 3] & (1 << (i & 7))) {
			numvis++;
		}
	}

	//
	// compress the bit string
	//
	Verbose("leaf %4i : %4i visible\n", leafnum, numvis);
	totalvis += numvis;

	byte buffer2[MAX_MAP_LEAFS / 8];
	int diskbytes = (g_leafcount_all + 7) >> 3;
	std::fill_n(buffer2, diskbytes, 0);
	for (i = 0; i < g_portalleafs; i++) {
		for (j = 0; j < g_leafcounts[i]; j++) {
			int srcofs = i >> 3;
			int srcbit = 1 << (i & 7);
			int dstofs = (g_leafstarts[i] + j) >> 3;
			int dstbit = 1 << ((g_leafstarts[i] + j) & 7);
			if (outbuffer[srcofs] & srcbit) {
				buffer2[dstofs] |= dstbit;
			}
		}
	}
	i = CompressVis(buffer2, diskbytes, compressed, sizeof(compressed));

	dest = vismap_p;
	vismap_p += i;

	if (vismap_p > vismap_end) {
		Error("Vismap expansion overflow");
	}

	for (j = 0; j < g_leafcounts[leafnum]; j++) {
		g_dleafs[g_leafstarts[leafnum] + j + 1].visofs = dest - vismap;
	}

	memcpy(dest, compressed, i);
}

// =====================================================================================
//  CalcPortalVis
// =====================================================================================
static void CalcPortalVis() {
	// g_fastvis just uses mightsee for a very loose bound
	if (g_fastvis) {
		for (std::size_t i = 0; i < g_numportals * 2; i++) {
			g_portals[i].visbits = g_portals[i].mightsee;
			g_portals[i].status = stat_done;
		}
		return;
	}

	NamedRunThreadsOn(g_numportals * 2, g_estimate, LeafThread);
}

// AJM: MVD
// =====================================================================================
//  SaveVisData
// =====================================================================================
void SaveVisData(char const * filename) {
	FILE* fp = fopen(filename, "wb");

	if (!fp) {
		return;
	}

	SafeWrite(
		fp, g_dvisdata.data(), (vismap_p - (byte*) g_dvisdata.data())
	);

	// BUG BUG BUG!
	// Leaf offsets need to be saved too!!!!
	for (int i = 0; i < g_numleafs; i++) {
		SafeWrite(fp, &g_dleafs[i].visofs, sizeof(int));
	}

	fclose(fp);
}

// =====================================================================================
//  CalcVis
// =====================================================================================
static void CalcVis() {
	std::filesystem::path const visDataFilePath{
		path_to_temp_file_with_extension(g_Mapname, u8".vdt").c_str()
	};

	// Remove this file
	std::filesystem::remove(visDataFilePath.c_str());

	NamedRunThreadsOn(g_numportals * 2, g_estimate, BasePortalVis);

	// First do a normal VIS, save to file, then redo MaxDistVis

	CalcPortalVis();

	// Add additional leaves to the uncompressed vis.
	for (unsigned i = 0; i < g_portalleafs; i++) {
		if (!g_leafinfos[i].additional_leaves.empty()) {
			for (int leaf : g_leafinfos[i].additional_leaves) {
				LeafFlowNeighborAddLeaf(i, leaf, g_leafinfos[i].neighbor);
				leaf_flow_add_exclude.clear();
			}
		}
	}

	//
	// assemble the leaf vis lists by oring and compressing the portal lists
	//
	for (unsigned i = 0; i < g_portalleafs; i++) {
		LeafFlow(i);
	}

	Log("average leafs visible: %i\n", totalvis / g_portalleafs);

	if (g_maxdistance) {
		totalvis = 0;

		Log("saving visdata to %s...\n", visDataFilePath.c_str());
		SaveVisData(visDataFilePath.c_str());

		// We need to reset the uncompressed variable and portal visbits
		free(g_uncompressed);
		g_uncompressed = (byte*) calloc(g_portalleafs, g_bitbytes);

		vismap_p = (byte*) g_dvisdata.data();

		// We don't need to run BasePortalVis again
		NamedRunThreadsOn(g_portalleafs, g_estimate, MaxDistVis);

		// No need to run this - MaxDistVis now writes directly to visbits
		// after the initial VIS
		// CalcPortalVis();

		for (unsigned i = 0; i < g_portalleafs; i++) {
			LeafFlow(i);
		}

		Log("average maxdistance leafs visible: %i\n",
			totalvis / g_portalleafs);
	}
}

// =====================================================================================
//  CheckNullToken
// =====================================================================================
static inline void CheckNullToken(char const * const token) {
	if (token == nullptr) {
		Error("LoadPortals: Damaged or invalid .prt file\n");
	}
}

// =====================================================================================
//  LoadPortals
// =====================================================================================
static void LoadPortals(char* portal_image) {
	int i, j;
	portal_t* p;
	leaf_t* l;
	int numpoints;
	winding_t* w;
	int leafnums[2];
	hlvis_plane_t plane;
	char const * const seperators = " ()\r\n\t";
	char* token;

	token = strtok(portal_image, seperators);
	CheckNullToken(token);
	if (!sscanf(token, "%u", &g_portalleafs)) {
		Error("LoadPortals: failed to read header: number of leafs");
	}

	token = strtok(nullptr, seperators);
	CheckNullToken(token);
	if (!sscanf(token, "%i", &g_numportals)) {
		Error("LoadPortals: failed to read header: number of portals");
	}

	Log("%4i portalleafs\n", g_portalleafs);
	Log("%4i numportals\n", g_numportals);

	g_bitbytes = ((g_portalleafs + 63) & ~63) >> 3;
	g_bitlongs = g_bitbytes / sizeof(long);

	// each file portal is split into two memory portals
	g_portals = (portal_t*) calloc(2 * g_numportals, sizeof(portal_t));
	g_leafs = (leaf_t*) calloc(g_portalleafs, sizeof(leaf_t));
	g_leafinfos = (leafinfo_t*) calloc(g_portalleafs, sizeof(leafinfo_t));
	g_leafcounts = (int*) calloc(g_portalleafs, sizeof(int));
	g_leafstarts = (int*) calloc(g_portalleafs, sizeof(int));

	originalvismapsize = g_portalleafs * ((g_portalleafs + 7) / 8);

	vismap = vismap_p = (byte*) g_dvisdata.data();
	vismap_end = vismap + MAX_MAP_VISIBILITY;

	if (g_portalleafs
		> MAX_MAP_LEAFS) { // this may cause hlvis to overflow, because
						   // numportalleafs can be larger than g_numleafs
						   // in some special cases
		Error(
			"Too many portalleafs (g_portalleafs(%d) > MAX_MAP_LEAFS(%zd)).",
			g_portalleafs,
			MAX_MAP_LEAFS
		);
	}
	g_leafcount_all = 0;
	for (i = 0; i < g_portalleafs; i++) {
		unsigned rval = 0;
		token = strtok(nullptr, seperators);
		CheckNullToken(token);
		rval += sscanf(token, "%i", &g_leafcounts[i]);
		if (rval != 1) {
			Error("LoadPortals: read leaf %i failed", i);
		}
		g_leafstarts[i] = g_leafcount_all;
		g_leafcount_all += g_leafcounts[i];
	}
	if (g_leafcount_all
		!= g_dmodels[0]
			   .visleafs) { // internal error (this should never happen)
		Error(
			"Corrupted leaf mapping (g_leafcount_all(%d) != g_dmodels[0].visleafs(%d)).",
			g_leafcount_all,
			g_dmodels[0].visleafs
		);
	}
	for (i = 0; i < g_portalleafs; i++) {
		for (j = 0; j < g_overview_count; j++) {
			int d = g_overview[j].visleafnum - g_leafstarts[i];
			if (0 <= d && d < g_leafcounts[i]) {
				if (g_overview[j].reverse) {
					g_leafinfos[i].isskyboxpoint = true;
				} else {
					g_leafinfos[i].isoverviewpoint = true;
				}
			}
		}

		for (j = 0; j < g_room_count; j++) {
			int d1 = g_room[j].visleafnum - g_leafstarts[i];

			if (0 <= d1 && d1 < g_leafcounts[i]) {
				for (int k = 0; k < g_portalleafs; k++) {
					int d2 = g_room[j].target_visleafnum - g_leafstarts[k];

					if (0 <= d2 && d2 < g_leafcounts[k]) {
						g_leafinfos[i].additional_leaves.push_back(k);
						g_leafinfos[i].neighbor = g_room[j].neighbor;
					}
				}
			}
		}
	}
	for (i = 0, p = g_portals; i < g_numportals; i++) {
		std::size_t rval = 0;

		token = strtok(nullptr, seperators);
		CheckNullToken(token);
		rval += sscanf(token, "%i", &numpoints);
		token = strtok(nullptr, seperators);
		CheckNullToken(token);
		rval += sscanf(token, "%i", &leafnums[0]);
		token = strtok(nullptr, seperators);
		CheckNullToken(token);
		rval += sscanf(token, "%i", &leafnums[1]);

		if (rval != 3) {
			Error("LoadPortals: reading portal %i", i);
		}
		if (numpoints > MAX_POINTS_ON_WINDING) {
			Error("LoadPortals: portal %i has too many points", i);
		}
		if (((unsigned) leafnums[0] > g_portalleafs)
			|| ((unsigned) leafnums[1] > g_portalleafs)) {
			Error("LoadPortals: reading portal %i", i);
		}

		w = p->winding = NewWinding(numpoints);
		w->original = true;
		w->numpoints = numpoints;

		for (j = 0; j < numpoints; j++) {
			float3_array v;
			std::size_t rval = 0;

			token = strtok(nullptr, seperators);
			CheckNullToken(token);
			rval += sscanf(token, "%f", &v[0]);
			token = strtok(nullptr, seperators);
			CheckNullToken(token);
			rval += sscanf(token, "%f", &v[1]);
			token = strtok(nullptr, seperators);
			CheckNullToken(token);
			rval += sscanf(token, "%f", &v[2]);

			if (rval != 3) {
				Error("LoadPortals: reading portal %i", i);
			}
			w->points[j] = v;
		}

		// calc plane
		PlaneFromWinding(w, &plane);

		// create forward portal
		l = &g_leafs[leafnums[0]];
		hlassume(
			l->numportals < MAX_PORTALS_ON_LEAF, assume_MAX_PORTALS_ON_LEAF
		);
		l->portals[l->numportals] = p;
		l->numportals++;

		p->winding = w;
		p->plane.normal = negate_vector(plane.normal);
		p->plane.dist = -plane.dist;
		p->leaf = leafnums[1];
		p++;

		// create backwards portal
		l = &g_leafs[leafnums[1]];
		hlassume(
			l->numportals < MAX_PORTALS_ON_LEAF, assume_MAX_PORTALS_ON_LEAF
		);
		l->portals[l->numportals] = p;
		l->numportals++;

		p->winding = NewWinding(w->numpoints);
		p->winding->numpoints = w->numpoints;
		for (j = 0; j < w->numpoints; j++) {
			p->winding->points[j] = w->points[w->numpoints - 1 - j];
		}

		p->plane = plane;
		p->leaf = leafnums[0];
		p++;
	}
}

// =====================================================================================
//  LoadPortalsByFilename
// =====================================================================================
static void LoadPortalsByFilename(char const * const filename) {
	std::optional<std::u8string> maybeContents = read_utf8_file(
		filename, true
	);
	if (!maybeContents) {
		Error(
			"Portal file '%s' could not be read, cannot VIS the map\n",
			filename
		);
	}

	LoadPortals((char*) maybeContents.value().data());
}

// =====================================================================================
//  Usage
// =====================================================================================
static void Usage() {
	Banner();

	Log("\n-= %s Options =-\n\n", g_Program);
	Log("    -full           : Full vis\n");
	Log("    -fast           : Fast vis\n\n");
	Log("    -nofixprt       : Disables optimization of portal file for import to J.A.C.K. map editor\n\n"
	); // seedee
	Log("    -texdata #      : Alter maximum texture memory limit (in kb)\n"
	);
	Log("    -chart          : display bsp statitics\n");
	Log("    -low | -high    : run program an altered priority level\n");
	Log("    -nolog          : don't generate the compile logfiles\n");
	Log("    -threads #      : manually specify the number of threads to run\n"
	);
#ifdef SYSTEM_WIN32
	Log("    -estimate       : display estimated time during compile\n");
#endif
#ifdef SYSTEM_POSIX
	Log("    -noestimate     : do not display continuous compile time estimates\n"
	);
#endif
	Log("    -maxdistance #  : Alter the maximum distance for visibility\n"
	);
	Log("    -verbose        : compile with verbose messages\n");
	Log("    -noinfo         : Do not show tool configuration information\n"
	);
	Log("    -dev #          : compile with developer message\n\n");
	Log("    mapfile         : The mapfile to compile\n\n");

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

	Log("\n-= Current %s Settings =-\n", g_Program);
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
	Log("developer           [ %7d ] [ %7d ]\n",
		(int) g_developer,
		(int) cli_option_defaults::developer);
	Log("chart               [ %7s ] [ %7s ]\n",
		g_chart ? "on" : "off",
		cli_option_defaults::chart ? "on" : "off");
	Log("estimate            [ %7s ] [ %7s ]\n",
		g_estimate ? "on" : "off",
		cli_option_defaults::estimate ? "on" : "off");
	Log("max texture memory  [ %7td ] [ %7td ]\n",
		g_max_map_miptex,
		cli_option_defaults::max_map_miptex);

	Log("max vis distance    [ %7d ] [ %7d ]\n",
		g_maxdistance,
		DEFAULT_MAXDISTANCE_RANGE);

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

	// HLVIS Specific Settings
	Log("fast vis            [ %7s ] [ %7s ]\n",
		g_fastvis ? "on" : "off",
		DEFAULT_FASTVIS ? "on" : "off");
	Log("full vis            [ %7s ] [ %7s ]\n",
		g_fullvis ? "on" : "off",
		DEFAULT_FULLVIS ? "on" : "off");
	Log("nofixprt            [ %7s ] [ %7s ]\n",
		g_nofixprt ? "on" : "off",
		DEFAULT_NOFIXPRT ? "on" : "off");

	Log("\n\n");
}

int VisLeafnumForPoint(float3_array const & point) {
	int nodenum;
	float dist;
	dnode_t* node;
	dplane_t* plane;

	nodenum = 0;
	while (nodenum >= 0) {
		node = &g_dnodes[nodenum];
		plane = &g_dplanes[node->planenum];
		dist = dot_product(point, plane->normal) - plane->dist;
		if (dist >= 0.0) {
			nodenum = node->children[0];
		} else {
			nodenum = node->children[1];
		}
	}

	return -nodenum - 2;
}

// seedee
// =====================================================================================
//  FixPrt
//      Imports portal file to vector, erases vis cache lines, overwrites
//      portal file
// =====================================================================================
void FixPrt(char const * portalfile) {
	Log("\nReading portal file '%s'\n", portalfile);

	std::vector<std::string> prtVector;
	std::ifstream inputFileStream{ portalfile }; // Import from .prt file

	if (!inputFileStream) // If import fails
	{
		Log("Failed reading portal file '%s', skipping optimization for J.A.C.K. map editor\n",
			portalfile);
		return;
	}

	std::string strInput;

	while (std::getline(inputFileStream, strInput)
	) // While there are still lines to read
	{
		if (strInput.size() > 0) { // If line is not empty add to vector
			prtVector.push_back(strInput);
		}
	}
	inputFileStream.close();

	std::size_t portalFileLines = prtVector.size(
	); // Count lines before optimization

	auto itPortalCoords = std::find_if(
		prtVector.begin(),
		prtVector.end(),
		[](std::string const & s) {
			return s.find('(')
				!= std::string::npos; // Point iterator to the first string
									  // that contains portal coordinates,
									  // which has a bracket
		}
	);

	bool skipFix = false;

	if (prtVector[1] == "0") // If portal count on line 2 is 0
	{
		Log("Number of portals in file is 0\n");
		skipFix = true;
	}
	if (itPortalCoords
		== prtVector.end()) // If it didn't find any line containing an
							// opening bracket (the portal coordinates)
	{
		Log("No portal coordinates detected in file\n");
		skipFix = true;
	}
	if (itPortalCoords
		== prtVector.begin()) // If the first line contains an opening
							  // bracket (possible portal coordinates)
	{
		Log("Unexpected possible portal coordinates at line 1\n");
		skipFix = true;
	}
	if (skipFix) {
		Log("Skipping optimization for J.A.C.K. map editor\n");
		return;
	}
	prtVector.erase( // Deletes from string 3 until string before portal
					 // coordinates
		prtVector.begin() + 2,
		itPortalCoords
	);

	std::size_t optimizedPortalFileLines = prtVector.size(
	); // Count lines after optimization

	Log("Reduced %zu lines to %zu\n",
		portalFileLines,
		optimizedPortalFileLines);

	// Print contents of vector
	/*
	for (std::string& line : prtVector)
		Log("%s\n", line.c_str());
	*/

	std::ofstream outputFileStream; // Output to .prt file

	outputFileStream.open(portalfile);

	if (outputFileStream.is_open()) {
		for (int i = 0; i < prtVector.size(); ++i) {
			outputFileStream << prtVector[i]
							 << "\n"; // Print each string as a new line
		}
		outputFileStream.close();

		Log("Optimization for J.A.C.K. map editor successful, writing portal file '%s'\n",
			portalfile);
	} else // If open fails
	{
		Log("Failed writing portal file '%s', skipping optimization for J.A.C.K. map editor\n",
			portalfile);
		return;
	}

	return;
}

// =====================================================================================
//  main
// =====================================================================================
int main(int const argc, char** argv) {
	std::u8string_view mapname_from_arg;

	g_Program = "HLVIS";

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

			for (std::size_t i = 1; i < argc; i++) {
				std::u8string_view const arg = (char8_t const *) argv[i];
				if (arg == u8"-threads") {
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
#ifdef SYSTEM_POSIX
				else if (arg == u8"-noestimate") {
					g_estimate = false;
				}
#endif
				else if (arg == u8"-fast") {
					Log("g_fastvis = true\n");
					g_fastvis = true;
				} else if (arg == u8"-full") {
					g_fullvis = true;
				} else if (arg == u8"-nofixprt") {
					g_nofixprt = true;
				} else if (arg == u8"-dev") {
					if (i + 1 < argc) // added "1" .--vluzacn
					{
						g_developer = (developer_level) atoi(argv[++i]);
					} else {
						Usage();
					}
				} else if (arg == u8"-verbose") {
					g_verbose = true;
				}

				else if (arg == u8"-noinfo") {
					g_info = false;
				} else if (arg == u8"-chart") {
					g_chart = true;
				} else if (arg == u8"-low") {
					g_threadpriority = q_threadpriority::eThreadPriorityLow;
				} else if (arg == u8"-high") {
					g_threadpriority
						= q_threadpriority::eThreadPriorityHigh;
				} else if (arg == u8"-nolog") {
					g_log = false;
				} else if (arg == u8"-texdata") {
					if (i + 1 < argc) // added "1" .--vluzacn
					{
						int x = atoi(argv[++i]) * 1024;

						// if (x > g_max_map_miptex) //--vluzacn
						{ g_max_map_miptex = x; }
					} else {
						Usage();
					}
				} else if (arg == u8"-maxdistance") {
					if (i + 1 < argc) // added "1" .--vluzacn
					{
						g_maxdistance = abs(atoi(argv[++i]));
					} else {
						Usage();
					}
				} else if (!arg.starts_with(u8'-')
						   && mapname_from_arg.empty()) {
					mapname_from_arg = arg;
				} else {
					Log("Unknown option \"%s\"\n",
						(char const *) arg.data());
					Usage();
				}
			}

			if (mapname_from_arg.empty()) {
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
			atexit(dtexdata_free);
			// END INIT

			// BEGIN VIS
			time_counter timeCounter;

			LoadBSPFile(
				path_to_temp_file_with_extension(g_Mapname, u8".bsp")
			);
			parse_entities_from_bsp_file();
			{
				for (std::size_t i = 0; i < g_numentities; i++) {
					std::u8string_view current_entity_classname
						= get_classname(g_entities[i]);

					if (current_entity_classname
						== u8"info_overview_point") {
						if (g_overview_count < g_overview_max) {
							float3_array const p{ get_float3_for_key(
								g_entities[i], u8"origin"
							) };
							g_overview[g_overview_count].origin = p;
							g_overview[g_overview_count].visleafnum
								= VisLeafnumForPoint(p);
							g_overview[g_overview_count].reverse
								= IntForKey(&g_entities[i], u8"reverse");
							g_overview_count++;
						}
					} else if (current_entity_classname
							   == u8"info_portal") {
						if (g_room_count < g_room_max) {
							float3_array const room_origin{
								get_float3_for_key(
									g_entities[i], u8"origin"
								)
							};
							g_room[g_room_count].visleafnum
								= VisLeafnumForPoint(room_origin);
							g_room[g_room_count].neighbor = std::clamp(
								IntForKey(&g_entities[i], u8"neighbor"),
								0,
								MAX_ROOM_NEIGHBOR
							);

							std::u8string_view target = value_for_key(
								&g_entities[i], u8"target"
							);

							if (target.empty()) {
								continue;
							}

							bool has_target = false;

							// Find the target entity.
							// Rewalk yes, very sad.
							for (int j = 0; j < g_numentities; j++) {
								// Find a `info_leaf` and check if its
								// targetname matches our target
								if (key_value_is(
										&g_entities[j],
										u8"classname",
										u8"info_leaf"
									)
									&& key_value_is(
										&g_entities[j],
										u8"targetname",
										target
									)) {
									float3_array const room_target_origin{
										get_float3_for_key(
											g_entities[j], u8"origin"
										)
									};
									g_room[g_room_count].target_visleafnum
										= VisLeafnumForPoint(
											room_target_origin
										);

									has_target = true;
								}
							}

							if (!has_target) {
								Warning(
									"Entity %zu (info_portal) does not have a target leaf.",
									i
								);
							}

							g_room_count++;
						}
					}
				}
			}
			LoadPortalsByFilename(
				path_to_temp_file_with_extension(g_Mapname, u8".prt")
					.c_str()
			);

			Settings();
			g_uncompressed = (byte*) calloc(g_portalleafs, g_bitbytes);

			CalcVis();

			g_visdatasize = vismap_p - (byte*) g_dvisdata.data();
			Log("g_visdatasize:%i  compressed from %i\n",
				g_visdatasize,
				originalvismapsize);

			if (!g_nofixprt) // seedee
			{
				FixPrt(path_to_temp_file_with_extension(g_Mapname, u8".prt")
						   .c_str());
			}

			if (g_chart) {
				print_bsp_file_sizes(bspGlobals);
			}

			WriteBSPFile(
				path_to_temp_file_with_extension(g_Mapname, u8".bsp")
					.c_str()
			);

			LogTimeElapsed(timeCounter.get_total());

			free(g_uncompressed);
			// END VIS
		}
	}

	return 0;
}
