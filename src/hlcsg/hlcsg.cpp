#include "hlcsg.h"

#include "bsp_file_sizes.h"
#include "bspfile.h"
#include "cli_option_defaults.h"
#include "cmdlib.h"
#include "cmdlinecfg.h"
#include "filelib.h"
#include "hlcsg_settings.h"
#include "internal_types/various.h"
#include "legacy_character_encodings.h"
#include "log.h"
#include "map_entity_parser.h"
#include "threads.h"
#include "time_counter.h"
#include "utf8.h"
#include "util.h"

#include <numbers>
#include <string_view>
#include <utility>
using namespace std::literals;

/*

 NOTES

 - check map size for +/- 4k limit at load time
 - allow for multiple wad.cfg configurations per compile

*/

static FILE* out[NUM_HULLS]; // pointer to each of the hull out files (.p0,
							 // .p1, ect.)
static FILE* out_view[NUM_HULLS];
static FILE* out_detailbrush[NUM_HULLS];
static int c_tiny;
static int c_tiny_clip;
static int c_outfaces;
static int c_csgfaces;
bounding_box world_bounds;

hull_sizes g_hull_size{ standard_hull_sizes };

double g_tiny_threshold = DEFAULT_TINY_THRESHOLD;

bool g_noclip = DEFAULT_NOCLIP;			   // no clipping hull "-noclip"
bool g_onlyents = DEFAULT_ONLYENTS;		   // onlyents mode "-onlyents"
bool g_wadtextures = DEFAULT_WADTEXTURES;  // "-nowadtextures"
bool g_chart = cli_option_defaults::chart; // show chart "-chart"
bool g_skyclip = DEFAULT_SKYCLIP;		   // no sky clipping "-noskyclip"
bool g_estimate
	= cli_option_defaults::estimate;	 // progress estimates "-estimate"
bool g_info = cli_option_defaults::info; // "-info" ?
static std::filesystem::path
	g_hullfile; // external hullfile "-hullfile sdfsd"
static std::filesystem::path g_nullfile;

bool g_bUseNullTex = cli_option_defaults::nulltex; // "-nonulltex"

cliptype g_cliptype = DEFAULT_CLIPTYPE; // "-cliptype <value>"

bool g_bClipNazi = DEFAULT_CLIPNAZI; // "-noclipeconomy"

double g_scalesize = DEFAULT_SCALESIZE;
bool g_resetlog = DEFAULT_RESETLOG;
bool g_nolightopt = DEFAULT_NOLIGHTOPT;
bool g_nullifytrigger = DEFAULT_NULLIFYTRIGGER;
bool g_viewsurface = false;

// =====================================================================================
//  GetParamsFromEnt
//      parses entity keyvalues for setting information
// =====================================================================================
void GetParamsFromEnt(entity_t* mapent) {
	Log("\nCompile Settings detected from info_compile_parameters entity\n"
	);

	// verbose(choices) : "Verbose compile messages" : 0 = [ 0 : "Off" 1 :
	// "On" ]
	int verboseValue = IntForKey(mapent, u8"verbose");
	if (verboseValue == 1) {
		g_verbose = true;
	} else if (verboseValue == 0) {
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

	// texdata(string) : "Texture Data Memory" : "4096"
	int texdataValue = IntForKey(mapent, u8"texdata") * 1024;
	if (texdataValue > g_max_map_miptex) {
		g_max_map_miptex = texdataValue;
	}
	char szTmp[256];
	snprintf(szTmp, sizeof(szTmp), "%td", g_max_map_miptex);
	Log("%30s [ %-9s ]\n", "Texture Data Memory", szTmp);

	// hullfile(string) : "Custom Hullfile"
	if (has_key_value(mapent, u8"hullfile")) {
		g_hullfile = std::filesystem::path{
			value_for_key(mapent, u8"hullfile"),
			std::filesystem::path::generic_format
		};
		Log("%30s [ %-9s ]\n", "Custom Hullfile", g_hullfile.c_str());
	}

	// noclipeconomy(choices) : "Strip Uneeded Clipnodes?" : 1 = [ 1 : "Yes"
	// 0 : "No" ]
	int const noclipeconomyValue = IntForKey(mapent, u8"noclipeconomy");
	if (noclipeconomyValue == 1) {
		g_bClipNazi = true;
	} else if (noclipeconomyValue == 0) {
		g_bClipNazi = false;
	}
	Log("%30s [ %-9s ]\n",
		"Clipnode Economy Mode",
		g_bClipNazi ? "on" : "off");

	/*
	hlcsg(choices) : "HLCSG" : 1 =
	[
		1 : "Normal"
		2 : "Onlyents"
		0 : "Off"
	]
	*/
	int const hlcsgValue = IntForKey(mapent, u8"hlcsg");
	g_onlyents = false;
	if (hlcsgValue == 2) {
		g_onlyents = true;
	} else if (hlcsgValue == 0) {
		Fatal(
			assume_TOOL_CANCEL,
			"%s was set to \"Off\" (0) in info_compile_parameters entity, execution cancelled",
			(char const *) g_Program.data()
		);
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
	if (IntForKey(mapent, u8"nocliphull") == 1) {
		g_noclip = true;
	} else {
		g_noclip = false;
	}
	Log("%30s [ %-9s ]\n",
		"Clipping Hull Generation",
		g_noclip ? "off" : "on");

	switch (IntForKey(mapent, u8"cliptype")) {
		// 0 was "smallest" (option that has been removed)
		case 1:
			g_cliptype = clip_normalized;
			break;
		// 2 was "simple" (option that has been removed)
		case 3: // 3 has always been "precise"
			g_cliptype = clip_precise;
			break;
		case 4:
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
	if (IntForKey(mapent, u8"noskyclip") == 1) {
		g_skyclip = false;
	} else {
		g_skyclip = true;
	}
	Log("%30s [ %-9s ]\n",
		"Sky brush clip generation",
		g_skyclip ? "on" : "off");

	///////////////
	Log("\n");
}

// =====================================================================================
//  NewFaceFromFace
//      Duplicates the non point information of a face, used by SplitFace
// =====================================================================================
bface_t NewFaceFromFace(bface_t const & in) {
	bface_t newf{};
	newf.contents = in.contents;
	newf.texinfo = in.texinfo;
	newf.planenum = in.planenum;
	newf.plane = in.plane;
	newf.backcontents = in.backcontents;

	return newf;
}

static void WriteFace(
	int const hull, bface_t const * const f, detail_level detailLevel
) {
	ThreadLock();
	if (!hull) {
		c_csgfaces++;
	}

	// .p0 format
	accurate_winding const & w = f->w;

	// plane summary
	fprintf(
		out[hull],
		"%i %i %i %i %zu\n",
		detailLevel,
		f->planenum,
		f->texinfo,
		std::to_underlying(f->contents),
		w.size()
	);

	// for each of the points on the face
	for (std::size_t i = 0; i < w.size(); i++) {
		// write the co-ords
		fprintf(
			out[hull],
			"%5.8f %5.8f %5.8f\n",
			w.point(i)[0],
			w.point(i)[1],
			w.point(i)[2]
		);
	}

	// put in an extra line break
	fprintf(out[hull], "\n");
	if (g_viewsurface) {
		static bool side = false;
		side = !side;
		if (side) {
			double3_array center = w.getCenter();
			double3_array center2{ vector_add(center, f->plane->normal) };
			fprintf(
				out_view[hull],
				"%5.2f %5.2f %5.2f\n",
				center2[0],
				center2[1],
				center2[2]
			);
			for (std::size_t i = 0; i < w.size(); i++) {
				double3_array const & p1{ w.point(i) };
				double3_array const & p2{ w.point((i + 1) % w.size()) };

				fprintf(
					out_view[hull],
					"%5.2f %5.2f %5.2f\n",
					center[0],
					center[1],
					center[2]
				);
				fprintf(
					out_view[hull],
					"%5.2f %5.2f %5.2f\n",
					p1[0],
					p1[1],
					p1[2]
				);
				fprintf(
					out_view[hull],
					"%5.2f %5.2f %5.2f\n",
					p2[0],
					p2[1],
					p2[2]
				);
			}
			fprintf(
				out_view[hull],
				"%5.2f %5.2f %5.2f\n",
				center[0],
				center[1],
				center[2]
			);
			fprintf(
				out_view[hull],
				"%5.2f %5.2f %5.2f\n",
				center2[0],
				center2[1],
				center2[2]
			);
		}
	}

	ThreadUnlock();
}

static void WriteDetailBrush(int hull, std::vector<bface_t> const & faces) {
	ThreadLock();
	fprintf(out_detailbrush[hull], "0\n");
	for (bface_t const & f : faces) {
		accurate_winding const & w{ f.w };
		fprintf(out_detailbrush[hull], "%i %zu\n", f.planenum, w.size());
		for (int i = 0; i < w.size(); i++) {
			fprintf(
				out_detailbrush[hull],
				"%5.8f %5.8f %5.8f\n",
				w.point(i)[0],
				w.point(i)[1],
				w.point(i)[2]
			);
		}
	}
	fprintf(out_detailbrush[hull], "-1 -1\n");
	ThreadUnlock();
}

// =====================================================================================
//  SaveOutside
//      The faces remaining on the outside list are final polygons.  Write
//      them to the output file. Passable contents (water, lava, etc) will
//      generate a mirrored copy of the face to be seen from the inside.
// =====================================================================================
static void SaveOutside(
	csg_brush& b,
	int hull,
	std::vector<bface_t>& outside,
	contents_t mirrorcontents
) {
	for (bface_t& f : outside) {
		contents_t frontcontents, backcontents;
		texinfo_count const texinfo{ f.texinfo };
		wad_texture_name const texname{
			GetTextureByNumber_CSG(texinfo).value_or(wad_texture_name{})
		};
		frontcontents = f.contents;
		if (mirrorcontents == contents_t::TOEMPTY) {
			backcontents = f.backcontents;
		} else {
			backcontents = mirrorcontents;
		}
		if (frontcontents == contents_t::TOEMPTY) {
			frontcontents = contents_t::EMPTY;
		}
		if (backcontents == contents_t::TOEMPTY) {
			backcontents = contents_t::EMPTY;
		}

		bool backnull = false;
		bool frontnull = false;
		if (mirrorcontents == contents_t::TOEMPTY) {
			// SKIP and HINT are special textures for hlbsp so they should
			// be kept
			bool const specialTextureForHlbsp = texname.is_skip()
				|| texname.is_any_hint();
			backnull = !specialTextureForHlbsp;
		}
		if (texname.marks_discardable_faces()
			&& frontcontents != backcontents) {
			// NOT actually discardable, so remove BEVELHINT/SOLIDHINT
			// texture name and behave like NULL
			frontnull = backnull = true;
		}
		if (b.entitynum != 0 && texname.is_any_liquid()) {
			backnull = true; // strip water face on one side
		}

		f.contents = frontcontents;
		f.texinfo = frontnull ? no_texinfo : texinfo;
		if (f.w.getArea() < g_tiny_threshold) {
			c_tiny++;
			Verbose(
				"Entity %i, Brush %i: tiny fragment\n",
				b.originalentitynum,
				b.originalbrushnum
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
		if (!hull) {
			texinfo_count texinfo = f.texinfo;
			wad_texture_name const texname{
				GetTextureByNumber_CSG(texinfo).value_or(wad_texture_name{})
			};
			if (texinfo != no_texinfo // nullified textures (nullptr, BEVEL,
									  // aaatrigger, etc.)
				&& !g_texinfo[texinfo].has_special_flag() // sky
				&& !texname.is_skip()
				&& !texname.is_any_hint(
				) // HINT and SKIP will be nullified only after hlbsp
			) {
				texinfo_t const & tex{ g_texinfo[texinfo] };

				// check for "Malformed face (%d) normal"
				float3_array texnormal = cross_product(
					tex.vecs[1].xyz, tex.vecs[0].xyz
				);
				normalize_vector(texnormal);
				if (fabs(dot_product(texnormal, f.plane->normal))
					<= NORMAL_EPSILON) {
					Warning(
						"Entity %i, Brush %i: Malformed texture alignment (texture %s): Texture axis perpendicular to face.",
						b.originalentitynum,
						b.originalbrushnum,
						texname.c_str()
					);
				}

				// check for "Bad surface extents"

				bool bad = false;
				for (double3_array const & point : f.w.points()) {
					for (int j = 0; j < 2; ++j) {
						double const val = dot_product(
											   point, tex.vecs[j].xyz
										   )
							+ tex.vecs[j].offset;
						if (val < -99999 || val > 999'999) {
							bad = true;
						}
					}
				}
				if (bad) {
					Warning(
						"Entity %i, Brush %i: Malformed texture alignment (texture %s): Bad surface extents.",
						b.originalentitynum,
						b.originalbrushnum,
						texname.c_str()
					);
				}
			}
		}

		WriteFace(hull, &f, (hull ? b.clipNodeDetailLevel : b.detailLevel));

		//              if (mirrorcontents != contents_t::SOLID)
		{
			f.planenum ^= 1;
			f.plane = &g_mapPlanes[f.planenum];
			f.contents = backcontents;
			f.texinfo = backnull ? -1 : texinfo;

			// swap point orders
			f.w.reverse_points();
			WriteFace(
				hull, &f, (hull ? b.clipNodeDetailLevel : b.detailLevel)
			);
		}
	}
	outside.clear();
}

// =====================================================================================
//  CopyFace
// =====================================================================================
bface_t CopyFace(bface_t const & f) {
	bface_t n{ NewFaceFromFace(f) };
	n.w = f.w;
	n.bounds = f.bounds;
	return n;
}

#include <ranges>

// =====================================================================================
//  CopyFaceList
// =====================================================================================
std::vector<bface_t> CopyFaceList(std::vector<bface_t> const & faceList) {
	std::vector<bface_t> out;
	out.reserve(faceList.size());
	std::ranges::copy(
		std::views::transform(faceList, CopyFace), std::back_inserter(out)
	);
	return out;
}

// =====================================================================================
//  CopyFacesToOutside
//      Make a copy of all the faces of the brush, so they can be chewed up
//      by other brushes. All of the faces start on the outside list. As
//      other brushes take bites out of the faces, the fragments are moved
//      to the inside list, so they can be freed when they are determined to
//      be completely enclosed in solid.
// =====================================================================================
static std::vector<bface_t> CopyFacesToOutside(brushhull_t const * bh) {
	std::vector<bface_t> outside;
	outside.reserve(bh->faces.size());
	for (bface_t const & f : bh->faces) {
		bface_t newf{ CopyFace(f) };
		newf.bounds = newf.w.getBounds();
		outside.emplace_back(std::move(newf));
	}

	return outside;
}

// =====================================================================================
//  CSGBrush
// =====================================================================================

static void CSGBrush(int brushnum) {
	// get entity and brush info from the given brushnum that we can work
	// with
	csg_brush& b1 = g_mapbrushes[brushnum];
	entity_t* e = &g_entities[b1.entitynum];

	// for each of the hulls
	for (int hull = 0; hull < NUM_HULLS; hull++) {
		brushhull_t* bh1 = &b1.hulls[hull];
		if (!bh1->faces.empty()
			&& (hull ? b1.clipNodeDetailLevel : b1.detailLevel)) {
			switch (b1.contents) {
				case contents_t::ORIGIN:
				case contents_t::BOUNDINGBOX:
				case contents_t::HINT:
				case contents_t::TOEMPTY:
					break;
				default:
					Error(
						"Entity %i, Brush %i: %s brushes not allowed in detail\n",
						b1.originalentitynum,
						b1.originalbrushnum,
						(char const *) ContentsToString(b1.contents).data()
					);
					break;
				case contents_t::SOLID:
					WriteDetailBrush(hull, bh1->faces);
					break;
			}
		}

		// set outside to a copy of the brush's faces
		std::vector<bface_t> outside{ CopyFacesToOutside(bh1) };
		if (b1.contents == contents_t::TOEMPTY) {
			for (bface_t& f : outside) {
				f.contents = contents_t::TOEMPTY;
				f.backcontents = contents_t::TOEMPTY;
			}
		}
		bool overwrite{ false };
		// for each brush in entity e
		for (int bn = 0; bn < e->numbrushes; bn++) {
			// see if b2 needs to clip a chunk out of b1
			if (e->firstBrush + bn == brushnum) {
				continue;
			}
			overwrite = e->firstBrush + bn > brushnum;

			csg_brush const & b2 = g_mapbrushes[e->firstBrush + bn];
			brushhull_t const & bh2 = b2.hulls[hull];

			if (b2.contents == contents_t::TOEMPTY) {
				continue;
			}
			if (hull ? (b2.clipNodeDetailLevel > b1.clipNodeDetailLevel)
					 : (std::int64_t(b2.detailLevel)
							- std::int64_t(b2.chopDown)
						> std::int64_t(b1.detailLevel)
							+ std::int64_t(b1.chopUp))) {
				continue; // You can't chop
			}
			if (b2.contents == b1.contents
				&& (hull
						? (b2.clipNodeDetailLevel != b1.clipNodeDetailLevel)
						: (b2.detailLevel != b1.detailLevel))) {
				overwrite
					= (hull ? (b2.clipNodeDetailLevel
							   < b1.clipNodeDetailLevel)
							: (b2.detailLevel < b1.detailLevel));
			}
			if (b2.contents == b1.contents && hull == 0
				&& b2.detailLevel == b1.detailLevel
				&& b2.coplanarPriority != b1.coplanarPriority) {
				overwrite = b2.coplanarPriority > b1.coplanarPriority;
			}

			if (bh2.faces.empty()) {
				continue; // brush isn't in this hull
			}

			// check brush bounding box first
			// TODO: use boundingbox method instead
			if (test_disjoint(bh1->bounds, bh2.bounds)) {
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
				if (test_disjoint(bh2.bounds, f.bounds)) {
					// This face doesn't intersect brush2's bbox
					outside.emplace_back(std::move(f));
					continue;
				}
				if (hull ? (b2.clipNodeDetailLevel > b1.clipNodeDetailLevel)
						 : (b2.detailLevel > b1.detailLevel)) {
					wad_texture_name const texname{
						GetTextureByNumber_CSG(f.texinfo).value_or(
							wad_texture_name{}
						)
					};
					if (f.texinfo == no_texinfo || texname.is_skip()
						|| texname.is_any_hint()) {
						// should not nullify the fragment inside detail
						// brush
						outside.emplace_back(std::move(f));
						continue;
					}
				}

				// throw pieces on the front sides of the planes
				// into the outside list, return the remains on the inside
				// find the fragment inside brush2
				accurate_winding w{ f.w };
				for (bface_t const & f2 : bh2.faces) {
					if (f.planenum == f2.planenum) {
						if (!overwrite) {
							// face plane is outside brush2
							w.clear();
							break;
						} else {
							continue;
						}
					}
					if (f.planenum == (f2.planenum ^ 1)) {
						continue;
					}

					accurate_winding backWinding;
					accurate_winding frontWinding;
					w.clip(*f2.plane, backWinding, frontWinding);
					w = std::move(backWinding);

					if (w.empty()) {
						break;
					}
				}
				// do real split
				if (w) {
					bool skip{ false };
					for (bface_t const & f2 : bh2.faces) {
						if (f.planenum == f2.planenum
							|| f.planenum == (f2.planenum ^ 1)) {
							continue;
						}
						int valid = 0;
						for (int x = 0; x < w.size(); x++) {
							double const dist
								= dot_product(w.point(x), f2.plane->normal)
								- f2.plane->dist;
							if (dist >= -ON_EPSILON * 4) // only estimate
							{
								++valid;
							}
						}
						if (valid >= 2) { // this splitplane forms an edge

							accurate_winding backWinding;
							accurate_winding frontWinding;
							f.w.clip(*f2.plane, backWinding, frontWinding);

							if (frontWinding) {
								if (backWinding) {
									f.w = std::move(backWinding);
								} else {
									f.w.clear();
								}
							}
							if (frontWinding) {
								bface_t front{ NewFaceFromFace(f) };
								front.w = std::move(frontWinding);
								front.bounds = front.w.getBounds();

								outside.emplace_back(std::move(front));
							}

							if (f.w) {
								f.bounds = f.w.getBounds();
							} else {
								skip = true;
								break;
							}
						}
					}
					if (skip) {
						continue;
					}
				} else {
					outside.emplace_back(std::move(f));
					continue;
				}

				double area = f.w.getArea();
				if (area < g_tiny_threshold) {
					Verbose(
						"Entity %i, Brush %i: tiny penetration\n",
						b1.originalentitynum,
						b1.originalbrushnum
					);
					c_tiny_clip++;
					continue;
				}
				// there is one convex fragment of the original
				// face left inside brush2

				if (hull ? (b2.clipNodeDetailLevel > b1.clipNodeDetailLevel)
						 : (b2.detailLevel > b1.detailLevel
						   )) { // don't chop or set contents, only nullify
					f.texinfo = -1;
					outside.emplace_back(std::move(f));
					continue;
				}
				if ((hull ? b2.clipNodeDetailLevel < b1.clipNodeDetailLevel
						  : b2.detailLevel < b1.detailLevel)
					&& b2.contents == contents_t::SOLID) {
					// Real solid
					continue;
				}
				if (b1.contents == contents_t::TOEMPTY) {
					bool onfront = true, onback = true;
					for (bface_t const & f2 : bh2.faces) {
						if (f.planenum == (f2.planenum ^ 1)) {
							onback = false;
						}
						if (f.planenum == f2.planenum) {
							onfront = false;
						}
					}
					if (onfront && f.contents < b2.contents) {
						f.contents = b2.contents;
					}
					if (onback && f.backcontents < b2.contents) {
						f.backcontents = b2.contents;
					}
					if (!(f.contents == contents_t::SOLID
						  && f.backcontents == contents_t::SOLID
						  && !GetTextureByNumber_CSG(f.texinfo)
								  .value_or(wad_texture_name{})
								  .is_solid_hint()
						  && !GetTextureByNumber_CSG(f.texinfo)
								  .value_or(wad_texture_name{})
								  .is_bevel_hint())) {
						outside.emplace_back(std::move(f));
					}
					continue;
				}
				if (b1.contents > b2.contents
					|| b1.contents == b2.contents
						&& GetTextureByNumber_CSG(f.texinfo)
							   .value_or(wad_texture_name{})
							   .is_solid_hint()
					|| b1.contents == b2.contents
						&& GetTextureByNumber_CSG(f.texinfo)
							   .value_or(wad_texture_name{})
							   .is_bevel_hint()) {
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
static void EmitPlanes() {
	mapplane_t* mp;

	g_numplanes = g_mapPlanes.size();
	mp = g_mapPlanes.data();
	dplane_t* dp = g_dplanes.data();
	{
		std::filesystem::path planeFilePath{
			path_to_temp_file_with_extension(g_Mapname, u8".pln")
		};
		FILE* planeout = fopen(planeFilePath.c_str(), "wb");
		if (!planeout) {
			Error("Couldn't open %s", planeFilePath.c_str());
		}
		SafeWrite(
			planeout,
			g_mapPlanes.data(),
			g_mapPlanes.size() * sizeof(mapplane_t)
		);
		fclose(planeout);
	}
	for (int i = 0; i < g_mapPlanes.size(); i++, mp++, dp++) {
		// if (!(mp->redundant))
		//{
		//     Log("EmitPlanes: plane %i non redundant\n", i);
		dp->normal = to_float3(mp->normal);
		dp->dist = mp->dist;
		dp->type = mp->type;
		// }
		// else
		// {
		//     Log("EmitPlanes: plane %i redundant\n", i);
		// }
	}
}

// =====================================================================================
//  SetModelNumbers
//      blah
// =====================================================================================
static void SetModelNumbers() {
	int i;
	int models;
	char value[10];

	models = 1;
	for (i = 1; i < g_numentities; i++) {
		if (g_entities[i].numbrushes) {
			safe_snprintf(value, sizeof(value), "*%i", models);
			models++;
			set_key_value(
				&g_entities[i], u8"model", (char8_t const *) value
			);
		}
	}
}

void ReuseModel() {
	int i;
	for (i = g_numentities - 1; i >= 1;
		 i--) // so it won't affect the remaining entities in the loop when
			  // we move this entity backward
	{
		std::u8string_view name = value_for_key(
			&g_entities[i], u8"zhlt_usemodel"
		);
		if (name.empty()) {
			continue;
		}
		int j;
		for (j = 1; j < g_numentities; j++) {
			if (has_key_value(&g_entities[j], u8"zhlt_usemodel")) {
				continue;
			}
			if (key_value_is(&g_entities[j], u8"targetname", name)) {
				break;
			}
		}
		if (j == g_numentities) {
			if (strings_equal_with_ascii_case_insensitivity(
					name, u8"null"
				)) {
				DeleteKey(&g_entities[i], u8"model");
				continue;
			}
			Error(
				"zhlt_usemodel: can not find target entity '%s', or that entity is also using 'zhlt_usemodel'.\n",
				(char const *) name.data()
			);
		}
		set_key_value(
			&g_entities[i],
			u8"model",
			value_for_key(&g_entities[j], u8"model")
		);
		if (j > i) {
			// move this entity forward
			// to prevent precache error in case of .mdl/.spr and wrong
			// result of EntityForModel in case of map model
			entity_t tmp{ std::move(g_entities[i]) };
			std::move(
				&g_entities[i + 1],
				&g_entities[i + 1] + ((j + 1) - (i + 1)),
				&g_entities[i]
			);
			g_entities[j] = std::move(tmp);
		}
	}
}

// =====================================================================================
//  SetLightStyles
// =====================================================================================
constexpr std::size_t MAX_SWITCHED_LIGHTS = 32;

static void SetLightStyles() {
	int stylenum;
	std::u8string_view t;
	entity_t* e;
	int i, j;
	char value[10];
	std::array<std::u8string, MAX_SWITCHED_LIGHTS> lighttargets;

	bool newtexlight = false;

	// any light that is controlled (has a targetname)
	// must have a unique style number generated for it

	stylenum = 0;
	for (i = 1; i < g_numentities; i++) {
		e = &g_entities[i];

		t = value_for_key(e, u8"classname");
		if (!t.starts_with(u8"light")) {
			// LRC:
			//  if it's not a normal light entity, allocate it a new style
			//  if necessary.
			t = value_for_key(e, u8"style");
			switch (atoi((char const *) t.data())) {
				case 0: // not a light, no style, generally pretty boring
					continue;
				case -1: // normal switchable texlight
					safe_snprintf(
						value, sizeof(value), "%i", 32 + stylenum
					);
					set_key_value(e, u8"style", (char8_t const *) value);
					stylenum++;
					continue;
				case -2: // backwards switchable texlight
					safe_snprintf(
						value, sizeof(value), "%i", -(32 + stylenum)
					);
					set_key_value(e, u8"style", (char8_t const *) value);
					stylenum++;
					continue;
				case -3: // (HACK) a piggyback texlight: switched on and off
						 // by triggering a real light that has the same
						 // name
					set_key_value(
						e, u8"style", u8"0"
					); // just in case the level designer didn't give it a
					   // name
					newtexlight = true;
					// don't 'continue', fall out
			}
			// LRC (ends)
		}
		t = value_for_key(e, u8"targetname");
		if (has_key_value(e, u8"zhlt_usestyle")) {
			t = value_for_key(e, u8"zhlt_usestyle");
			if (strings_equal_with_ascii_case_insensitivity(t, u8"null")) {
				t = u8"";
			}
		}
		if (t.empty()) {
			continue;
		}

		// find this targetname
		for (j = 0; j < stylenum; j++) {
			if (lighttargets[j] == t) {
				break;
			}
		}
		if (j == stylenum) {
			hlassume(
				stylenum < MAX_SWITCHED_LIGHTS, assume_MAX_SWITCHED_LIGHTS
			);
			lighttargets[j] = t;
			stylenum++;
		}
		safe_snprintf(value, sizeof(value), "%i", 32 + j);
		set_key_value(e, u8"style", (char8_t const *) value);
	}
}

static float3_array angles_for_vector(float3_array const & vector) {
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
				angles[1] = 2 * std::atan(y / (1 + x))
					/ std::numbers::pi_v<float> * 180;
			} else {
				angles[1] = 2 * std::atan(y / (1 + x))
						/ std::numbers::pi_v<float> * 180
					+ 360;
			}
		}
	}
	return angles;
}

static void UnparseEntities() {
	char line[4096]; // TODO: Replace. 4096 is arbitrary

	char8_t* buf = g_dentdata.data();
	char8_t* end = buf;
	*end = 0;

	for (int i = 0; i < g_numentities; i++) {
		entity_t* mapent = &g_entities[i];
		if (classname_is(mapent, u8"info_sunlight")
			|| classname_is(mapent, u8"light_environment")) {
			float3_array vec;
			{
				vec = get_float3_for_key(*mapent, u8"angles");
				float pitch = float_for_key(*mapent, u8"pitch");
				if (pitch) {
					vec[0] = pitch;
				}

				std::u8string_view target = value_for_key(
					mapent, u8"target"
				);
				if (!target.empty()) {
					std::optional<std::reference_wrapper<entity_t>>
						maybeTargetEnt = find_target_entity(target);
					if (maybeTargetEnt) {
						float3_array originA = get_float3_for_key(
							*mapent, u8"origin"
						);
						float3_array originB = get_float3_for_key(
							maybeTargetEnt.value().get(), u8"origin"
						);
						float3_array const normal = vector_subtract(
							originB, originA
						);
						vec = angles_for_vector(normal);
					}
				}
			}
			char stmp[1024];
			safe_snprintf(stmp, 1024, "%g %g %g", vec[0], vec[1], vec[2]);
			set_key_value(mapent, u8"angles", (char8_t const *) stmp);
			DeleteKey(mapent, u8"pitch");

			if (classname_is(mapent, u8"info_sunlight")) {
				if (g_numentities == MAX_MAP_ENTITIES) {
					Error("g_numentities == MAX_MAP_ENTITIES");
				}
				entity_t* newent = &g_entities[g_numentities++];
				using std::swap;
				swap(newent->keyValues, mapent->keyValues);
				set_key_value(newent, u8"classname", u8"light_environment");
				set_key_value(newent, u8"_fake", u8"1");
			}
		}
	}
	for (int i = 0; i < g_numentities; i++) {
		entity_t* mapent = &g_entities[i];
		if (classname_is(mapent, u8"light_shadow")
			|| classname_is(mapent, u8"light_bounce")) {
			set_key_value(
				mapent, u8"convertfrom", ValueForKey(mapent, u8"classname")
			);
			set_key_value(
				mapent,
				u8"classname",
				(has_key_value(mapent, u8"convertto")
					 ? value_for_key(mapent, u8"convertto")
					 : u8"light"sv)
			);
			DeleteKey(mapent, u8"convertto");
		}
	}
	// ugly code
	for (int i = 0; i < g_numentities; i++) {
		entity_t* mapent = &g_entities[i];
		if (classname_is(mapent, u8"light_surface")) {
			if (key_value_is_empty(mapent, u8"_tex")) {
				set_key_value(mapent, u8"_tex", u8"                ");
			}
			std::u8string_view newclassname = value_for_key(
				mapent, u8"convertto"
			);
			if (newclassname.empty()) {
				set_key_value(mapent, u8"classname", u8"light");
			} else if (!newclassname.starts_with(u8"light")) {
				Error(
					"New classname for 'light_surface' should begin with 'light' not '%s'.\n",
					(char const *) newclassname.data()
				);
			} else {
				set_key_value(mapent, u8"classname", newclassname);
			}
			DeleteKey(mapent, u8"convertto");
		}
	}
	if (!g_nolightopt) {
		int count = 0;
		std::unique_ptr<bool[]> lightneedcompare = std::make_unique<bool[]>(
			g_numentities
		);
		hlassume(lightneedcompare != nullptr, assume_NoMemory);
		for (int i = g_numentities - 1; i > -1; i--) {
			entity_t* ent = &g_entities[i];
			std::u8string_view const classname = get_classname(*ent);
			std::u8string_view const targetname = value_for_key(
				ent, u8"targetname"
			);
			int style = IntForKey(ent, u8"style");
			if (!targetname[0]
				|| classname != u8"light" && classname != u8"light_spot"
					&& classname != u8"light_environment") {
				continue;
			}
			int j;
			for (j = i + 1; j < g_numentities; j++) {
				if (!lightneedcompare[j]) {
					continue;
				}
				entity_t* ent2 = &g_entities[j];
				std::u8string_view const targetname2 = value_for_key(
					ent2, u8"targetname"
				);
				int style2 = IntForKey(ent2, u8"style");
				if (style == style2 && targetname == targetname2) {
					break;
				}
			}
			if (j < g_numentities) {
				DeleteKey(ent, u8"targetname");
				count++;
			} else {
				lightneedcompare[i] = true;
			}
		}
		if (count > 0) {
			Log("%d redundant named lights optimized.\n", count);
		}
	}
	for (int i = 0; i < g_numentities; i++) {
		if (g_entities[i].keyValues.empty()) {
			// Ent got removed
			continue;
		}

		strcat((char*) end, (char const *) "{\n");
		end += 2;

		for (entity_key_value const & kv : g_entities[i].keyValues) {
			if (kv.is_removed()) {
				continue;
			}
			snprintf(
				line,
				sizeof(line),
				"\"%s\" \"%s\"\n",
				(char const *) kv.key().data(),
				(char const *) kv.value().data()
			);
			strcat((char*) end, line);
			end += strlen(line);
		}
		strcat((char*) end, (char const *) u8"}\n");
		end += 2;

		if (end > buf + MAX_MAP_ENTSTRING) {
			Error("Entity text too long");
		}
	}
	g_entdatasize = end - buf + 1;
}

// =====================================================================================
//  ConvertHintToEmtpy
// =====================================================================================
static void ConvertHintToEmpty() {
	// Convert HINT brushes to EMPTY after they have been carved by csg
	for (std::size_t i = 0; i < MAX_MAP_BRUSHES; i++) {
		if (g_mapbrushes[i].contents == contents_t::HINT) {
			g_mapbrushes[i].contents = contents_t::EMPTY;
		}
	}
}

// =====================================================================================
//  WriteBSP
// =====================================================================================
// Only used with -onlyents
void LoadWadValue() {
	std::u8string wadValue;
	map_entity_parser parser{ { g_dentdata.data(), g_entdatasize } };
	parsed_entity parsedEntity;
	if (parser.parse_entity(parsedEntity)
		!= parse_entity_outcome::entity_parsed) {
		Error("Failed to parse worldspawn");
	}
	auto wadKvIt = std::ranges::find_if(
		parsedEntity.keyValues,
		[](entity_key_value const & kv) { return kv.key() == u8"wad"; }
	);

	if (wadKvIt != parsedEntity.keyValues.end()) {
		set_key_value(&g_entities[0], std::move(*wadKvIt));
	}
}

void WriteBSP(char const * const name) {
	std::filesystem::path bspPath;
	bspPath = name;
	bspPath += u8".bsp";

	SetModelNumbers();
	ReuseModel();
	SetLightStyles();

	if (!g_onlyents) {
		WriteMiptex(bspPath);
	}
	if (g_onlyents) {
		LoadWadValue();
	}

	UnparseEntities();
	ConvertHintToEmpty(); // this is ridiculous. --vluzacn
	if (g_chart) {
		print_bsp_file_sizes(bspGlobals);
	}
	WriteBSPFile(bspPath);
}

unsigned int BrushClipHullsDiscarded = 0;
unsigned int ClipNodesDiscarded = 0;

static void MarkEntForNoclip(entity_t* ent) {
	csg_brush* b;

	for (std::size_t i = ent->firstBrush;
		 i < ent->firstBrush + ent->numbrushes;
		 ++i) {
		b = &g_mapbrushes[i];
		b->noclip = true;

		BrushClipHullsDiscarded++;
		ClipNodesDiscarded += b->numSides;
	}
}

// =====================================================================================
//  CheckForNoClip
//      marks the noclip flag on any brushes that dont need clipnode
//      generation, eg. func_illusionaries
// =====================================================================================
static void CheckForNoClip() {
	int i;
	entity_t* ent;

	int spawnflags;
	int count = 0;

	if (!g_bClipNazi) {
		return; // NO CLIP FOR YOU!!!
	}

	for (i = 0; i < g_numentities; i++) {
		if (!g_entities[i].numbrushes) {
			continue; // not a model
		}

		if (!i) {
			continue; // dont waste our time with worldspawn
		}

		ent = &g_entities[i];

		std::u8string_view entclassname = get_classname(*ent);
		spawnflags = atoi((char const *) ValueForKey(ent, u8"spawnflags"));
		int skin = IntForKey(ent, u8"skin");

		if (skin == -16) {
			continue;
		}
		if (entclassname == u8"env_bubbles"
			|| entclassname == u8"func_illusionary"
			|| ((spawnflags & 8)
				&& (entclassname == u8"func_train"
					|| entclassname == u8"func_door"
					|| entclassname == u8"func_water"
					|| entclassname == u8"func_door_rotating"
					|| entclassname == u8"func_pendulum"
					|| entclassname == u8"func_train"
					|| entclassname == u8"func_tracktrain"
					|| entclassname == u8"func_vehicle"))
			|| (skin != 0)
				&& (entclassname == u8"func_door"
					|| entclassname == u8"func_water")
			|| (spawnflags & 2) && (entclassname == u8"func_conveyor")
			|| (spawnflags & 1) && (entclassname == u8"func_rot_button")
			|| (spawnflags & 64) && (entclassname == u8"func_rotating")) {
			MarkEntForNoclip(ent);
			count++;
		}
	}

	Log("%i entities discarded from clipping hulls\n", count);
}

// =====================================================================================
//  ProcessModels
// =====================================================================================

static void ProcessModels() {
	contents_t contents;
	csg_brush temp;

	std::vector<csg_brush> temps;

	for (int i = 0; i < g_numentities; i++) {
		if (!g_entities[i].numbrushes) { // only models
			continue;
		}

		// sort the contents down so stone bites water, etc
		int first = g_entities[i].firstBrush;
		if (temps.size() < g_entities[i].numbrushes) {
			temps.resize(g_entities[i].numbrushes);
		}
		for (int j = 0; j < g_entities[i].numbrushes; j++) {
			temps[j] = g_mapbrushes[first + j];
		}
		contents_t placedcontents;
		bool b_placedcontents = false;
		for (brush_count placed = 0; placed < g_entities[i].numbrushes;) {
			bool b_contents = false;
			for (int j = 0; j < g_entities[i].numbrushes; j++) {
				csg_brush* brush = &temps[j];
				if (b_placedcontents && brush->contents <= placedcontents) {
					continue;
				}
				if (b_contents && brush->contents >= contents) {
					continue;
				}
				b_contents = true;
				contents = brush->contents;
			}
			for (int j = 0; j < g_entities[i].numbrushes; j++) {
				csg_brush* brush = &temps[j];
				if (brush->contents == contents) {
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
			NamedRunThreadsOnIndividual(
				g_entities[i].numbrushes, g_estimate, CSGBrush
			);
			g_numthreads = numT;
			CheckFatal();
		} else {
			for (int j = 0; j < g_entities[i].numbrushes; j++) {
				CSGBrush(first + j);
			}
		}

		// write end of model marker
		for (int j = 0; j < NUM_HULLS; j++) {
			fprintf(out[j], "-1 -1 -1 -1 -1\n");
			fprintf(out_detailbrush[j], "-1\n");
		}
	}
}

// =====================================================================================
//  SetModelCenters
// =====================================================================================
static void SetModelCenters(int entitynum) {
	int i;
	int last;
	char string[4096]; // TODO: Replace. 4096 is arbitrary
	entity_t* e = &g_entities[entitynum];
	bounding_box bounds;

	if ((entitynum == 0)
		|| (e->numbrushes == 0)) { // skip worldspawn and point entities
		return;
	}

	if (!has_key_value(
			e, u8"light_origin"
		)) { // skip if its not a zhlt_flags light_origin
		return;
	}

	for (i = e->firstBrush, last = e->firstBrush + e->numbrushes; i < last;
		 i++) {
		if (g_mapbrushes[i].contents != contents_t::ORIGIN
			&& g_mapbrushes[i].contents != contents_t::BOUNDINGBOX) {
			add_to_bounding_box(bounds, g_mapbrushes[i].hulls[0].bounds);
		}
	}

	double3_array center = midpoint_between(bounds.mins, bounds.maxs);

	safe_snprintf(
		string,
		sizeof(string),
		"%i %i %i",
		(int) center[0],
		(int) center[1],
		(int) center[2]
	);
	set_key_value(e, u8"model_center", (char8_t const *) string);
}

//
// =====================================================================================
//

// =====================================================================================
//  BoundWorld
// =====================================================================================
static void BoundWorld() {
	int i;
	brushhull_t* h;

	world_bounds = empty_bounding_box;

	for (i = 0; i < g_nummapbrushes; i++) {
		h = &g_mapbrushes[i].hulls[0];
		if (h->faces.empty()) {
			continue;
		}
		add_to_bounding_box(world_bounds, h->bounds);
	}

	Verbose(
		"World bounds: (%i %i %i) to (%i %i %i)\n",
		(int) world_bounds.mins[0],
		(int) world_bounds.mins[1],
		(int) world_bounds.mins[2],
		(int) world_bounds.maxs[0],
		(int) world_bounds.maxs[1],
		(int) world_bounds.maxs[2]
	);
}

// =====================================================================================
//  Usage
//      prints out usage sheet
// =====================================================================================
static void Usage() {
	hlcsg_settings const defaultSettings{};

	Banner(); // TODO: Call banner from main CSG process?

	Log("\n-= %s Options =-\n\n", (char const *) g_Program.data());
	Log("    -nowadtextures   : Include all used textures into bsp\n");
	Log("    -wadinclude file : Include specific wad or directory into bsp\n"
	);
	Log("    -noclip          : don't create clipping hull\n");
	Log("    -legacy-map-encoding : Legacy character encoding such as %s to use if the .map is not in UTF-8\n",
		(char const *)
			code_name_of_legacy_encoding(defaultSettings.legacyMapEncoding)
				.data());
	Log("    -force-legacy-map-encoding : Always use the -legacy-map-encoding character encoding for the .map instead of UTF-8\n"
	);

	Log("    -clipeconomy     : turn clipnode economy mode on\n");

	Log("    -cliptype value  : set to legacy, normalized, or precise (default)\n"
	);
	Log("    -nullfile file   : specify list of entities to retexture with NULL\n"
	);

	Log("    -onlyents        : do an entity update from .map to .bsp\n");
	Log("    -noskyclip       : disable automatic clipping of SKY brushes\n"
	);
	Log("    -tiny #          : minmum brush face surface area before it is discarded\n"
	);
	Log("    -hullfile file   : Reads in custom collision hull dimensions\n"
	);
	Log("    -wadcfgfile file : wad configuration file\n");
	Log("    -wadconfig name  : use the old wad configuration approach (select a group from wad.cfg)\n"
	);
	Log("    -texdata #       : Alter maximum texture memory limit (in kb)\n"
	);
	Log("    -chart           : display bsp statitics\n");
	Log("    -low | -high     : run program an altered priority level\n");
	Log("    -nolog           : don't generate the compile logfiles\n");
	Log("    -noresetlog      : Do not delete log file\n");
	Log("    -threads #       : manually specify the number of threads to run\n"
	);
#ifdef SYSTEM_WIN32
	Log("    -estimate        : display estimated time during compile\n");
#endif
#ifdef SYSTEM_POSIX
	Log("    -noestimate      : do not display continuous compile time estimates\n"
	);
#endif
	Log("    -verbose         : compile with verbose messages\n");
	Log("    -noinfo          : Do not show tool configuration information\n"
	);

	Log("    -nonulltex       : Turns off null texture stripping\n");
	Log("    -nonullifytrigger: don't remove 'aaatrigger' texture\n");

	Log("    -nolightopt      : don't optimize engine light entities\n");

	Log("    -dev %s : compile with developer logging\n\n",
		(char const *) developer_level_options.data());

	Log("    -scale #         : Scale the world. Use at your own risk.\n");
	Log("    -worldextent #   : Extend map geometry limits beyond +/-32768.\n"
	);
	Log("    mapfile          : The mapfile to compile\n\n");

	exit(1);
}

// =====================================================================================
//  DumpWadinclude
//      prints out the wadinclude list
// =====================================================================================
static void DumpWadinclude() {
	Log("Wadinclude list\n");
	Log("---------------\n");
	WadInclude_i it;

	for (it = g_WadInclude.begin(); it != g_WadInclude.end(); it++) {
		Log("%s\n", (char const *) it->c_str());
	}
	Log("---------------\n\n");
}

// =====================================================================================
//  Settings
//      prints out settings sheet
// =====================================================================================
static void
Settings(bsp_data const & bspData, hlcsg_settings const & settings) {
	hlcsg_settings const defaultSettings{};
	char const * tmp;

	if (!g_info) {
		return;
	}

	Log("\nCurrent %s Settings\n", (char const *) g_Program.data());
	Log("Name                 |  Setting  |  Default\n"
		"---------------------|-----------|-------------------------\n");

	// ZHLT Common Settings
	Log("threads             [ %7td ] [  Varies ]\n", g_numthreads);
	Log("verbose               [ %7s ] [ %7s ]\n",
		g_verbose ? "on" : "off",
		cli_option_defaults::verbose ? "on" : "off");
	Log("log                   [ %7s ] [ %7s ]\n",
		g_log ? "on" : "off",
		cli_option_defaults::log ? "on" : "off");
	Log("reset logfile         [ %7s ] [ %7s ]\n",
		g_resetlog ? "on" : "off",
		DEFAULT_RESETLOG ? "on" : "off");

	Log("developer             [ %7s ] [ %7s ]\n",
		(char const *) name_of_developer_level(g_developer).data(),
		(char const *)
			name_of_developer_level(cli_option_defaults::developer)
				.data());
	Log("chart                 [ %7s ] [ %7s ]\n",
		g_chart ? "on" : "off",
		cli_option_defaults::chart ? "on" : "off");
	Log("estimate              [ %7s ] [ %7s ]\n",
		g_estimate ? "on" : "off",
		cli_option_defaults::estimate ? "on" : "off");
	Log("max texture memory    [ %7td ] [ %7td ]\n",
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
	Log("priority              [ %7s ] [ %7s ]\n", tmp, "Normal");
	Log("\n");

	// HLCSG Specific Settings

	Log(".map legacy encoding  [ %7s ] [ %7s ]\n",
		(char const *)
			human_name_of_legacy_encoding(settings.legacyMapEncoding)
				.data(),
		(char const *)
			human_name_of_legacy_encoding(defaultSettings.legacyMapEncoding)
				.data());
	Log("force .map legacy enc.[ %7s ] [ %7s ]\n",
		settings.forceLegacyMapEncoding ? "on" : "off",
		defaultSettings.forceLegacyMapEncoding ? "on" : "off");
	Log("noclip                [ %7s ] [ %7s ]\n",
		g_noclip ? "on" : "off",
		DEFAULT_NOCLIP ? "on" : "off");

	Log("null texture stripping[ %7s ] [ %7s ]\n",
		g_bUseNullTex ? "on" : "off",
		cli_option_defaults::nulltex ? "on" : "off");

	Log("clipnode economy mode [ %7s ] [ %7s ]\n",
		g_bClipNazi ? "on" : "off",
		DEFAULT_CLIPNAZI ? "on" : "off");

	Log("clip hull type        [ %7s ] [ %7s ]\n",
		GetClipTypeString(g_cliptype),
		GetClipTypeString(DEFAULT_CLIPTYPE));

	Log("onlyents              [ %7s ] [ %7s ]\n",
		g_onlyents ? "on" : "off",
		DEFAULT_ONLYENTS ? "on" : "off");
	Log("wadtextures           [ %7s ] [ %7s ]\n",
		g_wadtextures ? "on" : "off",
		DEFAULT_WADTEXTURES ? "on" : "off");
	Log("skyclip               [ %7s ] [ %7s ]\n",
		g_skyclip ? "on" : "off",
		DEFAULT_SKYCLIP ? "on" : "off");
	Log("hullfile              [ %7s ] [ %7s ]\n",
		g_hullfile.empty() ? "None" : g_hullfile.c_str(),
		"None");
	Log("nullfile              [ %7s ] [ %7s ]\n",
		g_nullfile.empty() ? "None" : g_nullfile.c_str(),
		"None");
	Log("nullify trigger       [ %7s ] [ %7s ]\n",
		g_nullifytrigger ? "on" : "off",
		DEFAULT_NULLIFYTRIGGER ? "on" : "off");
	// calc min surface area
	{
		char tiny_penetration[10];
		char default_tiny_penetration[10];

		safe_snprintf(
			tiny_penetration,
			sizeof(tiny_penetration),
			"%3.3f",
			g_tiny_threshold
		);
		safe_snprintf(
			default_tiny_penetration,
			sizeof(default_tiny_penetration),
			"%3.3f",
			DEFAULT_TINY_THRESHOLD
		);
		Log("min surface area      [ %7s ] [ %7s ]\n",
			tiny_penetration,
			default_tiny_penetration);
	}

	{
		char buf1[10];
		char buf2[10];

		if (g_scalesize > 0) {
			safe_snprintf(buf1, sizeof(buf1), "%3.3f", g_scalesize);
		} else {
			strcpy(buf1, "None");
		}
		if (DEFAULT_SCALESIZE > 0) {
			safe_snprintf(buf2, sizeof(buf2), "%3.3f", DEFAULT_SCALESIZE);
		} else {
			strcpy(buf2, "None");
		}
		Log("map scaling           [ %7s ] [ %7s ]\n", buf1, buf2);
	}
	Log("light name optimize   [ %7s ] [ %7s ]\n",
		!g_nolightopt ? "on" : "off",
		!DEFAULT_NOLIGHTOPT ? "on" : "off");
	Log("world extent          [ %7d ] [ %7d ]\n",
		bspData.worldExtent,
		65536);

	Log("\n");
}

// =====================================================================================
//  CSGCleanup
// =====================================================================================
void CSGCleanup() {
	// Log("CSGCleanup\n");
	FreeWadPaths();
}

// =====================================================================================
//  Main
//      Oh, come on.
// =====================================================================================
int main(int const argc, char** argv) {
	hlcsg_settings settings{};
	bsp_data& bspData = bspGlobals;
	int i;
	std::filesystem::path sourceFilePath; // The .map
	char const * mapname_from_arg
		= nullptr; // mapname path from passed argvar

	g_Program = u8"HLCSG"; // Constructive Solid Geometry

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

			// Hard coded list of -wadinclude files, used for HINT texture
			// brushes so lazy mapmakers wont cause beta testers (or
			// possibly end users) to get a wad error on hlt.wad etc.
			g_WadInclude.push_back(u8"hlt.wad");
			g_WadInclude.push_back(u8"sdhlt.wad"); // seedee's HLT
			g_WadInclude.push_back(u8"zhlt.wad");  // Zoner's HLT

			InitDefaultHulls();

			// detect argv
			for (i = 1; i < argc; i++) {
				if (strings_equal_with_ascii_case_insensitivity(
						argv[i], u8"-threads"
					)) {
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

				else if (strings_equal_with_ascii_case_insensitivity(
							 argv[i], u8"-worldextent"
						 )) {
					bspData.worldExtent = atoi(argv[++i]);
				}
#ifdef SYSTEM_POSIX
				else if (strings_equal_with_ascii_case_insensitivity(
							 argv[i], u8"-noestimate"
						 )) {
					g_estimate = false;
				}
#endif

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
							   argv[i], u8"-skyclip"
						   )) {
					g_skyclip = true;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-noskyclip"
						   )) {
					g_skyclip = false;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-noclip"
						   )) {
					g_noclip = true;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-onlyents"
						   )) {
					g_onlyents = true;
				}

				else if (strings_equal_with_ascii_case_insensitivity(
							 argv[i], u8"-nonulltex"
						 )) {
					g_bUseNullTex = false;
				}

				else if (strings_equal_with_ascii_case_insensitivity(
							 argv[i], u8"-clipeconomy"
						 )) {
					g_bClipNazi = true;
				}

				else if (strings_equal_with_ascii_case_insensitivity(
							 argv[i], u8"-cliptype"
						 )) {
					if (i + 1 < argc) // added "1" .--vluzacn
					{
						++i;
						if (strings_equal_with_ascii_case_insensitivity(
								argv[i], "normalized"
							)) {
							g_cliptype = clip_normalized;
						} else if (strings_equal_with_ascii_case_insensitivity(
									   argv[i], "precise"
								   )) {
							g_cliptype = clip_precise;
						} else if (strings_equal_with_ascii_case_insensitivity(
									   argv[i], "legacy"
								   )) {
							g_cliptype = clip_legacy;
						}
					} else {
						Log("Error: -cliptype: incorrect usage of parameter\n"
						);
						Usage();
					}
				}

				else if (strings_equal_with_ascii_case_insensitivity(
							 argv[i], u8"-nullfile"
						 )) {
					if (i + 1 < argc) // added "1" .--vluzacn
					{
						g_nullfile = std::filesystem::path{
							argv[++i], std::filesystem::path::auto_format
						};
					} else {
						Log("Error: -nullfile: expected path to null ent file following parameter\n"
						);
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-nowadtextures"
						   )) {
					g_wadtextures = false;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-wadinclude"
						   )) {
					if (i + 1 < argc) // added "1" .--vluzacn
					{
						g_WadInclude.push_back((char8_t const *) argv[++i]);
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
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-tiny"
						   )) {
					if (i + 1 < argc) // added "1" .--vluzacn
					{
						g_tiny_threshold = (float) atof(argv[++i]);
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-hullfile"
						   )) {
					if (i + 1 < argc) // added "1" .--vluzacn
					{
						g_hullfile = std::filesystem::path{
							argv[++i], std::filesystem::path::auto_format
						};
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-force-legacy-map-encoding"
						   )) {
					settings.forceLegacyMapEncoding = true;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-legacy-map-encoding"
						   )) {
					if (i + 1 < argc) {
						std::u8string_view name{
							(char8_t const *) argv[++i]
						};
						auto encoding = legacy_encoding_by_code_name(name);
						if (encoding) {
							settings.legacyMapEncoding = encoding.value();
						} else {
							Usage();
						}
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-scale"
						   )) {
					if (i + 1 < argc) {
						g_scalesize = atof(argv[++i]);
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-noresetlog"
						   )) {
					g_resetlog = false;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-nolightopt"
						   )) {
					g_nolightopt = true;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-viewsurface"
						   )) {
					g_viewsurface = true;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-nonullifytrigger"
						   )) {
					g_nullifytrigger = false;
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

			// no mapfile?
			if (!mapname_from_arg) {
				// what a shame.
				Log("No mapfile specified\n");
				Usage();
			}
			sourceFilePath = std::filesystem::path(
				mapname_from_arg, std::filesystem::path::auto_format
			);
			if (!sourceFilePath.has_extension()) {
				sourceFilePath += std::filesystem::path(
					u8".map", std::filesystem::path::generic_format
				);
			}

			// handle mapname
			g_Mapname = std::filesystem::path(
				mapname_from_arg, std::filesystem::path::auto_format
			);
			g_Mapname.replace_extension(std::filesystem::path{});

			// onlyents
			if (!g_onlyents) {
				ResetTmpFiles();
			}

			// other stuff
			ResetErrorLog();
			if (!g_onlyents && g_resetlog) {
				ResetLog();
			}
			OpenLog();
			atexit(CloseLog);
			LogStart(argcold, argvold);
			log_arguments(argc, argv);
			hlassume(CalcFaceExtents_test(), assume_first);
			atexit(CSGCleanup);
			dtexdata_init();

			// START CSG
			time_counter timeCounter;
			if (!g_hullfile.empty() && g_hullfile.is_relative()) {
				std::filesystem::path test
					= std::filesystem::path(g_Mapname).parent_path()
					/ g_hullfile;
				if (std::filesystem::exists(test)) {
					g_hullfile = test;
				} else {
					test = get_path_to_directory_with_executable(argv)
						/ g_hullfile;
					if (std::filesystem::exists(test)) {
						g_hullfile = test;
					}
				}
			}
			if (!g_nullfile.empty() && g_nullfile.is_relative()) {
				std::filesystem::path test
					= std::filesystem::path(g_Mapname).parent_path()
					/ g_nullfile;
				if (std::filesystem::exists(test)) {
					g_nullfile = test;
				} else {
					test = get_path_to_directory_with_executable(argv)
						/ g_nullfile;
					if (std::filesystem::exists(test)) {
						g_nullfile = test;
					}
				}
			}
			Verbose("Loading hull file\n");
			// If the user specified a hull file, load it now
			LoadHullfile(g_hullfile);

			if (g_bUseNullTex) {
				properties_initialize(g_nullfile);
			}
			Verbose("Loading map file\n");
			LoadMapFile(settings, sourceFilePath.c_str());
			ThreadSetDefault();
			ThreadSetPriority(g_threadpriority);
			Settings(bspData, settings);

			if (!g_onlyents) {
				GetUsedWads();

				DumpWadinclude();
				Log("\n");
			}

			// if onlyents, just grab the entites and resave
			if (g_onlyents) {
				std::filesystem::path out;
				out = g_Mapname;
				out += u8".bsp";

				LoadBSPFile(out);

				// Write it all back out again.
				WriteBSP(g_Mapname.c_str());

				LogTimeElapsed(timeCounter.get_total());
				return 0;
			}

			CheckForNoClip();

			// createbrush
			// TODO: Reimplement multi-threading here!
			for (brush_count brushIndex = 0; brushIndex != g_nummapbrushes;
				 ++brushIndex) {
				csg_brush& brush{ g_mapbrushes[brushIndex] };
				create_brush(brush, g_entities[brush.entitynum]);
			}

			// NamedRunThreadsOnIndividual(
			//	g_nummapbrushes, g_estimate, CreateBrush
			//);
			CheckFatal();

			// boundworld
			BoundWorld();

			Verbose("%5zu map planes\n", g_mapPlanes.size());

			// Set model centers
			for (i = 0; i < g_numentities; i++) {
				SetModelCenters(i
				); // NamedRunThreadsOnIndividual(g_numentities, g_estimate,
				   // SetModelCenters); //--vluzacn
			}

			// open hull files
			for (i = 0; i < NUM_HULLS; i++) {
				std::filesystem::path const polyFilePath{
					path_to_temp_file_with_extension(
						g_Mapname, polyFileExtensions[i]
					)
				};

				out[i] = fopen(polyFilePath.c_str(), "w");
				if (!out[i]) {
					Error("Couldn't open %s", polyFilePath.c_str());
				}

				std::filesystem::path const brushFilePath{
					path_to_temp_file_with_extension(
						g_Mapname, brushFileExtensions[i]
					)
				};

				out_detailbrush[i] = fopen(brushFilePath.c_str(), "w");
				if (!out_detailbrush[i]) {
					Error("Couldn't open %s", brushFilePath.c_str());
				}

				if (g_viewsurface) {
					std::filesystem::path const surfaceFilePath{
						path_to_temp_file_with_extension(
							g_Mapname, surfaceFileExtensions[i]
						)
					};
					out_view[i] = fopen(surfaceFilePath.c_str(), "w");
					if (!out[i]) {
						Error("Counldn't open %s", surfaceFilePath.c_str());
					}
				}
			}
			{
				std::filesystem::path hullSizeFilePath{
					path_to_temp_file_with_extension(g_Mapname, u8".hsz")
				};
				FILE* f;
				f = fopen(hullSizeFilePath.c_str(), "w");
				if (!f) {
					Error("Couldn't open %s", hullSizeFilePath.c_str());
				}
				for (i = 0; i < NUM_HULLS; i++) {
					float const x1{ g_hull_size[i][0][0] };
					float const y1{ g_hull_size[i][0][1] };
					float const z1{ g_hull_size[i][0][2] };
					float const x2{ g_hull_size[i][1][0] };
					float const y2{ g_hull_size[i][1][1] };
					float const z2{ g_hull_size[i][1][2] };
					fprintf(
						f, "%g %g %g %g %g %g\n", x1, y1, z1, x2, y2, z2
					);
				}
				fclose(f);
			}

			ProcessModels();

			Verbose("%5i csg faces\n", c_csgfaces);
			Verbose("%5i used faces\n", c_outfaces);
			Verbose("%5i tiny faces\n", c_tiny);
			Verbose("%5i tiny clips\n", c_tiny_clip);

			// close hull files
			for (i = 0; i < NUM_HULLS; i++) {
				fclose(out[i]);
				fclose(out_detailbrush[i]);
				if (g_viewsurface) {
					fclose(out_view[i]);
				}
			}

			EmitPlanes();

			WriteBSP(g_Mapname.c_str());

			// Debug
			if constexpr (false) {
				Log("\n---------------------------------------\n"
					"Map Plane Usage:\n"
					"  #  normal             origin             dist   type\n"
					"    (   x,    y,    z) (   x,    y,    z) (     )\n");
				for (i = 0; i < g_mapPlanes.size(); i++) {
					mapplane_t* p = &g_mapPlanes[i];

					Log("%3i (%4.0f, %4.0f, %4.0f) (%4.0f, %4.0f, %4.0f) (%5.0f) %i\n",
						i,
						p->normal[1],
						p->normal[2],
						p->normal[3],
						p->origin[1],
						p->origin[2],
						p->origin[3],
						p->dist,
						(int) p->type);
				}
				Log("---------------------------------------\n\n");
			}

			LogTimeElapsed(timeCounter.get_total());
		}
	}
	return 0;
}
