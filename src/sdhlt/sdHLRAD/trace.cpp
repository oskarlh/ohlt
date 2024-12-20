#include "cmdlib.h"
#include "mathlib.h"
#include "bspfile.h"
#include "log.h" //--vluzacn
#include "winding.h"
#include "qrad.h"

// #define      ON_EPSILON      0.001

typedef struct tnode_s
{
    planetypes      type;
    vec3_array          normal;
    float           dist;
    int             children[2];
    int             pad;
} tnode_t;

static tnode_t* tnodes;
static tnode_t* tnode_p;

/*
 * ==============
 * MakeTnode
 * 
 * Converts the disk node structure into the efficient tracing structure
 * ==============
 */
static void     MakeTnode(const int nodenum)
{
    tnode_t*        t;
    int             i;
    dnode_t*        node;

    t = tnode_p++;

    node = g_dnodes.data() + nodenum;
    const dplane_t& plane = g_dplanes[node->planenum];

    t->type = plane.type;
    VectorCopy(plane.normal, t->normal);
	if (plane.normal[(plane.type)%3] < 0) {
		if (plane.type < 3)
		{
			Warning ("MakeTnode: negative plane");
		}
		else
		{
			Developer (DEVELOPER_LEVEL_MESSAGE, "Warning: MakeTnode: negative plane\n");
		}
	}
    t->dist = plane.dist;

    for (i = 0; i < 2; i++)
    {
        if (node->children[i] < 0)
            t->children[i] = g_dleafs[-node->children[i] - 1].contents;
        else
        {
            t->children[i] = tnode_p - tnodes;
            MakeTnode(node->children[i]);
        }
    }

}

/*
 * =============
 * MakeTnodes
 * 
 * Loads the node structure out of a .bsp file to be used for light occlusion
 * =============
 */
#if 0 // turn on for debugging. --vluzacn
#include <windows.h>
 #define PERR(bSuccess, api){if(!(bSuccess)) printf("%s:Error %d from %s \
    on line %d\n", __FILE__, GetLastError(), api, __LINE__);}
 void cls( HANDLE hConsole )
 {
    COORD coordScreen = { 0, 0 };    /* here's where we'll home the
                                        cursor */ 
    BOOL bSuccess;
    DWORD cCharsWritten;
    CONSOLE_SCREEN_BUFFER_INFO csbi; /* to get buffer info */ 
    DWORD dwConSize;                 /* number of character cells in
                                        the current buffer */ 

    /* get the number of character cells in the current buffer */ 

    bSuccess = GetConsoleScreenBufferInfo( hConsole, &csbi );
    PERR( bSuccess, "GetConsoleScreenBufferInfo" );
    dwConSize = csbi.dwSize.X * csbi.dwSize.Y;

    /* fill the entire screen with blanks */ 

    bSuccess = FillConsoleOutputCharacter( hConsole, (TCHAR) ' ',
       dwConSize, coordScreen, &cCharsWritten );
    PERR( bSuccess, "FillConsoleOutputCharacter" );

    /* get the current text attribute */ 

    bSuccess = GetConsoleScreenBufferInfo( hConsole, &csbi );
    PERR( bSuccess, "ConsoleScreenBufferInfo" );

    /* now set the buffer's attributes accordingly */ 

    bSuccess = FillConsoleOutputAttribute( hConsole, csbi.wAttributes,
       dwConSize, coordScreen, &cCharsWritten );
    PERR( bSuccess, "FillConsoleOutputAttribute" );

    /* put the cursor at (0, 0) */ 

    bSuccess = SetConsoleCursorPosition( hConsole, coordScreen );
    PERR( bSuccess, "SetConsoleCursorPosition" );
    return;
 }
#include <conio.h>
void ViewTNode ()
{
	int nodecount;
	int nodes[128];
	int parents[128];
	int selection;
	int linestart;
	nodes[0] = 0;
	parents[0] = -1;
	nodecount = 1;
	selection = 0;
	linestart = 0;
	HANDLE hConsole = CreateFile("CONOUT$", GENERIC_WRITE|GENERIC_READ, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, 0);
	while (1)
	{
		cls (hConsole);
		cprintf ("\nTNodes:\n");
		{
			int i;
			for (i=0; i<nodecount; i++)
				if (i>=linestart && i<linestart+30)
				{
					int j;
					if (i==selection)
						cprintf (">  ");
					else
						cprintf ("%3d", i);
					for (j=i; j=parents[j], j>=0;)
						cprintf ("|");
					if (nodes[i] == CONTENTS_SOLID)
						cprintf ("SOLID");
					else if (nodes[i] == CONTENTS_SKY)
						cprintf ("SKY");
					else if (nodes[i] < 0)
						cprintf ("EMPTY (%d)", nodes[i]);
					else
					{
						cprintf ("NODE %d", nodes[i]);
						tnode_t* tnode;
						tnode = &tnodes[nodes[i]];
						planetypes d[6] = {plane_x, plane_y, plane_z, plane_anyx, plane_anyy, plane_anyz};
						char s[6][16] = {"_X", "_Y", "_Z", "anyX", "anyY", "anyZ"};
						int k;
						for (k=0; k<6; k++)
							if (tnode->type == d[k])
							{
								cprintf (" %s", s[k]);
								break;
							}
						if (k == 6)
							cprintf (" type=%d", tnode->type);
						cprintf (" N,D=(%g,%g,%g),%g", tnode->normal[0], tnode->normal[1], tnode->normal[2], tnode->dist);
						cprintf (" C=(%d,%d)", tnode->children[0], tnode->children[1]);
						cprintf (" pad=%d", tnode->pad);
					}
					cprintf ("\n");
				}
			}
		int c;
		while (1)
		{
			switch (c=getch ())
			{
			case 'q':
				exit (0);
				break;
			case 224:
				break;
			case 72:
				cprintf ("KEY: UP\n");
				selection--;
				if (selection < 0)
					selection = 0;
				break;
			case 80:
				cprintf ("KEY: DOWN\n");
				selection++;
				if (selection > nodecount-1)
					selection = nodecount-1;
				break;
			case 73:
				cprintf ("KEY: PGUP\n");
				linestart -= 10;
				if (linestart < 0)
					linestart = 0;
				if (linestart >= nodecount)
					linestart = nodecount /10 *10;
				break;
			case 81:
				cprintf ("KEY: PGDOWN\n");
				linestart += 10;
				if (linestart < 0)
					linestart = 0;
				if (linestart >= nodecount)
					linestart = nodecount /10 *10;
				break;
			case 75:
				cprintf ("KEY: LEFT\n");
				{
					int i, j;
					for (i=0; i<nodecount; i++)
					{
						for (j=i; j=parents[j], j>=0;)
							if (j==selection)
								break;
						if (j>=0)
							break;
					}
					if (i>=nodecount)
					{
						if (parents[selection] >= 0)
							selection = parents[selection];
						break;
					}
				}
				{
					int map[128];
					int mcount = 0;
					int i;
					int j;
					for (i=0; i<nodecount; i++)
					{
						map[i] = mcount;
						for (j=i; j=parents[j], j>=0;)
							if (j==selection)
								map[i] = -1;
						if (map[i] >= 0)
							mcount++;
					}
					for (i=0; i<nodecount; i++)
					{
						if (map[i] >= 0)
						{
							nodes[map[i]] = nodes[i];
							if (parents[i] >= 0)
								parents[map[i]] = map[parents[i]];
						}
					}
					nodecount = mcount;
					selection = map[selection];
				}
				break;
			case 77:
				cprintf ("KEY: RIGHT\n");
				{
					if (nodes[selection] < 0)
						break;
					int j;
					for (j=0; j<nodecount; j++)
						if (parents[j] >= 0 && parents[j] == selection)
							break;
					if (j<nodecount)
						break;
					tnode_t* tnode;
					tnode = &tnodes[nodes[selection]];
					for (j=0; j<nodecount; j++)
						if (parents[j] >= 0 && parents[j] > selection)
							parents[j] += 2;
					for (j = nodecount-1; j > selection; j--)
					{
						nodes[j+2] = nodes[j];
						parents[j+2] = parents[j];
					}
					nodecount += 2;
					nodes[selection+1] = tnode->children[0];
					nodes[selection+2] = tnode->children[1];
					parents[selection+1] = selection;
					parents[selection+2] = selection;
				}
				break;
			default:
				cprintf ("KEY: U%d\n", c);
				continue;
			}
			break;
		}
	}
}
#endif
void            MakeTnodes(dmodel_t* /*bm*/)
{
    // 32 byte align the structs
    tnodes = (tnode_t*)calloc((g_numnodes + 1), sizeof(tnode_t));

	// The alignment doesn't have any effect at all. --vluzacn
	int ofs = 31 - (int)(((uintptr_t)tnodes + (uintptr_t)31) & (uintptr_t)31);
	tnodes = (tnode_t *)((byte *)tnodes + ofs);
    tnode_p = tnodes;

    MakeTnode(0);
#if 0 //debug. vluzacn
	ViewTNode ();
#endif
}

//==========================================================

static int             TestLine_r(const int node, const vec3_array& start, const vec3_array& stop
						   , int &linecontent
						   , vec3_array& skyhit
						   )
{
    tnode_t*        tnode;
    float           front, back;
    vec3_array          mid;
    float           frac;
    int             side;
    int             r;

	if (node < 0)
	{
		if (node == linecontent)
			return CONTENTS_EMPTY;
		if (node == CONTENTS_SOLID)
		{
			return CONTENTS_SOLID;
		}
		if (node == CONTENTS_SKY)
		{
			VectorCopy (start, skyhit);
			return CONTENTS_SKY;
		}
		if (linecontent)
		{
			return CONTENTS_SOLID;
		}
		linecontent = node;
		return CONTENTS_EMPTY;
	}

    tnode = &tnodes[node];
    switch (tnode->type)
    {
    case plane_x:
        front = start[0] - tnode->dist;
        back = stop[0] - tnode->dist;
        break;
    case plane_y:
        front = start[1] - tnode->dist;
        back = stop[1] - tnode->dist;
        break;
    case plane_z:
        front = start[2] - tnode->dist;
        back = stop[2] - tnode->dist;
        break;
    default:
        front = (start[0] * tnode->normal[0] + start[1] * tnode->normal[1] + start[2] * tnode->normal[2]) - tnode->dist;
        back = (stop[0] * tnode->normal[0] + stop[1] * tnode->normal[1] + stop[2] * tnode->normal[2]) - tnode->dist;
        break;
    }

	if (front > ON_EPSILON/2 && back > ON_EPSILON/2)
	{
		return TestLine_r(tnode->children[0], start, stop
			, linecontent
			, skyhit
			);
	}
	if (front < -ON_EPSILON/2 && back < -ON_EPSILON/2)
	{
		return TestLine_r(tnode->children[1], start, stop
			, linecontent
			, skyhit
			);
	}
	if (fabs(front) <= ON_EPSILON && fabs(back) <= ON_EPSILON)
	{
		int r1 = TestLine_r(tnode->children[0], start, stop
			, linecontent
			, skyhit
			);
		if (r1 == CONTENTS_SOLID)
			return CONTENTS_SOLID;
		int r2 = TestLine_r(tnode->children[1], start, stop
			, linecontent
			, skyhit
			);
		if (r2 == CONTENTS_SOLID)
			return CONTENTS_SOLID;
		if (r1 == CONTENTS_SKY || r2 == CONTENTS_SKY)
			return CONTENTS_SKY;
		return CONTENTS_EMPTY;
	}
	side = (front - back) < 0;
	frac = front / (front - back);
	if (frac < 0) frac = 0;
	if (frac > 1) frac = 1;
	mid[0] = start[0] + (stop[0] - start[0]) * frac;
	mid[1] = start[1] + (stop[1] - start[1]) * frac;
	mid[2] = start[2] + (stop[2] - start[2]) * frac;
	r = TestLine_r(tnode->children[side], start, mid
		, linecontent
		, skyhit
		);
	if (r != CONTENTS_EMPTY)
		return r;
	return TestLine_r(tnode->children[!side], mid, stop
		, linecontent
		, skyhit
		);
}

int             TestLine(const vec3_array& start, const vec3_array& stop
						 , vec3_array& skyhit
						 )
{
	int linecontent = 0;
    return TestLine_r(0, start, stop
		, linecontent
		, skyhit
		);
}


typedef struct
{
	Winding *winding;
	dplane_t plane;
	int numedges;
	dplane_t *edges;
	int texinfo;
	bool tex_alphatest;
	vec_t tex_vecs[2][4];
	int tex_width;
	int tex_height;
	std::uint8_t *tex_canvas;
} opaqueface_t;
opaqueface_t *opaquefaces;

typedef struct opaquenode_s
{
	planetypes type;
	vec3_array normal;
	vec_t dist;
	int children[2];
	int firstface;
	int numfaces;
} opaquenode_t;
opaquenode_t *opaquenodes;

opaquemodel_t *opaquemodels;

bool TryMerge (opaqueface_t *f, const opaqueface_t *f2)
{
	if (!f->winding || !f2->winding)
	{
		return false;
	}
	if (fabs (f2->plane.dist - f->plane.dist) > ON_EPSILON
		|| fabs (f2->plane.normal[0] - f->plane.normal[0]) > NORMAL_EPSILON
		|| fabs (f2->plane.normal[1] - f->plane.normal[1]) > NORMAL_EPSILON
		|| fabs (f2->plane.normal[2] - f->plane.normal[2]) > NORMAL_EPSILON
		)
	{
		return false;
	}
	if ((f->tex_alphatest || f2->tex_alphatest) && f->texinfo != f2->texinfo)
	{
		return false;
	}

	Winding *w = f->winding;
	const Winding *w2 = f2->winding;
	const vec_t *pA, *pB, *pC, *pD, *p2A, *p2B, *p2C, *p2D;
	int i, i2;

	for (i = 0; i < w->size(); i++)
	{
		for (i2 = 0; i2 < w2->size(); i2++)
		{
			pA = w->m_Points[(i+w->size()-1)%w->size()].data();
			pB = w->m_Points[i].data();
			pC = w->m_Points[(i+1)%w->size()].data();
			pD = w->m_Points[(i+2)%w->size()].data();
			p2A = w2->m_Points[(i2+w2->size()-1)%w2->size()].data();
			p2B = w2->m_Points[i2].data();
			p2C = w2->m_Points[(i2+1)%w2->size()].data();
			p2D = w2->m_Points[(i2+2)%w2->size()].data();
			if (!VectorCompare (pB, p2C) || !VectorCompare (pC, p2B))
			{
				continue;
			}
			break;
		}
		if (i2 == w2->size())
		{
			continue;
		}
		break;
	}
	if (i == w->size())
	{
		return false;
	}

	const vec_t *normal = f->plane.normal.data();
	vec3_array e1, e2;
	dplane_t pl1, pl2;
	int side1, side2;

	VectorSubtract (p2D, pA, e1);
	CrossProduct (normal, e1, pl1.normal); // pointing outward
	if (VectorNormalize (pl1.normal) == 0.0)
	{
		Developer (DEVELOPER_LEVEL_WARNING, "Warning: TryMerge: Empty edge.\n");
		return false;
	}
	pl1.dist = DotProduct (pA, pl1.normal);
	if (DotProduct (pB, pl1.normal) - pl1.dist < -ON_EPSILON)
	{
		return false;
	}
	side1 = (DotProduct (pB, pl1.normal) - pl1.dist > ON_EPSILON)? 1: 0;

	VectorSubtract (pD, p2A, e2);
	CrossProduct (normal, e2, pl2.normal); // pointing outward
	if (VectorNormalize (pl2.normal) == 0.0)
	{
		Developer (DEVELOPER_LEVEL_WARNING, "Warning: TryMerge: Empty edge.\n");
		return false;
	}
	pl2.dist = DotProduct (p2A, pl2.normal);
	if (DotProduct (p2B, pl2.normal) - pl2.dist < -ON_EPSILON)
	{
		return false;
	}
	side2 = (DotProduct (p2B, pl2.normal) - pl2.dist > ON_EPSILON)? 1: 0;

	Winding *neww = new Winding (w->size() + w2->size() - 4 + side1 + side2);
	int j, k;
	k = 0;
	for (j = (i + 2) % w->size(); j != i; j = (j + 1) % w->size())
	{
		VectorCopy (w->m_Points[j], neww->m_Points[k]);
		k++;
	}
	if (side1)
	{
		VectorCopy (w->m_Points[j], neww->m_Points[k]);
		k++;
	}
	for (j = (i2 + 2) % w2->size(); j != i2; j = (j + 1) % w2->size())
	{
		VectorCopy (w2->m_Points[j], neww->m_Points[k]);
		k++;
	}
	if (side2)
	{
		VectorCopy (w2->m_Points[j], neww->m_Points[k]);
		k++;
	}
	neww->RemoveColinearPoints ();
	if (neww->size() < 3)
	{
		Developer (DEVELOPER_LEVEL_WARNING, "Warning: TryMerge: Empty winding.\n");
		delete neww;
		neww = nullptr;
	}
	delete f->winding;
	f->winding = neww;
	return true;
}

int MergeOpaqueFaces (int firstface, int numfaces)
{
	int i, j, newnum;
	opaqueface_t *faces = &opaquefaces[firstface];
	for (i = 0; i < numfaces; i++)
	{
		for (j = 0; j < i; j++)
		{
			if (TryMerge (&faces[i], &faces[j]))
			{
				delete faces[j].winding;
				faces[j].winding = nullptr;
				j = -1;
				continue;
			}
		}
	}
	for (i = 0, j = 0; i < numfaces; i++)
	{
		if (faces[i].winding)
		{
			faces[j] = faces[i];
			j++;
		}
	}
	newnum = j;
	for (; j < numfaces; j++)
	{
		memset (&faces[j], 0, sizeof(opaqueface_t));
	}
	return newnum;
}

void BuildFaceEdges (opaqueface_t *f)
{
	if (!f->winding)
		return;
	f->numedges = f->winding->size();
	f->edges = (dplane_t *)calloc (f->numedges, sizeof (dplane_t));
	const vec_t *p1, *p2;
	const vec_t *n = f->plane.normal.data();
	vec3_array e;
	dplane_t *pl;
	int x;
	for (x = 0; x < f->winding->size(); x++)
	{
		p1 = f->winding->m_Points[x].data();
		p2 = f->winding->m_Points[(x+1)%f->winding->size()].data();
		pl = &f->edges[x];
		VectorSubtract (p2, p1, e);
		CrossProduct (n, e, pl->normal);
		if (VectorNormalize (pl->normal) == 0.0)
		{
			Developer (DEVELOPER_LEVEL_WARNING, "Warning: BuildFaceEdges: Empty edge.\n");
			VectorClear (pl->normal);
			pl->dist = -1;
			continue;
		}
		pl->dist = DotProduct (pl->normal, p1);
	}
}

void CreateOpaqueNodes ()
{
	int i, j;
	opaquemodels = (opaquemodel_t *)calloc (g_nummodels, sizeof (opaquemodel_t));
	opaquenodes = (opaquenode_t *)calloc (g_numnodes, sizeof (opaquenode_t));
	opaquefaces = (opaqueface_t *)calloc (g_numfaces, sizeof (opaqueface_t));
	for (i = 0; i < g_numfaces; i++)
	{
		opaqueface_t *of = &opaquefaces[i];
		dface_t *df = &g_dfaces[i];
		of->winding = new Winding (*df);
		if (of->winding->size() < 3)
		{
			delete of->winding;
			of->winding = nullptr;
		}
		of->plane = g_dplanes[df->planenum];
		if (df->side)
		{
			VectorInverse (of->plane.normal);
			of->plane.dist = -of->plane.dist;
		}
		of->texinfo = df->texinfo;
		texinfo_t *info = &g_texinfo[of->texinfo];
		for (j = 0; j < 2; j++)
		{
			for (int k = 0; k < 4; k++)
			{
				of->tex_vecs[j][k] = info->vecs[j][k];
			}
		}
		radtexture_t *tex = &g_textures[info->miptex];
		of->tex_alphatest = tex->name[0] == '{';
		of->tex_width = tex->width;
		of->tex_height = tex->height;
		of->tex_canvas = tex->canvas;
	}
	for (i = 0; i < g_numnodes; i++)
	{
		opaquenode_t *on = &opaquenodes[i];
		dnode_t *dn = &g_dnodes[i];
		on->type = g_dplanes[dn->planenum].type;
		VectorCopy (g_dplanes[dn->planenum].normal, on->normal);
		on->dist = g_dplanes[dn->planenum].dist;
		on->children[0] = dn->children[0];
		on->children[1] = dn->children[1];
		on->firstface = dn->firstface;
		on->numfaces = dn->numfaces;
		on->numfaces = MergeOpaqueFaces (on->firstface, on->numfaces);
	}
	for (i = 0; i < g_numfaces; i++)
	{
		BuildFaceEdges (&opaquefaces[i]);
	}
	for (i = 0; i < g_nummodels; i++)
	{
		opaquemodel_t *om = &opaquemodels[i];
		dmodel_t *dm = &g_dmodels[i];
		om->headnode = dm->headnode[0];
		for (j = 0; j < 3; j++)
		{
			om->mins[j] = dm->mins[j] - 1;
			om->maxs[j] = dm->maxs[j] + 1;
		}
	}
}

void DeleteOpaqueNodes ()
{
	int i;
	for (i = 0; i < g_numfaces; i++)
	{
		opaqueface_t *of = &opaquefaces[i];
		if (of->winding)
			delete of->winding;
		if (of->edges)
			free (of->edges);
	}
	free (opaquefaces);
	free (opaquenodes);
	free (opaquemodels);
}

static int TestLineOpaque_face (int facenum, const vec3_array& hit)
{
	opaqueface_t *thisface = &opaquefaces[facenum];
	int x;
	if (thisface->numedges == 0)
	{
		Developer (DEVELOPER_LEVEL_WARNING, "Warning: TestLineOpaque: Empty face.\n");
		return 0;
	}
	for (x = 0; x < thisface->numedges; x++)
	{
		if (DotProduct (hit, thisface->edges[x].normal) - thisface->edges[x].dist > ON_EPSILON)
		{
			return 0;
		}
	}
	if (thisface->tex_alphatest)
	{
		double x, y;
		x = DotProduct (hit, thisface->tex_vecs[0]) + thisface->tex_vecs[0][3];
		y = DotProduct (hit, thisface->tex_vecs[1]) + thisface->tex_vecs[1][3];
		x = floor (x - thisface->tex_width * floor (x / thisface->tex_width));
		y = floor (y - thisface->tex_height * floor (y / thisface->tex_height));
		x = x > thisface->tex_width - 1? thisface->tex_width - 1: x < 0? 0: x;
		y = y > thisface->tex_height - 1? thisface->tex_height - 1: y < 0? 0: y;
		if (thisface->tex_canvas[(int)y * thisface->tex_width + (int)x] == 0xFF)
		{
			return 0;
		}
	}
	return 1;
}

static int TestLineOpaque_r (int nodenum, const vec3_array& start, const vec3_array& stop)
{
	opaquenode_t *thisnode;
	vec_t front, back;
	if (nodenum < 0)
	{
		return 0;
	}
	thisnode = &opaquenodes[nodenum];
	switch (thisnode->type)
	{
	case plane_x:
		front = start[0] - thisnode->dist;
		back = stop[0] - thisnode->dist;
		break;
	case plane_y:
		front = start[1] - thisnode->dist;
		back = stop[1] - thisnode->dist;
		break;
	case plane_z:
		front = start[2] - thisnode->dist;
		back = stop[2] - thisnode->dist;
		break;
	default:
		front = DotProduct (start, thisnode->normal) - thisnode->dist;
		back = DotProduct (stop, thisnode->normal) - thisnode->dist;
	}
	if (front > ON_EPSILON / 2 && back > ON_EPSILON / 2)
	{
		return TestLineOpaque_r (thisnode->children[0], start, stop);
	}
	if (front < -ON_EPSILON / 2 && back < -ON_EPSILON / 2)
	{
		return TestLineOpaque_r (thisnode->children[1], start, stop);
	}
	if (fabs (front) <= ON_EPSILON && fabs (back) <= ON_EPSILON)
	{
		return TestLineOpaque_r (thisnode->children[0], start, stop)
			|| TestLineOpaque_r (thisnode->children[1], start, stop);
	}
	{
		int side;
		vec_t frac;
		vec3_array mid;
		int facenum;
		side = (front - back) < 0;
		frac = front / (front - back);
		if (frac < 0) frac = 0;
		if (frac > 1) frac = 1;
		mid[0] = start[0] + (stop[0] - start[0]) * frac;
		mid[1] = start[1] + (stop[1] - start[1]) * frac;
		mid[2] = start[2] + (stop[2] - start[2]) * frac;
		for (facenum = thisnode->firstface; facenum < thisnode->firstface + thisnode->numfaces; facenum++)
		{
			if (TestLineOpaque_face (facenum, mid))
			{
				return 1;
			}
		}
		return TestLineOpaque_r (thisnode->children[side], start, mid)
			|| TestLineOpaque_r (thisnode->children[!side], mid, stop);
	}
}

int TestLineOpaque (int modelnum, const vec3_array& modelorigin, const vec3_array& start, const vec3_array& stop)
{
	opaquemodel_t *thismodel = &opaquemodels[modelnum];
	vec_t front, back, frac;
	vec3_array p1, p2;
	VectorSubtract (start, modelorigin, p1);
	VectorSubtract (stop, modelorigin, p2);
	int axial;
	for (axial = 0; axial < 3; axial++)
	{
		front = p1[axial] - thismodel->maxs[axial];
		back = p2[axial] - thismodel->maxs[axial];
		if (front >= -ON_EPSILON && back >= -ON_EPSILON)
		{
			return 0;
		}
		if (front > ON_EPSILON || back > ON_EPSILON)
		{
			frac = front / (front - back);
			if (front > back)
			{
				p1[0] = p1[0] + (p2[0] - p1[0]) * frac;
				p1[1] = p1[1] + (p2[1] - p1[1]) * frac;
				p1[2] = p1[2] + (p2[2] - p1[2]) * frac;
			}
			else
			{
				p2[0] = p1[0] + (p2[0] - p1[0]) * frac;
				p2[1] = p1[1] + (p2[1] - p1[1]) * frac;
				p2[2] = p1[2] + (p2[2] - p1[2]) * frac;
			}
		}
		front = thismodel->mins[axial] - p1[axial];
		back = thismodel->mins[axial] - p2[axial];
		if (front >= -ON_EPSILON && back >= -ON_EPSILON)
		{
			return 0;
		}
		if (front > ON_EPSILON || back > ON_EPSILON)
		{
			frac = front / (front - back);
			if (front > back)
			{
				p1[0] = p1[0] + (p2[0] - p1[0]) * frac;
				p1[1] = p1[1] + (p2[1] - p1[1]) * frac;
				p1[2] = p1[2] + (p2[2] - p1[2]) * frac;
			}
			else
			{
				p2[0] = p1[0] + (p2[0] - p1[0]) * frac;
				p2[1] = p1[1] + (p2[1] - p1[1]) * frac;
				p2[2] = p1[2] + (p2[2] - p1[2]) * frac;
			}
		}
	}
	return TestLineOpaque_r (thismodel->headnode, p1, p2);
}

int CountOpaqueFaces_r (opaquenode_t *node)
{
	int count;
	count = node->numfaces;
	if (node->children[0] >= 0)
	{
		count += CountOpaqueFaces_r (&opaquenodes[node->children[0]]);
	}
	if (node->children[1] >= 0)
	{
		count += CountOpaqueFaces_r (&opaquenodes[node->children[1]]);
	}
	return count;
}

int CountOpaqueFaces (int modelnum)
{
	return CountOpaqueFaces_r (&opaquenodes[opaquemodels[modelnum].headnode]);
}

int TestPointOpaque_r (int nodenum, bool solid, const vec3_array& point)
{
	opaquenode_t *thisnode;
	vec_t dist;
	while (1)
	{
		if (nodenum < 0)
		{
			if (solid && g_dleafs[-nodenum-1].contents == CONTENTS_SOLID)
				return 1;
			else
				return 0;
		}
		thisnode = &opaquenodes[nodenum];
		switch (thisnode->type)
		{
		case plane_x:
			dist = point[0] - thisnode->dist;
			break;
		case plane_y:
			dist = point[1] - thisnode->dist;
			break;
		case plane_z:
			dist = point[2] - thisnode->dist;
			break;
		default:
			dist = DotProduct (point, thisnode->normal) - thisnode->dist;
		}
		if (dist > HUNT_WALL_EPSILON)
		{
			nodenum = thisnode->children[0];
		}
		else if (dist < -HUNT_WALL_EPSILON)
		{
			nodenum = thisnode->children[1];
		}
		else
		{
			break;
		}
	}
	{
		int facenum;
		for (facenum = thisnode->firstface; facenum < thisnode->firstface + thisnode->numfaces; facenum++)
		{
			if (TestLineOpaque_face (facenum, point))
			{
				return 1;
			}
		}
	}
	return TestPointOpaque_r (thisnode->children[0], solid, point)
		|| TestPointOpaque_r (thisnode->children[1], solid, point);
}

int TestPointOpaque (int modelnum, const vec3_array& modelorigin, bool solid, const vec3_array& point)
{
	opaquemodel_t *thismodel = &opaquemodels[modelnum];
	vec3_array newpoint;
	VectorSubtract (point, modelorigin, newpoint);
	int axial;
	for (axial = 0; axial < 3; axial++)
	{
		if (newpoint[axial] > thismodel->maxs[axial])
			return 0;
		if (newpoint[axial] < thismodel->mins[axial])
			return 0;
	}
	return TestPointOpaque_r (thismodel->headnode, solid, newpoint);
}