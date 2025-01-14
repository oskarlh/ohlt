/*
 
    CONSTRUCTIVE SOLID GEOMETRY    -aka-    C S G 

    Code based on original code from Valve Software, 
    Modified by Sean "Zoner" Cavanaugh (seanc@gearboxsoftware.com) with permission.
    Modified by Tony "Merl" Moore (merlinis@bigpond.net.au) [AJM]
    Modified by amckern (amckern@yahoo.com)
    Modified by vluzacn (vluzacn@163.com)
    Modified by seedee (cdaniel9000@gmail.com)
    Modified by Oskar Larsson Högfeldt (AKA Oskar Potatis) (oskar@oskar.pm)

*/

#include "csg.h" 
#include "bsp_file_sizes.h"
#ifdef SYSTEM_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h> //--vluzacn
#endif
#include "cli_option_defaults.h"
#include "filelib.h"
#include <numbers>
#include "utf8.h"

#include <string_view>
#include <utility>
using namespace std::literals;

/*

 NOTES

 - check map size for +/- 4k limit at load time
 - allow for multiple wad.cfg configurations per compile

*/

static FILE*    out[NUM_HULLS]; // pointer to each of the hull out files (.p0, .p1, ect.)  
static FILE*    out_view[NUM_HULLS];
static FILE*    out_detailbrush[NUM_HULLS];
static int      c_tiny;        
static int      c_tiny_clip;
static int      c_outfaces;
static int      c_csgfaces;
bounding_box     world_bounds;

hull_sizes g_hull_size{standard_hull_sizes};

vec_t           g_tiny_threshold = DEFAULT_TINY_THRESHOLD;
     
bool            g_noclip = DEFAULT_NOCLIP;              // no clipping hull "-noclip"
bool            g_onlyents = DEFAULT_ONLYENTS;          // onlyents mode "-onlyents"
bool            g_wadtextures = DEFAULT_WADTEXTURES;    // "-nowadtextures"
bool            g_chart = cli_option_defaults::chart;                // show chart "-chart"
bool            g_skyclip = DEFAULT_SKYCLIP;            // no sky clipping "-noskyclip"
bool            g_estimate = cli_option_defaults::estimate;          // progress estimates "-estimate"
bool            g_info = cli_option_defaults::info;                  // "-info" ?
const char*     g_hullfile = nullptr;                      // external hullfile "-hullfie sdfsd"
const char*		g_wadcfgfile = nullptr;
const char*		g_wadconfigname = nullptr;

bool            g_bUseNullTex = cli_option_defaults::nulltex;        // "-nonulltex"

cliptype		g_cliptype = DEFAULT_CLIPTYPE;			// "-cliptype <value>"

const char*			g_nullfile = nullptr;

bool            g_bClipNazi = DEFAULT_CLIPNAZI;         // "-noclipeconomy"

bool            g_bWadAutoDetect = DEFAULT_WADAUTODETECT; // "-nowadautodetect"


vec_t g_scalesize = DEFAULT_SCALESIZE;
bool g_resetlog = DEFAULT_RESETLOG;
bool g_nolightopt = DEFAULT_NOLIGHTOPT;
#ifdef HLCSG_GAMETEXTMESSAGE_UTF8
bool g_noutf8 = DEFAULT_NOUTF8;
#endif
bool g_nullifytrigger = DEFAULT_NULLIFYTRIGGER;
bool g_viewsurface = false;

// =====================================================================================
//  GetParamsFromEnt
//      parses entity keyvalues for setting information
// =====================================================================================
void            GetParamsFromEnt(entity_t* mapent)
{
    char    szTmp[256];

    Log("\nCompile Settings detected from info_compile_parameters entity\n");

    // verbose(choices) : "Verbose compile messages" : 0 = [ 0 : "Off" 1 : "On" ]
    int verboseValue = IntForKey(mapent, u8"verbose");
    if (verboseValue == 1)
    {
        g_verbose = true;
    }
    else if (verboseValue == 0)
    {
        g_verbose = false;
    }
    Log("%30s [ %-9s ]\n", "Compile Option", "setting");
    Log("%30s [ %-9s ]\n", "Verbose Compile Messages", g_verbose ? "on" : "off");

    // estimate(choices) :"Estimate Compile Times?" : 0 = [ 0: "Yes" 1: "No" ]
    if (IntForKey(mapent, u8"estimate")) 
    {
        g_estimate = true;
    }
    else
    {
        g_estimate = false;
    }
    Log("%30s [ %-9s ]\n", "Estimate Compile Times", g_estimate ? "on" : "off");

	// priority(choices) : "Priority Level" : 0 = [	0 : "Normal" 1 : "High"	-1 : "Low" ]
	if (!strcmp((const char*) ValueForKey(mapent, u8"priority"), "1"))
    {
        g_threadpriority = q_threadpriority::eThreadPriorityHigh;
        Log("%30s [ %-9s ]\n", "Thread Priority", "high");
    }
    else if (!strcmp((const char*) ValueForKey(mapent, u8"priority"), "-1"))
    {
        g_threadpriority = q_threadpriority::eThreadPriorityLow;
        Log("%30s [ %-9s ]\n", "Thread Priority", "low");
    }

    // texdata(string) : "Texture Data Memory" : "4096"
    int texdataValue = IntForKey(mapent, u8"texdata") * 1024;
    if (texdataValue > g_max_map_miptex)
    {
        g_max_map_miptex = texdataValue;
    }
	snprintf(szTmp, sizeof(szTmp), "%td", g_max_map_miptex);
    Log("%30s [ %-9s ]\n", "Texture Data Memory", szTmp);

    // hullfile(string) : "Custom Hullfile"
    if (*ValueForKey(mapent, u8"hullfile"))
    {
        g_hullfile = (const char*) ValueForKey(mapent, u8"hullfile");
        Log("%30s [ %-9s ]\n", "Custom Hullfile", g_hullfile);
    }

    // wadautodetect(choices) : "Wad Auto Detect" : 0 =	[ 0 : "Off" 1 : "On" ]
    /*if (!strcmp(ValueForKey(mapent, "wadautodetect"), "1"))
    { 
        g_bWadAutoDetect = true;
    }
    else
    {
        g_bWadAutoDetect = false;
    }*/
    const char* wadautodetectValue = (const char*) ValueForKey(mapent, u8"wadautodetect"); //seedee
    g_bWadAutoDetect = (wadautodetectValue && atoi(wadautodetectValue) >= 1);
    Log("%30s [ %-9s ]\n", "Wad Auto Detect", g_bWadAutoDetect ? "on" : "off");
	
	// wadconfig(string) : "Custom Wad Configuration" : ""
    if (*ValueForKey(mapent, u8"wadconfig"))
    {
        g_wadconfigname = c_strdup((const char*) ValueForKey(mapent, u8"wadconfig"));
        Log("%30s [ %-9s ]\n", "Custom Wad Configuration Name", g_wadconfigname);
    }
	// wadcfgfile(string) : "Custom Wad Configuration File" : ""
    if (*ValueForKey(mapent, u8"wadcfgfile"))
    {
        g_wadcfgfile = c_strdup((const char*) ValueForKey(mapent, u8"wadcfgfile"));
        Log("%30s [ %-9s ]\n", "Custom Wad Configuration File", g_wadcfgfile);
    }

    // noclipeconomy(choices) : "Strip Uneeded Clipnodes?" : 1 = [ 1 : "Yes" 0 : "No" ]
    const int noclipeconomyValue = IntForKey(mapent, u8"noclipeconomy");
    if (noclipeconomyValue == 1)
    {
        g_bClipNazi = true;
    }
    else if (noclipeconomyValue == 0)
    {
        g_bClipNazi = false;
    }        
    Log("%30s [ %-9s ]\n", "Clipnode Economy Mode", g_bClipNazi ? "on" : "off");

    /*
    hlcsg(choices) : "HLCSG" : 1 =
    [
        1 : "Normal"
        2 : "Onlyents"
        0 : "Off"
    ]
    */
    const int hlcsgValue = IntForKey(mapent, u8"hlcsg");
    g_onlyents = false;
    if (hlcsgValue == 2)
    {
        g_onlyents = true;
    }
    else if (hlcsgValue == 0)
    {
        Fatal(assume_TOOL_CANCEL, 
            "%s was set to \"Off\" (0) in info_compile_parameters entity, execution cancelled", g_Program);
        CheckFatal();  
    }
    Log("%30s [ %-9s ]\n", "Onlyents", g_onlyents ? "on" : "off");

    /*
    nocliphull(choices) : "Generate clipping hulls" : 0 =
    [
        0 : "Yes"
        1 : "No"
    ]
    */
    if (IntForKey(mapent, u8"nocliphull") == 1)
    {
        g_noclip = true;
    }
    else 
    {
        g_noclip = false;
    }
    Log("%30s [ %-9s ]\n", "Clipping Hull Generation", g_noclip ? "off" : "on");
    // cliptype(choices) : "Clip Hull Type" : 4 = [ 0 : "Smallest" 1 : "Normalized" 2: "Simple" 3 : "Precise" 4 : "Legacy" ]

	switch(IntForKey(mapent, u8"cliptype"))
	{
	case 0:
		g_cliptype = clip_smallest;
		break;
	case 1:
		g_cliptype = clip_normalized;
		break;
	case 2:
		g_cliptype = clip_simple;
		break;
	case 3:
		g_cliptype = clip_precise;
		break;
	default:
		g_cliptype = clip_legacy;
		break;
	}
    Log("%30s [ %-9s ]\n", "Clip Hull Type", GetClipTypeString(g_cliptype));
    /*
    noskyclip(choices) : "No Sky Clip" : 0 =
    [
        1 : "On"
        0 : "Off"
    ]
    */
    if (IntForKey(mapent, u8"noskyclip") == 1)
    {
        g_skyclip = false;
    }
    else 
    {
        g_skyclip = true;
    }
    Log("%30s [ %-9s ]\n", "Sky brush clip generation", g_skyclip ? "on" : "off");

    ///////////////
    Log("\n");
}


// =====================================================================================
//  NewFaceFromFace
//      Duplicates the non point information of a face, used by SplitFace
// =====================================================================================
bface_t NewFaceFromFace(const bface_t& in)
{
    bface_t newf{};
    newf.contents = in.contents;
    newf.texinfo = in.texinfo;
    newf.planenum = in.planenum;
    newf.plane = in.plane;
	newf.backcontents = in.backcontents;

    return newf;
}

// =====================================================================================
//  WriteFace
// =====================================================================================
void            WriteFace(const int hull, const bface_t* const f
						  , int detaillevel
						  )
{
    unsigned int    i;

    ThreadLock();
    if (!hull)
        c_csgfaces++;

    // .p0 format
    const Winding& w = f->w;

    // plane summary
	fprintf (out[hull], "%i %i %i %i %zu\n", detaillevel, f->planenum, f->texinfo, f->contents, w.size());

    // for each of the points on the face
    for (i = 0; i < w.size(); i++)
    {
        // write the co-ords
        fprintf(out[hull], "%5.8f %5.8f %5.8f\n", w.m_Points[i][0], w.m_Points[i][1], w.m_Points[i][2]);
    }

    // put in an extra line break
    fprintf(out[hull], "\n");
	if (g_viewsurface)
	{
		static bool side = false;
		side = !side;
		if (side)
		{
			vec3_array center = w.getCenter ();
			vec3_array center2;
			VectorAdd (center, f->plane->normal, center2);
			fprintf (out_view[hull], "%5.2f %5.2f %5.2f\n", center2[0], center2[1], center2[2]);
			for (i = 0; i < w.size(); i++)
			{
				const vec3_array& p1{w.m_Points[i]};
                const vec3_array& p2{w.m_Points[(i+1) % w.size()]};

				fprintf (out_view[hull], "%5.2f %5.2f %5.2f\n", center[0], center[1], center[2]);
				fprintf (out_view[hull], "%5.2f %5.2f %5.2f\n", p1[0], p1[1], p1[2]);
				fprintf (out_view[hull], "%5.2f %5.2f %5.2f\n", p2[0], p2[1], p2[2]);
			}
			fprintf (out_view[hull], "%5.2f %5.2f %5.2f\n", center[0], center[1], center[2]);
			fprintf (out_view[hull], "%5.2f %5.2f %5.2f\n", center2[0], center2[1], center2[2]);
		}
	}

    ThreadUnlock();
}
static void WriteDetailBrush (int hull, const std::vector<bface_t>& faces)
{
	ThreadLock ();
	fprintf (out_detailbrush[hull], "0\n");
	for (const bface_t &f : faces)
	{
		const Winding& w{f.w};
		fprintf (out_detailbrush[hull], "%i %zu\n", f.planenum, w.size());
		for (int i = 0; i < w.size(); i++)
		{
			fprintf (out_detailbrush[hull], "%5.8f %5.8f %5.8f\n", w.m_Points[i][0], w.m_Points[i][1], w.m_Points[i][2]);
		}
	}
	fprintf (out_detailbrush[hull], "-1 -1\n");
	ThreadUnlock ();
}

// =====================================================================================
//  SaveOutside
//      The faces remaining on the outside list are final polygons.  Write them to the 
//      output file.
//      Passable contents (water, lava, etc) will generate a mirrored copy of the face 
//      to be seen from the inside.
// =====================================================================================
static void SaveOutside(brush_t& b, const int hull, std::vector<bface_t>& outside, const int mirrorcontents)
{
    for (bface_t& f : outside)
    {

		int frontcontents, backcontents;
		int texinfo = f.texinfo;
		const wad_texture_name texname{
            GetTextureByNumber_CSG(texinfo).value_or(wad_texture_name{})
        };
		frontcontents = f.contents;
		if (mirrorcontents == CONTENTS_TOEMPTY)
		{
			backcontents = f.backcontents;
		}
		else
		{
			backcontents = mirrorcontents;
		}
		if (frontcontents == CONTENTS_TOEMPTY)
		{
			frontcontents = CONTENTS_EMPTY;
		}
		if (backcontents == CONTENTS_TOEMPTY)
		{
			backcontents = CONTENTS_EMPTY;
		}

        bool backnull = false;
		bool frontnull = false;
		if (mirrorcontents == CONTENTS_TOEMPTY)
		{
            // SKIP and HINT are special textures for hlbsp so they should be kept
            const bool specialTextureForHlbsp = texname.is_skip() || texname.is_any_hint();
            backnull = !specialTextureForHlbsp;
		}
        if (texname.marks_discardable_faces() && frontcontents != backcontents) {
            // NOT actually discardable, so remove BEVELHINT/SOLIDHINT texture name and behave like NULL
            frontnull = backnull = true;
		}
		if (b.entitynum != 0 && texname.is_any_liquid()) {
			backnull = true; // strip water face on one side
		}

		f.contents = frontcontents;
		f.texinfo = frontnull? -1: texinfo;
        if (f.w.getArea() < g_tiny_threshold) {
            c_tiny++;
            Verbose("Entity %i, Brush %i: tiny fragment\n", 
				b.originalentitynum, b.originalbrushnum
				);
            continue;
        }

        // count unique faces
        if (!hull) {
            for (bface_t& f2 : b.hulls[hull].faces) {
                if (f2.planenum == f.planenum) {
                    if (!f2.used) {
                        f2.used = true;
                        c_outfaces++;
                    }
                    break;
                }
            }
        }

		// check the texture alignment of this face
		if (!hull)
		{
			int texinfo = f.texinfo;
			const wad_texture_name texname{
                GetTextureByNumber_CSG (texinfo).value_or(wad_texture_name{})
            };
			texinfo_t *tex = &g_texinfo[texinfo];

            if (texinfo != -1 // nullified textures (nullptr, BEVEL, aaatrigger, etc.)
                && !(tex->flags & TEX_SPECIAL) // sky
                && !texname.is_skip()
                && !texname.is_any_hint() // HINT and SKIP will be nullified only after hlbsp
                )
			{
				// check for "Malformed face (%d) normal"
				vec3_array texnormal;
				CrossProduct (tex->vecs[1], tex->vecs[0], texnormal);
				normalize_vector(texnormal);
				if (fabs (DotProduct (texnormal, f.plane->normal)) <= NORMAL_EPSILON)
				{
					Warning ("Entity %i, Brush %i: Malformed texture alignment (texture %s): Texture axis perpendicular to face.",
						b.originalentitynum, b.originalbrushnum,
						texname.c_str()
						);
				}

				// check for "Bad surface extents"
				bool bad;
				vec_t val;
				
				bad = false;
				for (int i = 0; i < f.w.size(); ++i)
				{
					for (int j = 0; j < 2; ++j)
					{
						val = DotProduct (f.w.m_Points[i], tex->vecs[j]) + tex->vecs[j][3];
						if (val < -99999 || val > 999999)
						{
							bad = true;
						}
					}
				}
				if (bad)
				{
					Warning ("Entity %i, Brush %i: Malformed texture alignment (texture %s): Bad surface extents.",
						b.originalentitynum, b.originalbrushnum,
						texname.c_str()
						);
				}
			}
		}

        WriteFace(hull, &f
			, 
			(hull? b.clipnodedetaillevel: b.detaillevel)
			);

        //              if (mirrorcontents != CONTENTS_SOLID)
        {
            f.planenum ^= 1;
            f.plane = &g_mapplanes[f.planenum];
			f.contents = backcontents;
			f.texinfo = backnull? -1: texinfo;

            // swap point orders
            for (std::size_t i = 0; i < f.w.size() / 2; ++i) // Add points backwards
            {
                using std::swap;
                swap(f.w.m_Points[i], f.w.m_Points[f.w.size() - 1 - i]);
            }
            WriteFace(hull, &f
				, 
				(hull? b.clipnodedetaillevel: b.detaillevel)
				);
        }
    }
    outside.clear();
}

// =====================================================================================
//  CopyFace
// =====================================================================================
bface_t CopyFace(const bface_t& f)
{
    bface_t n{NewFaceFromFace(f)};
    n.w = f.w;
    n.bounds = f.bounds;
    return n;
}

#include <ranges>
// =====================================================================================
//  CopyFaceList
// =====================================================================================
std::vector<bface_t> CopyFaceList(const std::vector<bface_t>& faceList)
{
    std::vector<bface_t> out;
    out.reserve(faceList.size());
    std::ranges::copy(std::views::transform(faceList, CopyFace), std::back_inserter(out));
    return out;
}

// =====================================================================================
//  CopyFacesToOutside
//      Make a copy of all the faces of the brush, so they can be chewed up by other 
//      brushes.
//      All of the faces start on the outside list.
//      As other brushes take bites out of the faces, the fragments are moved to the 
//      inside list, so they can be freed when they are determined to be completely 
//      enclosed in solid.
// =====================================================================================
static std::vector<bface_t> CopyFacesToOutside(const brushhull_t* bh)
{
    std::vector<bface_t> outside;
    outside.reserve(bh->faces.size());
    for (const bface_t& f : bh->faces)
    {
        bface_t newf{CopyFace(f)};
        newf.bounds = newf.w.getBounds();
        outside.emplace_back(std::move(newf));
    }

    return outside;
}

// =====================================================================================
//  CSGBrush
// =====================================================================================
extern const char *ContentsToString (const contents_t type);
static void     CSGBrush(int brushnum)
{

    // get entity and brush info from the given brushnum that we can work with
    brush_t& b1 = g_mapbrushes[brushnum];
    entity_t* e = &g_entities[b1.entitynum];

    // for each of the hulls
    for (int hull = 0; hull < NUM_HULLS; hull++)
    {
        brushhull_t* bh1 = &b1.hulls[hull];
		if (!bh1->faces.empty() && 
			(hull? b1.clipnodedetaillevel: b1.detaillevel)
			)
		{
			switch (b1.contents)
			{
			case CONTENTS_ORIGIN:
			case CONTENTS_BOUNDINGBOX:
			case CONTENTS_HINT:
			case CONTENTS_TOEMPTY:
				break;
			default:
				Error ("Entity %i, Brush %i: %s brushes not allowed in detail\n", 
					b1.originalentitynum, b1.originalbrushnum, 
					ContentsToString((contents_t)b1.contents));
				break;
			case CONTENTS_SOLID:
				WriteDetailBrush (hull, bh1->faces);
				break;
			}
		}

        // set outside to a copy of the brush's faces
        std::vector<bface_t> outside{CopyFacesToOutside(bh1)};
		if (b1.contents == CONTENTS_TOEMPTY)
		{
			for (bface_t& f : outside)
			{
				f.contents = CONTENTS_TOEMPTY;
				f.backcontents = CONTENTS_TOEMPTY;
			}
		}
        bool overwrite{false};
        // for each brush in entity e
        for (int bn = 0; bn < e->numbrushes; bn++)
        {
            // see if b2 needs to clip a chunk out of b1
			if (e->firstbrush + bn == brushnum)
			{
				continue;
			}
            overwrite = e->firstbrush + bn > brushnum;

            const brush_t& b2 = g_mapbrushes[e->firstbrush + bn];
            const brushhull_t& bh2 = b2.hulls[hull];

			if (b2.contents == CONTENTS_TOEMPTY)
				continue;
			if (
				(hull? (b2.clipnodedetaillevel - 0 > b1.clipnodedetaillevel + 0): (b2.detaillevel - b2.chopdown > b1.detaillevel + b1.chopup))
				)
				continue; // you can't chop
			if (b2.contents == b1.contents && 
				(hull? (b2.clipnodedetaillevel != b1.clipnodedetaillevel): (b2.detaillevel != b1.detaillevel))
				)
			{
				overwrite = 
					(hull? (b2.clipnodedetaillevel < b1.clipnodedetaillevel): (b2.detaillevel < b1.detaillevel))
					;
			}
			if (b2.contents == b1.contents
				&& hull == 0 && b2.detaillevel == b1.detaillevel
				&& b2.coplanarpriority != b1.coplanarpriority)
			{
				overwrite = b2.coplanarpriority > b1.coplanarpriority;
			}

            if (bh2.faces.empty())
                continue;                                  // brush isn't in this hull

            // check brush bounding box first
            // TODO: use boundingbox method instead
            if (test_disjoint(bh1->bounds, bh2.bounds))
            {
                continue;
            }

            // divide faces by the planes of the b2 to find which
            // fragments are inside

            std::vector<bface_t> fo;
            using std::swap;
            swap(fo, outside);
            outside.reserve(fo.size()); // TODO: More? Less?
            for (bface_t& f : fo) {
                // Check face bounding box first
                if (test_disjoint(bh2.bounds, f.bounds))
                {
                    // This face doesn't intersect brush2's bbox
                    outside.emplace_back(std::move(f));
                    continue;
                }
				if (
					(hull? (b2.clipnodedetaillevel > b1.clipnodedetaillevel): (b2.detaillevel > b1.detaillevel))
					)
				{
					const wad_texture_name texname{
                        GetTextureByNumber_CSG (f.texinfo).value_or(wad_texture_name{})
                    };
                    if (f.texinfo == -1
                        || texname.is_skip()
                        || texname.is_any_hint()
                        )
					{
						// should not nullify the fragment inside detail brush
                        outside.emplace_back(std::move(f));
						continue;
					}
				}


                // throw pieces on the front sides of the planes
                // into the outside list, return the remains on the inside
				// find the fragment inside brush2
				Winding w{f.w};
				for (const bface_t& f2 : bh2.faces)
				{
					if (f.planenum == f2.planenum)
					{
						if (!overwrite)
						{
							// face plane is outside brush2
							w.clear();
							break;
						}
						else
						{
							continue;
						}
					}
					if (f.planenum == (f2.planenum ^ 1))
					{
						continue;
					}
					Winding frontw;
					Winding backw;
					w.Clip (f2.plane->normal, f2.plane->dist, frontw, backw);
                    w = std::move(backw);
					if (w.empty())
					{
						break;
					}
				}
				// do real split
				if (w)
				{
                    bool skip{false};
					for (const bface_t& f2 : bh2.faces)
					{
						if (f.planenum == f2.planenum || f.planenum == (f2.planenum ^ 1))
						{
							continue;
						}
						int valid = 0;
						for (int x = 0; x < w.size(); x++)
						{
							vec_t dist = DotProduct(w.m_Points[x], f2.plane->normal) - f2.plane->dist;
							if (dist >= -ON_EPSILON*4) // only estimate
							{
								++valid;
							}
						}
						if (valid >= 2)
						{ // this splitplane forms an edge
							Winding frontw;
							Winding backw;
							f.w.Clip (f2.plane->normal, f2.plane->dist, frontw, backw);
							if (frontw)
							{
								bface_t front{NewFaceFromFace(f)};
								front.w = std::move(frontw);
								front.bounds = front.w.getBounds();

                                outside.emplace_back(std::move(front));
							}
							if (backw)
							{
								f.w = std::move(backw);
								f.bounds = f.w.getBounds();
							} else {
                                skip = true;
                                break;
                            }
						}
					}
                    if(skip) {
                        continue;
                    }
				}
				else
				{
                    outside.emplace_back(std::move(f));
                    continue;
				}

                vec_t area = f.w.getArea();
                if (area < g_tiny_threshold)
                {
                    Verbose("Entity %i, Brush %i: tiny penetration\n", 
						b1.originalentitynum, b1.originalbrushnum
						);
                    c_tiny_clip++;
                    continue;
                }
                // there is one convex fragment of the original
                // face left inside brush2

                if (
                    (hull? (b2.clipnodedetaillevel > b1.clipnodedetaillevel): (b2.detaillevel > b1.detaillevel))
                    )
                { // don't chop or set contents, only nullify
                    f.texinfo = -1;
                    outside.emplace_back(std::move(f));
                    continue;
                }
                if (
                    (hull? b2.clipnodedetaillevel < b1.clipnodedetaillevel: b2.detaillevel < b1.detaillevel)
                    && b2.contents == CONTENTS_SOLID)
                {
                    // Real solid
                    continue;
                }
                if (b1.contents == CONTENTS_TOEMPTY)
                {
                    bool onfront = true, onback = true;
                    for (const bface_t& f2 : bh2.faces)
                    {
                        if (f.planenum == (f2.planenum ^ 1))
                            onback = false;
                        if (f.planenum == f2.planenum)
                            onfront = false;
                    }
                    if (onfront && f.contents < b2.contents)
                        f.contents = b2.contents;
                    if (onback && f.backcontents < b2.contents)
                        f.backcontents = b2.contents;
                    if (!(f.contents == CONTENTS_SOLID && f.backcontents == CONTENTS_SOLID
                        && !GetTextureByNumber_CSG(f.texinfo).value_or(wad_texture_name{}).is_solid_hint()
                        && !GetTextureByNumber_CSG(f.texinfo).value_or(wad_texture_name{}).is_bevel_hint()
                    ))
                    {
                        outside.emplace_back(std::move(f));
                    }
                    continue;
                }
                if (b1.contents > b2.contents
                    || b1.contents == b2.contents && GetTextureByNumber_CSG(f.texinfo).value_or(wad_texture_name{}).is_solid_hint()
                    || b1.contents == b2.contents && GetTextureByNumber_CSG(f.texinfo).value_or(wad_texture_name{}).is_bevel_hint()
                    )
                {
                    // Inside a water brush
                    f.contents = b2.contents;
                    outside.emplace_back(std::move(f));
                }
            }
        }

        // all of the faces left in outside are real surface faces
        SaveOutside(b1, hull, outside, b1.contents);
    }
}

//
// =====================================================================================
//

// =====================================================================================
//  EmitPlanes
// =====================================================================================
static void     EmitPlanes()
{
    int             i;
    mapplane_t* mp;

    g_numplanes = g_nummapplanes;
    mp = g_mapplanes.data();
    dplane_t* dp = g_dplanes.data();
	{
		char name[_MAX_PATH];
		safe_snprintf (name, _MAX_PATH, "%s.pln", g_Mapname);
		FILE *planeout = fopen (name, "wb");
		if (!planeout)
			Error("Couldn't open %s", name);
		SafeWrite (planeout, g_mapplanes.data(), g_nummapplanes * sizeof (mapplane_t));
		fclose (planeout);
	}
    for (i = 0; i < g_nummapplanes; i++, mp++, dp++)
    {
        //if (!(mp->redundant))
        //{
        //    Log("EmitPlanes: plane %i non redundant\n", i);
            VectorCopy(mp->normal, dp->normal);
            dp->dist = mp->dist;
            dp->type = mp->type;
       // }
        //else
       // {
       //     Log("EmitPlanes: plane %i redundant\n", i);
       // }
    }
}

// =====================================================================================
//  SetModelNumbers
//      blah
// =====================================================================================
static void     SetModelNumbers()
{
    int             i;
    int             models;
    char            value[10];

    models = 1;
    for (i = 1; i < g_numentities; i++)
    {
        if (g_entities[i].numbrushes)
        {
            safe_snprintf(value, sizeof(value), "*%i", models);
            models++;
            SetKeyValue(&g_entities[i], u8"model", (const char8_t*) value);
        }
    }
}

void     ReuseModel ()
{
	int i;
	for (i = g_numentities - 1; i >= 1; i--) // so it won't affect the remaining entities in the loop when we move this entity backward
	{
		const char *name = (const char*) ValueForKey (&g_entities[i], u8"zhlt_usemodel");
		if (!*name)
		{
			continue;
		}
		int j;
		for (j = 1; j < g_numentities; j++)
		{
			if (*ValueForKey (&g_entities[j], u8"zhlt_usemodel"))
			{
				continue;
			}
			if (!strcmp (name,(const char*)  ValueForKey (&g_entities[j], u8"targetname")))
			{
				break;
			}
		}
		if (j == g_numentities)
		{
			if (strings_equal_with_ascii_case_insensitivity(name, u8"null"))
			{
				SetKeyValue (&g_entities[i], u8"model", u8"");
				continue;
			}
			Error ("zhlt_usemodel: can not find target entity '%s', or that entity is also using 'zhlt_usemodel'.\n", name);
		}
		SetKeyValue (&g_entities[i], u8"model", ValueForKey (&g_entities[j], u8"model"));
		if (j > i)
		{
			// move this entity forward
			// to prevent precache error in case of .mdl/.spr and wrong result of EntityForModel in case of map model
			entity_t tmp{std::move(g_entities[i])};
            std::move(&g_entities[i + 1], &g_entities[i + 1] + ((j + 1) - (i + 1)), &g_entities[i]);
			g_entities[j] = std::move(tmp);
		}
	}
}

// =====================================================================================
//  SetLightStyles
// =====================================================================================
constexpr std::size_t MAX_SWITCHED_LIGHTS = 32;
constexpr std::size_t MAX_LIGHTTARGETS_NAME = 64;

static void     SetLightStyles()
{
    int             stylenum;
    const char*     t;
    entity_t*       e;
    int             i, j;
    char            value[10];
    char            lighttargets[MAX_SWITCHED_LIGHTS][MAX_LIGHTTARGETS_NAME];

    	bool			newtexlight = false;

    // any light that is controlled (has a targetname)
    // must have a unique style number generated for it

    stylenum = 0;
    for (i = 1; i < g_numentities; i++)
    {
        e = &g_entities[i];

        t = (const char*) ValueForKey(e, u8"classname");
        if (strncasecmp(t, "light", 5))
        {
            //LRC:
			// if it's not a normal light entity, allocate it a new style if necessary.
	        t = (const char*) ValueForKey(e, u8"style");
			switch (atoi(t))
			{
			case 0: // not a light, no style, generally pretty boring
				continue;
			case -1: // normal switchable texlight
				safe_snprintf(value, sizeof(value), "%i", 32 + stylenum);
				SetKeyValue(e, u8"style", (const char8_t*) value);
				stylenum++;
				continue;
			case -2: // backwards switchable texlight
				safe_snprintf(value, sizeof(value), "%i", -(32 + stylenum));
				SetKeyValue(e, u8"style", (const char8_t*) value);
				stylenum++;
				continue;
			case -3: // (HACK) a piggyback texlight: switched on and off by triggering a real light that has the same name
				SetKeyValue(e, u8"style", u8"0"); // just in case the level designer didn't give it a name
				newtexlight = true;
				// don't 'continue', fall out
			}
	        //LRC (ends)
        }
        t = (const char*) ValueForKey(e, u8"targetname");
		if (*ValueForKey (e, u8"zhlt_usestyle"))
		{
			t = (const char*) ValueForKey(e, u8"zhlt_usestyle");
			if (strings_equal_with_ascii_case_insensitivity(t, u8"null"))
			{
				t = "";
			}
		}
        if (!t[0])
        {
            continue;
        }

        // find this targetname
        for (j = 0; j < stylenum; j++)
        {
            if (!strcmp(lighttargets[j], t))
            {
                break;
            }
        }
        if (j == stylenum)
        {
            hlassume(stylenum < MAX_SWITCHED_LIGHTS, assume_MAX_SWITCHED_LIGHTS);
            safe_strncpy(lighttargets[j], t, MAX_LIGHTTARGETS_NAME);
            stylenum++;
        }
        safe_snprintf(value, sizeof(value), "%i", 32 + j);
        SetKeyValue(e, u8"style", (const char8_t*) value);
    }

}

static float3_array angles_for_vector(const float3_array& vector) {
	float z = vector[2], r = std::hypot(vector[0], vector[1]);
	float hyp = std::hypot(z, r);
	float3_array angles{};
	if (hyp >= NORMAL_EPSILON) {
		z /= hyp, r /= hyp;
		if (r < NORMAL_EPSILON) {
			if (z < 0) {
				angles[0] = -90, angles[1] = 0;
			} else {
				angles[0] = 90, angles[1] = 0;
			}
		} else {
			angles[0] = std::atan(z / r) / std::numbers::pi_v<float> * 180;
			float x = vector[0], y = vector[1];
			hyp = std::hypot(x, y);
			x /= hyp, y /= hyp;
			if (x < -1 + NORMAL_EPSILON) {
				angles[1] = -180;
			} else if (y >= 0) {
				angles[1] = 2 * std::atan (y / (1+x)) / std::numbers::pi_v<float> * 180;
			} else {
				angles[1] = 2 * std::atan (y / (1+x)) / std::numbers::pi_v<float> * 180 + 360;
			}
		}
	}
	return angles;
}

static void UnparseEntities()
{
    char8_t* buf;
    char8_t* end;
    char line[MAXTOKEN];
    int i;

    buf = g_dentdata.data();
    end = buf;
    *end = 0;

	for (i = 0; i < g_numentities; i++)
	{
		entity_t *mapent = &g_entities[i];
		if (
			classname_is(mapent, u8"info_sunlight") ||
			classname_is(mapent, u8"light_environment"))
		{
			float3_array vec{};
			{
				vec = get_float_vector_for_key(*mapent, u8"angles");
				float pitch = FloatForKey(mapent, u8"pitch");
				if (pitch) {
					vec[0] = pitch;
				}

				std::u8string_view target = value_for_key (mapent, u8"target");
				if (!target.empty()) {
					std::optional<std::reference_wrapper<entity_t>> maybeTargetEnt = find_target_entity(target);
					if (maybeTargetEnt) {
						float3_array originA = get_float_vector_for_key(*mapent, u8"origin");
						float3_array originB = get_float_vector_for_key(maybeTargetEnt.value().get(), u8"origin");
						float3_array normal;
						VectorSubtract(originB, originA, normal);
						vec = angles_for_vector(normal);
					}
				}
			}
			char stmp[1024];
			safe_snprintf(stmp, 1024, "%g %g %g", vec[0], vec[1], vec[2]);
			SetKeyValue(mapent, u8"angles", (const char8_t*) stmp);
			DeleteKey(mapent, u8"pitch");

			if (!strcmp ((const char*) ValueForKey (mapent, u8"classname"), "info_sunlight"))
			{
				if (g_numentities == MAX_MAP_ENTITIES)
				{
					Error("g_numentities == MAX_MAP_ENTITIES");
				}
				entity_t *newent = &g_entities[g_numentities++];
                using std::swap;
                swap(newent->keyValues, mapent->keyValues);
				SetKeyValue (newent, u8"classname", u8"light_environment");
				SetKeyValue (newent, u8"_fake", u8"1");
			}
		}
	}
    for (i = 0; i < g_numentities; i++)
	{
		entity_t *mapent = &g_entities[i];
		if (classname_is(mapent, u8"light_shadow")
			|| classname_is(mapent, u8"light_bounce")
		) {
			SetKeyValue (mapent, u8"convertfrom", ValueForKey (mapent, u8"classname"));
			SetKeyValue (mapent, u8"classname", (*ValueForKey (mapent, u8"convertto")? ValueForKey (mapent, u8"convertto"): u8"light"));
			DeleteKey (mapent, u8"convertto");
		}
	}
	// ugly code
	for (i = 0; i < g_numentities; i++)
	{
		entity_t *mapent = &g_entities[i];
		if (classname_is(mapent, u8"light_surface"))
		{
			if (key_value_is_empty(mapent, u8"_tex"))
			{
				SetKeyValue (mapent, u8"_tex", u8"                ");
			}
			std::u8string_view newclassname = value_for_key (mapent, u8"convertto");
			if (newclassname.empty())
			{
				SetKeyValue (mapent, u8"classname", u8"light");
			}
			else if (!newclassname.starts_with(u8"light"))
			{
				Error ("New classname for 'light_surface' should begin with 'light' not '%s'.\n", (const char*) newclassname.data());
			}
			else
			{
				SetKeyValue (mapent, u8"classname", newclassname);
			}
			DeleteKey (mapent, u8"convertto");
		}
	}
	if (!g_nolightopt)
	{
		int i, j;
		int count = 0;
		bool *lightneedcompare = (bool *)malloc (g_numentities * sizeof (bool));
		hlassume (lightneedcompare != nullptr, assume_NoMemory);
		memset (lightneedcompare, 0, g_numentities * sizeof(bool));
		for (i = g_numentities - 1; i > -1; i--)
		{
			entity_t *ent = &g_entities[i];
			const char8_t *classname = ValueForKey (ent, u8"classname");
			const char8_t *targetname = ValueForKey (ent, u8"targetname");
			int style = IntForKey (ent, u8"style");
			if (!targetname[0] || strcmp ((const char*) classname, "light") && strcmp ((const char*) classname, "light_spot") && strcmp ((const char*) classname, "light_environment"))
				continue;
			for (j = i + 1; j < g_numentities; j++)
			{
				if (!lightneedcompare[j])
					continue;
				entity_t *ent2 = &g_entities[j];
				const char8_t *targetname2 = ValueForKey (ent2, u8"targetname");
				int style2 = IntForKey (ent2, u8"style");
				if (style == style2 && !strcmp ((const char*) targetname, (const char*) targetname2))
					break;
			}
			if (j < g_numentities)
			{
				DeleteKey (ent, u8"targetname");
				count++;
			}
			else
			{
				lightneedcompare[i] = true;
			}
		}
		if (count > 0)
		{
			Log ("%d redundant named lights optimized.\n", count);
		}
		free (lightneedcompare);
	}
    for (i = 0; i < g_numentities; i++)
    {
        if (g_entities[i].keyValues.empty()) {
            // Ent got removed
            continue;
        }

        strcat((char*) end, (const char*) "{\n");
        end += 2;

        for (const entity_key_value& kv : g_entities[i].keyValues)
        {
            snprintf(line, sizeof(line), "\"%s\" \"%s\"\n", (const char*) kv.key().data(), (const char*) kv.value().data());
            strcat((char*) end, line);
            end += strlen(line);
        }
        strcat((char*) end, (const char*) u8"}\n");
        end += 2;

        if (end > buf + MAX_MAP_ENTSTRING)
        {
            Error("Entity text too long");
        }
    }
    g_entdatasize = end - buf + 1;
}

// =====================================================================================
//  ConvertHintToEmtpy
// =====================================================================================
static void     ConvertHintToEmpty()
{
    // Convert HINT brushes to EMPTY after they have been carved by csg
    for (std::size_t i = 0; i < MAX_MAP_BRUSHES; i++)
    {
        if (g_mapbrushes[i].contents == CONTENTS_HINT)
        {
            g_mapbrushes[i].contents = CONTENTS_EMPTY;
        }
    }
}

// =====================================================================================
//  WriteBSP
// =====================================================================================
void LoadWadValue ()
{
    std::u8string wadValue;
	ParseFromMemory(std::u8string_view{g_dentdata.data(), g_entdatasize});
	if (GetToken (true)) {
		if (g_token != u8"{"sv)
		{
			Error ("ParseEntity: { not found");
		}
		while (1)
		{
			if (!GetToken (true))
			{
				Error ("ParseEntity: EOF without closing brace");
			}
			if (g_token == u8"}"sv)
			{
				break;
			}
			entity_key_value kv{parse_entity_key_value()};
            if (kv.key() == u8"wad") {
			    SetKeyValue(&g_entities[0], std::move(kv));
            }
		}
	}
}
void WriteBSP(const char* const name)
{
    std::filesystem::path bspPath;
    bspPath = name;
    bspPath += u8".bsp";

    SetModelNumbers();
	ReuseModel();
    SetLightStyles();

    if (!g_onlyents)
        WriteMiptex(bspPath);
	if (g_onlyents)
		LoadWadValue ();

    UnparseEntities();
    ConvertHintToEmpty(); // this is ridiculous. --vluzacn
    if (g_chart)
        print_bsp_file_sizes(bspGlobals);
    WriteBSPFile(bspPath);
}


// AJM: added in 
unsigned int    BrushClipHullsDiscarded = 0; 
unsigned int    ClipNodesDiscarded = 0;

//AJM: added in function
static void     MarkEntForNoclip(entity_t*  ent)
{
    int             i;
    brush_t*        b;

    for (i = ent->firstbrush; i < ent->firstbrush + ent->numbrushes; i++)
    {
        b = &g_mapbrushes[i];
        b->noclip = 1;  

        BrushClipHullsDiscarded++;
        ClipNodesDiscarded += b->numsides;
    }
}

// AJM
// =====================================================================================
//  CheckForNoClip
//      marks the noclip flag on any brushes that dont need clipnode generation, eg. func_illusionaries
// =====================================================================================
static void     CheckForNoClip()
{
    int             i;
    entity_t*       ent;

    char            entclassname[MAX_KEY]; 
    int             spawnflags;
	int				count = 0;

    if (!g_bClipNazi) 
        return; // NO CLIP FOR YOU!!!

    for (i = 0; i < g_numentities; i++)
    {
        if (!g_entities[i].numbrushes) 
            continue; // not a model

        if (!i) 
            continue; // dont waste our time with worldspawn

        ent = &g_entities[i];

        std::strcpy(entclassname, (const char*) ValueForKey(ent, u8"classname"));
        spawnflags = atoi((const char*) ValueForKey(ent, u8"spawnflags"));
		int skin = IntForKey(ent, u8"skin"); //vluzacn

		if ((skin != -16) &&
			(
				!strcmp(entclassname, "env_bubbles")
				|| !strcmp(entclassname, "func_illusionary")
				|| (spawnflags & 8) && 
				(   /* NOTE: func_doors as far as i can tell may need clipnodes for their
							player collision detection, so for now, they stay out of it. */
					!strcmp(entclassname, "func_train")
					|| !strcmp(entclassname, "func_door")
					|| !strcmp(entclassname, "func_water")
					|| !strcmp(entclassname, "func_door_rotating")
					|| !strcmp(entclassname, "func_pendulum")
					|| !strcmp(entclassname, "func_train")
					|| !strcmp(entclassname, "func_tracktrain")
					|| !strcmp(entclassname, "func_vehicle")
				)
				|| (skin != 0) && (!strcmp(entclassname, "func_door") || !strcmp(entclassname, "func_water"))
				|| (spawnflags & 2) && (!strcmp(entclassname, "func_conveyor"))
				|| (spawnflags & 1) && (!strcmp(entclassname, "func_rot_button"))
				|| (spawnflags & 64) && (!strcmp(entclassname, "func_rotating"))
			))
		{
			MarkEntForNoclip(ent);
			count++;
		}
    }

    Log("%i entities discarded from clipping hulls\n", count);
}

// =====================================================================================
//  ProcessModels
// =====================================================================================

static void     ProcessModels()
{
    int type;
    int             placed;
    int contents;
    brush_t temp;

    std::vector<brush_t> temps;

    for (int i = 0; i < g_numentities; i++)
    {
        if (!g_entities[i].numbrushes) { // only models
            continue;
        }

        // sort the contents down so stone bites water, etc
        int first = g_entities[i].firstbrush;
        if(temps.size() < g_entities[i].numbrushes) {
            temps.resize(g_entities[i].numbrushes);
        }
		for (int j = 0; j < g_entities[i].numbrushes; j++)
		{
            temps[j] = g_mapbrushes[first + j];
		}
		int placedcontents;
		bool b_placedcontents = false;
		for (placed = 0; placed < g_entities[i].numbrushes; )
		{
			bool b_contents = false;
			for (int j = 0; j < g_entities[i].numbrushes; j++)
			{
				brush_t *brush = &temps[j];
				if (b_placedcontents && brush->contents <= placedcontents)
					continue;
				if (b_contents && brush->contents >= contents)
					continue;
				b_contents = true;
				contents = brush->contents;
			}
			for (int j = 0; j < g_entities[i].numbrushes; j++)
			{
				brush_t *brush = &temps[j];
				if (brush->contents == contents)
				{
					g_mapbrushes[first + placed] = *brush;
					placed++;
				}
			}
			b_placedcontents = true;
			placedcontents = contents;
		}

        // csg them in order
        if (i == 0) // if its worldspawn....
        {
            int numT = g_numthreads;
            g_numthreads = 1;
            NamedRunThreadsOnIndividual(g_entities[i].numbrushes, g_estimate, CSGBrush);
            g_numthreads = numT;
            CheckFatal();
        }
        else
        {
            for (int j = 0; j < g_entities[i].numbrushes; j++)
            {
                CSGBrush(first + j);
            }
        }

        // write end of model marker
        for (int j = 0; j < NUM_HULLS; j++)
        {
			fprintf (out[j], "-1 -1 -1 -1 -1\n");
			fprintf (out_detailbrush[j], "-1\n");
        }
    }
}

// =====================================================================================
//  SetModelCenters
// =====================================================================================
static void     SetModelCenters(int entitynum)
{
    int             i;
    int             last;
    char            string[MAXTOKEN];
    entity_t*       e = &g_entities[entitynum];
    bounding_box     bounds;

    if ((entitynum == 0) || (e->numbrushes == 0)) // skip worldspawn and point entities
        return;

    if (!*ValueForKey(e, u8"light_origin")) // skip if its not a zhlt_flags light_origin
        return;

    for (i = e->firstbrush, last = e->firstbrush + e->numbrushes; i < last; i++)
    {
        if (g_mapbrushes[i].contents != CONTENTS_ORIGIN
			&& g_mapbrushes[i].contents != CONTENTS_BOUNDINGBOX
			)
        {
            add_to_bounding_box(bounds, g_mapbrushes[i].hulls->bounds);
        }
    }

    vec3_array center;
    VectorAdd(bounds.mins, bounds.maxs, center);
    VectorScale(center, 0.5, center);

    safe_snprintf(string, MAXTOKEN, "%i %i %i", (int)center[0], (int)center[1], (int)center[2]);
    SetKeyValue(e, u8"model_center", (const char8_t*)  string);
}

//
// =====================================================================================
//

// =====================================================================================
//  BoundWorld
// =====================================================================================
static void     BoundWorld()
{
    int             i;
    brushhull_t*    h;

    world_bounds = bounding_box{};

    for (i = 0; i < g_nummapbrushes; i++)
    {
        h = &g_mapbrushes[i].hulls[0];
        if (h->faces.empty())
        {
            continue;
        }
        add_to_bounding_box(world_bounds, h->bounds);
    }

    Verbose("World bounds: (%i %i %i) to (%i %i %i)\n",
            (int)world_bounds.mins[0], (int)world_bounds.mins[1], (int)world_bounds.mins[2],
            (int)world_bounds.maxs[0], (int)world_bounds.maxs[1], (int)world_bounds.maxs[2]);
}

// =====================================================================================
//  Usage
//      prints out usage sheet
// =====================================================================================
static void     Usage()
{
    Banner(); // TODO: Call banner from main CSG process? 

    Log("\n-= %s Options =-\n\n", g_Program);
    Log("    -nowadtextures   : Include all used textures into bsp\n");
    Log("    -wadinclude file : Include specific wad or directory into bsp\n");
    Log("    -noclip          : don't create clipping hull\n");
    
    Log("    -clipeconomy     : turn clipnode economy mode on\n");

	Log("    -cliptype value  : set to smallest, normalized, simple, precise, or legacy (default)\n");
	Log("    -nullfile file   : specify list of entities to retexture with NULL\n");

    Log("    -onlyents        : do an entity update from .map to .bsp\n");
    Log("    -noskyclip       : disable automatic clipping of SKY brushes\n");
    Log("    -tiny #          : minmum brush face surface area before it is discarded\n");
    Log("    -brushunion #    : threshold to warn about overlapping brushes\n\n");
    Log("    -hullfile file   : Reads in custom collision hull dimensions\n");
	Log("    -wadcfgfile file : wad configuration file\n");
	Log("    -wadconfig name  : use the old wad configuration approach (select a group from wad.cfg)\n");
    Log("    -texdata #       : Alter maximum texture memory limit (in kb)\n");
    Log("    -chart           : display bsp statitics\n");
    Log("    -low | -high     : run program an altered priority level\n");
    Log("    -nolog           : don't generate the compile logfiles\n");
	Log("    -noresetlog      : Do not delete log file\n");
    Log("    -threads #       : manually specify the number of threads to run\n");
#ifdef SYSTEM_WIN32
    Log("    -estimate        : display estimated time during compile\n");
#endif
#ifdef SYSTEM_POSIX
    Log("    -noestimate      : do not display continuous compile time estimates\n");
#endif
    Log("    -verbose         : compile with verbose messages\n");
    Log("    -noinfo          : Do not show tool configuration information\n");

    Log("    -nonulltex       : Turns off null texture stripping\n");
	Log("    -nonullifytrigger: don't remove 'aaatrigger' texture\n");


	Log("    -nolightopt      : don't optimize engine light entities\n");

#ifdef HLCSG_GAMETEXTMESSAGE_UTF8
	Log("    -notextconvert   : don't convert game_text message from Windows ANSI to UTF8 format\n");
#endif

    Log("    -dev #           : compile with developer message\n\n");


    Log("    -nowadautodetect : Disable auto-detection of wadfiles\n");

	Log("    -scale #         : Scale the world. Use at your own risk.\n");
    Log("    -worldextent #   : Extend map geometry limits beyond +/-32768.\n");
    Log("    mapfile          : The mapfile to compile\n\n");

    exit(1);
}

// =====================================================================================
//  DumpWadinclude
//      prints out the wadinclude list
// =====================================================================================
static void     DumpWadinclude()
{
    Log("Wadinclude list\n");
    Log("---------------\n");
    WadInclude_i it;

    for (it = g_WadInclude.begin(); it != g_WadInclude.end(); it++)
    {
        Log("%s\n", (const char*) it->c_str());
    }
    Log("---------------\n\n");
}

// =====================================================================================
//  Settings
//      prints out settings sheet
// =====================================================================================
static void     Settings(const bsp_data& bspData)
{
    const char*           tmp;

    if (!g_info)
        return; 

    Log("\nCurrent %s Settings\n", g_Program);
    Log("Name                 |  Setting  |  Default\n"
        "---------------------|-----------|-------------------------\n");

    // ZHLT Common Settings
    Log("threads             [ %7td ] [  Varies ]\n", g_numthreads);
    Log("verbose               [ %7s ] [ %7s ]\n", g_verbose ? "on" : "off", cli_option_defaults::verbose ? "on" : "off");
    Log("log                   [ %7s ] [ %7s ]\n", g_log ? "on" : "off", cli_option_defaults::log ? "on" : "off");
    Log("reset logfile         [ %7s ] [ %7s ]\n", g_resetlog ? "on" : "off", DEFAULT_RESETLOG ? "on" : "off");

    Log("developer             [ %7d ] [ %7d ]\n", g_developer, cli_option_defaults::developer);
    Log("chart                 [ %7s ] [ %7s ]\n", g_chart ? "on" : "off", cli_option_defaults::chart ? "on" : "off");
    Log("estimate              [ %7s ] [ %7s ]\n", g_estimate ? "on" : "off", cli_option_defaults::estimate ? "on" : "off");
    Log("max texture memory    [ %7td ] [ %7td ]\n", g_max_map_miptex, cli_option_defaults::max_map_miptex);

    switch (g_threadpriority)
    {
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
    Log("priority              [ %7s ] [ %7s ]\n", tmp, "Normal");
    Log("\n");

    // HLCSG Specific Settings

    Log("noclip                [ %7s ] [ %7s ]\n", g_noclip          ? "on" : "off", DEFAULT_NOCLIP       ? "on" : "off");

    Log("null texture stripping[ %7s ] [ %7s ]\n", g_bUseNullTex     ? "on" : "off", cli_option_defaults::nulltex      ? "on" : "off");


    Log("clipnode economy mode [ %7s ] [ %7s ]\n", g_bClipNazi       ? "on" : "off", DEFAULT_CLIPNAZI     ? "on" : "off");

	Log("clip hull type        [ %7s ] [ %7s ]\n", GetClipTypeString(g_cliptype), GetClipTypeString(DEFAULT_CLIPTYPE));

    Log("onlyents              [ %7s ] [ %7s ]\n", g_onlyents        ? "on" : "off", DEFAULT_ONLYENTS     ? "on" : "off");
    Log("wadtextures           [ %7s ] [ %7s ]\n", g_wadtextures     ? "on" : "off", DEFAULT_WADTEXTURES  ? "on" : "off");
    Log("skyclip               [ %7s ] [ %7s ]\n", g_skyclip         ? "on" : "off", DEFAULT_SKYCLIP      ? "on" : "off");
    Log("hullfile              [ %7s ] [ %7s ]\n", g_hullfile ? g_hullfile : "None", "None");
	Log("wad.cfg file          [ %7s ] [ %7s ]\n", g_wadcfgfile? g_wadcfgfile: "None", "None");
	Log("wad.cfg config name   [ %7s ] [ %7s ]\n", g_wadconfigname? g_wadconfigname: "None", "None");
	Log("nullfile              [ %7s ] [ %7s ]\n", g_nullfile ? g_nullfile : "None", "None");
	Log("nullify trigger       [ %7s ] [ %7s ]\n", g_nullifytrigger? "on": "off", DEFAULT_NULLIFYTRIGGER? "on": "off");
    // calc min surface area
    {
        char            tiny_penetration[10];
        char            default_tiny_penetration[10];

        safe_snprintf(tiny_penetration, sizeof(tiny_penetration), "%3.3f", g_tiny_threshold);
        safe_snprintf(default_tiny_penetration, sizeof(default_tiny_penetration), "%3.3f", DEFAULT_TINY_THRESHOLD);
        Log("min surface area      [ %7s ] [ %7s ]\n", tiny_penetration, default_tiny_penetration);
    }

    // calc union threshold
    {
        char            brush_union[10];
        char            default_brush_union[10];

        safe_snprintf(brush_union, sizeof(brush_union), "%3.3f", g_BrushUnionThreshold);
        safe_snprintf(default_brush_union, sizeof(default_brush_union), "%3.3f", DEFAULT_BRUSH_UNION_THRESHOLD);
        Log("brush union threshold [ %7s ] [ %7s ]\n", brush_union, default_brush_union);
    }
    {
        char            buf1[10];
        char            buf2[10];

		if (g_scalesize > 0)
			safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_scalesize);
		else
			strcpy (buf1, "None");
		if (DEFAULT_SCALESIZE > 0)
			safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_SCALESIZE);
		else
			strcpy (buf2, "None");
        Log("map scaling           [ %7s ] [ %7s ]\n", buf1, buf2);
    }
    Log("light name optimize   [ %7s ] [ %7s ]\n", !g_nolightopt? "on" : "off", !DEFAULT_NOLIGHTOPT? "on" : "off");
#ifdef HLCSG_GAMETEXTMESSAGE_UTF8
	Log("convert game_text     [ %7s ] [ %7s ]\n", !g_noutf8? "on" : "off", !DEFAULT_NOUTF8? "on" : "off");
#endif
    Log("world extent          [ %7d ] [ %7d ]\n", bspData.worldExtent, 65536);

    Log("\n");
}

// AJM: added in
// =====================================================================================
//  CSGCleanup
// =====================================================================================
void            CSGCleanup()
{
    //Log("CSGCleanup\n");
    FreeWadPaths();
}

// =====================================================================================
//  Main
//      Oh, come on.
// =====================================================================================
int             main(const int argc, char** argv)
{
    bsp_data& bspData = bspGlobals;
    int             i;                          
    char            name[_MAX_PATH];            // mapanme 
    double          start, end;                 // start/end time log
    const char*     mapname_from_arg = nullptr;    // mapname path from passed argvar

    g_Program = "HLCSG";

	int argcold = argc;
	char ** argvold = argv;
	{
		int argc;
		char ** argv;
		ParseParamFile (argcold, argvold, argc, argv);
		{
    if (argc == 1)
        Usage();

    // Hard coded list of -wadinclude files, used for HINT texture brushes so lazy
    // mapmakers wont cause beta testers (or possibly end users) to get a wad 
    // error on hlt.wad etc.
    g_WadInclude.push_back(u8"hlt.wad");
    g_WadInclude.push_back(u8"sdhlt.wad"); // seedee's HLT
    g_WadInclude.push_back(u8"zhlt.wad"); // Zoner's HLT

	InitDefaultHulls ();

    // detect argv
    for (i = 1; i < argc; i++)
    {
        if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-threads"))
        {
            if (i + 1 < argc)	//added "1" .--vluzacn
            {
                g_numthreads = atoi(argv[++i]);

                if (std::cmp_greater(g_numthreads, MAX_THREADS))
                {
                    Log("Expected value below %zu for '-threads'\n", MAX_THREADS);
                    Usage();
                }
            }
            else
            {
                Usage();
            }
        }

        else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-worldextent"))
        {
            bspData.worldExtent = atoi(argv[++i]);
        }

#ifdef SYSTEM_POSIX
        else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-noestimate"))
        {
            g_estimate = false;
        }
#endif

        else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-dev"))
        {
            if (i + 1 < argc)	//added "1" .--vluzacn
            {
                g_developer = (developer_level_t)atoi(argv[++i]);
            }
            else
            {
                Usage();
            }
        }
        else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-verbose"))
        {
            g_verbose = true;
        }
        else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-noinfo"))
        {
            g_info = false;
        }
        else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-chart"))
        {
            g_chart = true;
        }
        else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-low"))
        {
            g_threadpriority = q_threadpriority::eThreadPriorityLow;
        }
        else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-high"))
        {
            g_threadpriority = q_threadpriority::eThreadPriorityHigh;
        }
        else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-nolog"))
        {
            g_log = false;
        }
        else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-skyclip"))
        {
            g_skyclip = true;
        }
        else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-noskyclip"))
        {
            g_skyclip = false;
        }
        else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-noclip"))
        {
            g_noclip = true;
        }
        else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-onlyents"))
        {
            g_onlyents = true;
        }

        else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-nonulltex"))
        {
            g_bUseNullTex = false;
        }

        else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-clipeconomy"))
        {
            g_bClipNazi = true;
        }

		else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-cliptype"))
		{
			if (i + 1 < argc)	//added "1" .--vluzacn
			{
				++i;
				if(strings_equal_with_ascii_case_insensitivity(argv[i],"smallest"))
				{ g_cliptype = clip_smallest; }
				else if(strings_equal_with_ascii_case_insensitivity(argv[i],"normalized"))
				{ g_cliptype = clip_normalized; }
				else if(strings_equal_with_ascii_case_insensitivity(argv[i],"simple"))
				{ g_cliptype = clip_simple; }
				else if(strings_equal_with_ascii_case_insensitivity(argv[i],"precise"))
				{ g_cliptype = clip_precise; }
				else if(strings_equal_with_ascii_case_insensitivity(argv[i],"legacy"))
				{ g_cliptype = clip_legacy; }
			}
            else
            {
                Log("Error: -cliptype: incorrect usage of parameter\n");
                Usage();
            }
		}

		else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-nullfile"))
		{
            if (i + 1 < argc)	//added "1" .--vluzacn
            {
                g_nullfile = argv[++i];
            }
            else
            {
            	Log("Error: -nullfile: expected path to null ent file following parameter\n");
                Usage();
            }
		}
        else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-nowadautodetect"))
        { 
            g_bWadAutoDetect = false;
        }
        else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-nowadtextures"))
        {
            g_wadtextures = false;
        }
        else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-wadinclude"))
        {
            if (i + 1 < argc)	//added "1" .--vluzacn
            {
                g_WadInclude.push_back((const char8_t*) argv[++i]);
            }
            else
            {
                Usage();
            }
        }
        else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-texdata"))
        {
            if (i + 1 < argc)	//added "1" .--vluzacn
            {
                int             x = atoi(argv[++i]) * 1024;

                //if (x > g_max_map_miptex) //--vluzacn
                {
                    g_max_map_miptex = x;
                }
            }
            else
            {
                Usage();
            }
        }
        else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-brushunion"))
        {
            if (i + 1 < argc)	//added "1" .--vluzacn
            {
                g_BrushUnionThreshold = (float)atof(argv[++i]);
            }
            else
            {
                Usage();
            }
        }
        else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-tiny"))
        {
            if (i + 1 < argc)	//added "1" .--vluzacn
            {
                g_tiny_threshold = (float)atof(argv[++i]);
            }
            else
            {
                Usage();
            }
        }
        else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-hullfile"))
        {
            if (i + 1 < argc)	//added "1" .--vluzacn
            {
                g_hullfile = argv[++i];
            }
            else
            {
                Usage();
            }
        }
		else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-wadcfgfile"))
		{
			if (i + 1 < argc)
			{
				g_wadcfgfile = argv[++i];
			}
			else
			{
				Usage ();
			}
		}
		else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-wadconfig"))
		{
			if (i + 1 < argc)
			{
				g_wadconfigname = argv[++i];
			}
			else
			{
				Usage ();
			}
		}
        else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-scale"))
        {
            if (i + 1 < argc)
            {
                g_scalesize = atof(argv[++i]);
            }
            else
            {
                Usage();
            }
        }
		else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-noresetlog"))
		{
			g_resetlog = false;
		}
		else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-nolightopt"))
		{
			g_nolightopt = true;
		}
#ifdef HLCSG_GAMETEXTMESSAGE_UTF8
		else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-notextconvert"))
		{
			g_noutf8 = true;
		}
#endif
		else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-viewsurface"))
		{
			g_viewsurface = true;
		}
		else if (strings_equal_with_ascii_case_insensitivity(argv[i], u8"-nonullifytrigger"))
		{
			g_nullifytrigger = false;
		}
        else if (argv[i][0] == '-')
        {
            Log("Unknown option \"%s\"\n", argv[i]);
            Usage();
        }
        else if (!mapname_from_arg)
        {
            mapname_from_arg = argv[i];
        }
        else
        {
            Log("Unknown option \"%s\"\n", argv[i]);
            Usage();
        }
    }

    // no mapfile?
    if (!mapname_from_arg)
    {
        // what a shame.
        Log("No mapfile specified\n");
        Usage();
    }

    // handle mapname
    safe_strncpy(g_Mapname, mapname_from_arg, _MAX_PATH);
    FlipSlashes(g_Mapname);
    StripExtension(g_Mapname);

    // onlyents
    if (!g_onlyents)
        ResetTmpFiles();

    // other stuff
    ResetErrorLog();                     
	if (!g_onlyents && g_resetlog)
		ResetLog();                          
    OpenLog(g_clientid);                  
    atexit(CloseLog);                       
    LogStart(argcold, argvold);
	log_arguments(argc, argv);
	hlassume (CalcFaceExtents_test (), assume_first);
    atexit(CSGCleanup); // AJM
    dtexdata_init();                        
    atexit(dtexdata_free);

    // START CSG
    // AJM: re-arranged some stuff up here so that the mapfile is loaded
    //  before settings are finalised and printed out, so that the info_compile_parameters
    //  entity can be dealt with effectively
    start = I_FloatTime();
	if (g_hullfile)
	{
		std::filesystem::path test = std::filesystem::path(g_Mapname).parent_path() / g_hullfile;
		if (std::filesystem::exists (test))
		{
			g_hullfile = c_strdup(test.c_str());
		}
		else
		{
            test = get_path_to_directory_with_executable(argv) / g_hullfile;
			if (std::filesystem::exists (test))
			{
				g_hullfile = c_strdup(test.c_str());
			}
		}
	}
	if (g_nullfile)
	{

		std::filesystem::path test = std::filesystem::path(g_Mapname).parent_path() / g_nullfile;
		if (std::filesystem::exists (test))
		{
			g_nullfile = c_strdup(test.c_str());
		}
		else
		{
            std::filesystem::path test = get_path_to_directory_with_executable(argv) / g_nullfile;
			if (std::filesystem::exists (test))
			{
				g_nullfile = c_strdup(test.c_str());
			}
		}
	}
	if (g_wadcfgfile) // If wad.cfg exists
	{
        std::filesystem::path wadCfgPath = std::filesystem::path(g_Mapname).parent_path() / g_wadcfgfile;
		if (std::filesystem::exists (wadCfgPath)) // Use global wad.cfg if file exists
		{
			g_wadcfgfile = c_strdup(wadCfgPath.c_str()); 
		}
		else
		{
            // Look for wad.cfg relative to exe
            wadCfgPath = get_path_to_directory_with_executable(argv) / g_wadcfgfile;
			if (std::filesystem::exists (wadCfgPath))
			{
				g_wadcfgfile = c_strdup(wadCfgPath.c_str());
			}
		}
	}
    Verbose("Loading hull file\n");
    LoadHullfile(g_hullfile);               // if the user specified a hull file, load it now
	if(g_bUseNullTex)
	{ properties_initialize(g_nullfile); }
    safe_strncpy(name, mapname_from_arg, _MAX_PATH); // make a copy of the nap name
	FlipSlashes(name);
    DefaultExtension(name, ".map");                  // might be .reg
    Verbose("Loading map file\n");
    LoadMapFile(name);
    ThreadSetDefault();                    
    ThreadSetPriority(g_threadpriority);  
    Settings(bspData);


#ifdef HLCSG_GAMETEXTMESSAGE_UTF8
	if (!g_noutf8)
	{
		int count = 0;

		for (i = 0; i < g_numentities; i++)
		{
			entity_t *ent = &g_entities[i];
			const char *value;

			if (!classname_is(ent, u8"game_text"))
			{
				continue;
			}

			value = value_for_key (ent, u8"message");
			if (!value.empty())
			{
				std::u8string newvalue = ansiToUtf8 (value);
				if (newvalue != value)
				{
					SetKeyValue (ent, u8"message", newvalue);
					count++;
				}
			}
		}

		if (count)
		{
			Log ("%d game_text messages converted from Windows ANSI(CP_ACP) to UTF-8 encoding\n", count);
		}
	}
#endif
  if (!g_onlyents)
  {
	if (g_wadconfigname) //If wadconfig had a name provided //seedee
	{
        std::filesystem::path wadCfgPath = get_path_to_directory_with_executable(argv) / u8"wad.cfg";

        if (g_wadcfgfile) //If provided override the default
        {
            wadCfgPath = g_wadcfgfile;
        }
        LoadWadconfig(wadCfgPath.c_str(), g_wadconfigname);
	}
	else if (g_wadcfgfile)
	{
		if (!std::filesystem::exists (g_wadcfgfile))
		{
			Error("Couldn't find wad configuration file '%s'\n", g_wadcfgfile);
		}
		LoadWadcfgfile (g_wadcfgfile);
	}
	else
	{
		Log("Loading mapfile wad configuration by default\n");
		GetUsedWads();
	}

    if (!g_bWadAutoDetect)
    {
        Warning("Unused textures will not be excluded\n");
    }
    DumpWadinclude();
    Log("\n");
  }

    // if onlyents, just grab the entites and resave
    if (g_onlyents)
    {
        std::filesystem::path out;
        out = g_Mapname;
        out += u8".bsp";

        LoadBSPFile(out);

        // Write it all back out again.
        WriteBSP(g_Mapname);

        end = I_FloatTime();
        LogTimeElapsed(end - start);
        return 0;
    }

    CheckForNoClip(); 

    // createbrush
            int numT = g_numthreads;
         //   g_numthreads = 1;
    NamedRunThreadsOnIndividual(g_nummapbrushes, g_estimate, CreateBrush);
    g_numthreads= numT;
    CheckFatal();


    // boundworld
    BoundWorld();

    Verbose("%5i map planes\n", g_nummapplanes);

    // Set model centers
    for (i = 0; i < g_numentities; i++) SetModelCenters (i); //NamedRunThreadsOnIndividual(g_numentities, g_estimate, SetModelCenters); //--vluzacn

    // Calc brush unions
    if ((g_BrushUnionThreshold > 0.0) && (g_BrushUnionThreshold <= 100.0))
    {
        NamedRunThreadsOnIndividual(g_nummapbrushes, g_estimate, CalculateBrushUnions);
    }

    // open hull files
    for (i = 0; i < NUM_HULLS; i++)
    {
        char            name[_MAX_PATH];

        safe_snprintf(name, _MAX_PATH, "%s.p%i", g_Mapname, i);

        out[i] = fopen(name, "w");

        if (!out[i]) 
            Error("Couldn't open %s", name);
		safe_snprintf(name, _MAX_PATH, "%s.b%i", g_Mapname, i);
		out_detailbrush[i] = fopen(name, "w");
		if (!out_detailbrush[i])
			Error("Couldn't open %s", name);
		if (g_viewsurface)
		{
			safe_snprintf (name, _MAX_PATH, "%s_surface%i.pts", g_Mapname, i);
			out_view[i] = fopen (name, "w");
			if (!out[i])
				Error ("Counldn't open %s", name);
		}
    }
	{
		FILE			*f;
		char			name[_MAX_PATH];
		safe_snprintf (name, _MAX_PATH, "%s.hsz", g_Mapname);
		f = fopen (name, "w");
		if (!f)
			Error("Couldn't open %s", name);
		float x1,y1,z1;
		float x2,y2,z2;
		for (i = 0; i < NUM_HULLS; i++)
		{
			x1 = g_hull_size[i][0][0];
			y1 = g_hull_size[i][0][1];
			z1 = g_hull_size[i][0][2];
			x2 = g_hull_size[i][1][0];
			y2 = g_hull_size[i][1][1];
			z2 = g_hull_size[i][1][2];
			fprintf (f, "%g %g %g %g %g %g\n", x1, y1, z1, x2, y2, z2);
		}
		fclose (f);
	}

    ProcessModels();

    Verbose("%5i csg faces\n", c_csgfaces);
    Verbose("%5i used faces\n", c_outfaces);
    Verbose("%5i tiny faces\n", c_tiny);
    Verbose("%5i tiny clips\n", c_tiny_clip);

    // close hull files 
    for (i = 0; i < NUM_HULLS; i++)
	{
        fclose(out[i]);
		fclose (out_detailbrush[i]);
		if (g_viewsurface)
		{
			fclose (out_view[i]);
		}
	}

    EmitPlanes();



    WriteBSP(g_Mapname);

    // Debug
    if constexpr(false) {
        Log("\n---------------------------------------\n"
            "Map Plane Usage:\n"
            "  #  normal             origin             dist   type\n"
            "    (   x,    y,    z) (   x,    y,    z) (     )\n"
            );
        for (i = 0; i < g_nummapplanes; i++)
        {
            mapplane_t* p = &g_mapplanes[i];

            Log(
            "%3i (%4.0f, %4.0f, %4.0f) (%4.0f, %4.0f, %4.0f) (%5.0f) %i\n",
            i,     
            p->normal[1], p->normal[2], p->normal[3],
            p->origin[1], p->origin[2], p->origin[3],
            p->dist,
            (int) p->type
            );
        }
        Log("---------------------------------------\n\n");
    }

    // elapsed time
    end = I_FloatTime();
    LogTimeElapsed(end - start);

		}
	}
    return 0;
}
