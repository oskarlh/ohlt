#pragma once

#include "cmdlib.h"
#include "messages.h"
#include "win32fix.h"
#include "log.h"
#include "hlassert.h"
#include "mathlib.h"
#include "bspfile.h"
#include "threads.h"
#include "filelib.h"

#include "zones.h"
#include "cmdlinecfg.h"

#include <vector>
#include <unordered_map>

#define DEFAULT_MAXDISTANCE_RANGE 0


#define DEFAULT_FULLVIS false
#define DEFAULT_NOFIXPRT false
#define DEFAULT_FASTVIS false

#define	MAX_PORTALS	32768

#define RVIS_LEVEL_2

#define	MAX_POINTS_ON_FIXED_WINDING	32

struct winding_t
{
    bool original; // Don't free, it's part of the portal
    std::size_t numpoints;
    vec3_array points[MAX_POINTS_ON_FIXED_WINDING];
};

typedef struct
{
    vec3_array          normal;
    float           dist;
} plane_t;

typedef enum
{ 
    stat_none, 
    stat_working, 
    stat_done 
} vstatus_t;

typedef struct
{
    plane_t         plane;                                 // normal pointing into neighbor
    int             leaf;                                  // neighbor
    winding_t*      winding;
    vstatus_t       status;
    byte*           visbits;
    byte*           mightsee;
    unsigned        nummightsee;
    int             numcansee;
    std::uint_least32_t          zone;                                  // Which zone is this portal a member of
} portal_t;

struct sep_t
{
    sep_t* next;
    plane_t plane; // from portal is on positive side
};

typedef struct passage_s
{
    struct passage_s* next;
    int             from, to;                              // leaf numbers
    sep_t*          planes;
} passage_t;

#define	MAX_PORTALS_ON_LEAF		256
typedef struct leaf_s
{
    unsigned        numportals;
    passage_t*      passages;
    portal_t*       portals[MAX_PORTALS_ON_LEAF];
} leaf_t;

typedef struct pstack_s
{
    byte            mightsee[MAX_MAP_LEAFS / 8];           // bit string
    struct pstack_s* head;

    leaf_t*         leaf;
    portal_t*       portal;                                // portal exiting
    winding_t*      source;
    winding_t*      pass;

    winding_t       windings[3];                           // source, pass, temp in any order
    char            freewindings[3];

    const plane_t*  portalplane;

#ifdef RVIS_LEVEL_2
    int             clipPlaneCount;
    plane_t*        clipPlane;
#endif
} pstack_t;

typedef struct
{
    byte*           leafvis;                               // bit string
    //      byte            fullportal[MAX_PORTALS/8];              // bit string
    portal_t*       base;
    pstack_t        pstack_head;
} threaddata_t;


extern bool     g_fastvis;
extern bool     g_fullvis;

extern int      g_numportals;
extern unsigned g_portalleafs;

extern unsigned int g_maxdistance;

// This allows the current leaf to have portal to selected leaf.
// TODO: vector for target so it can do a lot. Though doing the entity won't be as simple.
// That means we need to parse string and what not. 
// For the time being, ONE target is good enough.
#define MAX_ROOM_NEIGHBOR 16
typedef struct
{
    int visleafnum;
    int target_visleafnum;
    // Traversal of neighbors being affected.
    int neighbor;
}
room_t;
extern const int g_room_max;
extern room_t g_room[];
extern int g_room_count;
extern std::unordered_map<int, bool> leaf_flow_add_exclude;

typedef struct
{
	vec3_array origin;
	int visleafnum;
	int reverse;
}
overview_t;
extern const int g_overview_max;
extern overview_t g_overview[];
extern int g_overview_count;

typedef struct
{
	bool isoverviewpoint;
	bool isskyboxpoint;
    // For info_portal
    std::vector<int> additional_leaves;
    int neighbor;
}
leafinfo_t;
extern leafinfo_t *g_leafinfos;

extern portal_t*g_portals;
extern leaf_t*  g_leafs;


extern byte*    g_uncompressed;
extern unsigned g_bitbytes;
extern unsigned g_bitlongs;

extern volatile int g_vislocalpercent;

extern Zones*          g_Zones;

extern void     BasePortalVis(int threadnum);


extern void		MaxDistVis(int threadnum);
//extern void		PostMaxDistVis(int threadnum);

extern void     PortalFlow(portal_t* p);
extern void     CalcAmbientSounds();
