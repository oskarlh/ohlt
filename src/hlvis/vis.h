#pragma once

#include "bspfile.h"
#include "cmdlib.h"
#include "cmdlinecfg.h"
#include "filelib.h"
#include "hlassert.h"
#include "log.h"
#include "mathlib.h"
#include "messages.h"
#include "threads.h"
#include "win32fix.h"

#include <unordered_map>
#include <vector>

#define DEFAULT_MAXDISTANCE_RANGE 0

#define DEFAULT_FULLVIS	 false
#define DEFAULT_NOFIXPRT false
#define DEFAULT_FASTVIS	 false

constexpr std::size_t MAX_PORTALS = 32768;

#define MAX_POINTS_ON_FIXED_WINDING 32

struct winding_t final {
	// Indicates if the winding is original (Don't free, it's
	// part of the portal)
	bool original;
	std::size_t numpoints;
	float3_array points[MAX_POINTS_ON_FIXED_WINDING];
};

struct hlvis_plane_t final {
	float3_array normal;
	float dist;
};

enum vstatus_t {
	stat_none,	  // No status
	stat_working, // Currently working
	stat_done	  // Process completed
};

// TODO: Rename! HLBSP also has a portal_t
struct portal_t final {
	hlvis_plane_t plane; // normal pointing into neighbor
	int leaf;			 // neighbor
	winding_t* winding;
	vstatus_t status;
	byte* visbits;
	byte* mightsee; // Potential visibility of the portal
	unsigned nummightsee;
	int numcansee;
	std::uint_least32_t zone; // Which zone is this portal a member of
};

struct sep_t final {
	sep_t* next;
	hlvis_plane_t plane; // from portal is on positive side
};

struct passage_t final {
	passage_t* next;
	int from, to; // leaf numbers
	sep_t* planes;
};

#define MAX_PORTALS_ON_LEAF 256

struct leaf_t final {
	unsigned numportals;
	passage_t* passages;
	portal_t* portals[MAX_PORTALS_ON_LEAF];
};

struct pstack_t final {
	byte mightsee[MAX_MAP_LEAFS / 8]; // bit string
	pstack_t* head;

	leaf_t* leaf;
	portal_t* portal; // portal exiting
	winding_t* source;
	winding_t* pass;

	std::array<winding_t, 3> windings; // source, pass, temp in any order
	std::array<bool, 3> freeWindings;

	hlvis_plane_t const * portalplane;

	int clipPlaneCount;
	hlvis_plane_t* clipPlane;
};

struct threaddata_t final {
	byte* leafvis; // bit string
	//      byte            fullportal[MAX_PORTALS/8];              // bit
	//      string
	portal_t* base;
	pstack_t pstack_head;
};

extern bool g_fastvis;
extern bool g_fullvis;

extern int g_numportals;
extern unsigned g_portalleafs;

extern unsigned int g_maxdistance;

// This allows the current leaf to have portal to selected leaf.
// TODO: vector for target so it can do a lot. Though doing the entity won't
// be as simple. That means we need to parse string and what not. For the
// time being, ONE target is good enough.
#define MAX_ROOM_NEIGHBOR 16

struct room_t final {
	int visleafnum;
	int target_visleafnum;
	// Traversal of neighbors being affected.
	int neighbor;
};

extern int const g_room_max;
extern room_t g_room[];
extern int g_room_count;
extern std::unordered_map<int, bool> leaf_flow_add_exclude;

struct overview_t final {
	float3_array origin;
	int visleafnum;
	int reverse;
};

extern int const g_overview_max;
extern overview_t g_overview[];
extern int g_overview_count;

struct leafinfo_t final {
	bool isoverviewpoint;
	bool isskyboxpoint;
	// For info_portal
	std::vector<int> additional_leaves;
	int neighbor;
};

extern leafinfo_t* g_leafinfos;

extern portal_t* g_portals;
extern leaf_t* g_leafs;

extern byte* g_uncompressed;
extern unsigned g_bitbytes;
extern unsigned g_bitlongs;

extern int volatile g_vislocalpercent;

extern void BasePortalVis(int threadnum);

extern void MaxDistVis(int threadnum);
// extern void		PostMaxDistVis(int threadnum);

extern void PortalFlow(portal_t* p);
extern void CalcAmbientSounds();
