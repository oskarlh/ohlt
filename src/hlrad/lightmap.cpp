#include "bspfile.h"
#include "color.h"
#include "developer_level.h"
#include "hlassert.h"
#include "hlrad.h"
#include "log.h"
#include "mathlib.h"
#include "mathtypes.h"
#include "threads.h"

#include <algorithm>
#include <array>
#include <numbers>
#include <span>
#include <utility>

std::array<edgeshare_t, MAX_MAP_EDGES> g_edgeshare;
std::array<float3_array, MAX_MAP_EDGES>
	g_face_centroids; // BUG: should this be MAX_MAP_FACES instead of
                      // MAX_MAP_EDGES???
bool g_sky_lighting_fix = DEFAULT_SKY_LIGHTING_FIX;

// =====================================================================================
//  PairEdges
// =====================================================================================
struct intersecttest_t final {
	int numclipplanes;
	dplane_t* clipplanes;
};

bool TestFaceIntersect(intersecttest_t* t, int facenum) {
	dface_t* f2 = &g_dfaces[facenum];
	fast_winding w{ fast_winding(*f2) };
	w.add_offset_to_points(g_face_offset[facenum]);
	for (int k = 0; k < t->numclipplanes; ++k) {
		if (!w.mutating_clip(
				t->clipplanes[k].normal,
				t->clipplanes[k].dist,
				false,
				ON_EPSILON * 4
			)) {
			break;
		}
	}
	bool intersect = w.size() > 0;
	return intersect;
}

intersecttest_t* CreateIntersectTest(dplane_t const * p, int facenum) {
	dface_t* f = &g_dfaces[facenum];
	intersecttest_t* t;
	t = (intersecttest_t*) malloc(sizeof(intersecttest_t));
	hlassume(t != nullptr, assume_msg::NoMemory);
	t->clipplanes = (dplane_t*) malloc(f->numedges * sizeof(dplane_t));
	hlassume(t->clipplanes != nullptr, assume_msg::NoMemory);
	t->numclipplanes = 0;
	int j;
	for (j = 0; j < f->numedges; j++) {
		// should we use winding instead?
		int edgenum = g_dsurfedges[f->firstedge + j];
		{
			float3_array v0, v1;
			if (edgenum < 0) {
				v0 = g_dvertexes[g_dedges[-edgenum].v[1]].point;
				v1 = g_dvertexes[g_dedges[-edgenum].v[0]].point;
			} else {
				v0 = g_dvertexes[g_dedges[edgenum].v[0]].point;
				v1 = g_dvertexes[g_dedges[edgenum].v[1]].point;
			}
			v0 = vector_add(v0, g_face_offset[facenum]);
			v1 = vector_add(v1, g_face_offset[facenum]);

			float3_array const dir = vector_subtract(v1, v0);
			float3_array normal = cross_product(
				dir, p->normal
			); // Facing inward
			if (!normalize_vector(normal)) {
				continue;
			}
			t->clipplanes[t->numclipplanes].normal = normal;
			t->clipplanes[t->numclipplanes].dist = dot_product(v0, normal);
			t->numclipplanes++;
		}
	}
	return t;
}

void FreeIntersectTest(intersecttest_t* t) {
	free(t->clipplanes);
	free(t);
}

void AddFaceForVertexNormal_printerror(
	int const edgeabs, int const edgeend, dface_t* const f
) {
	if (developer_level::warning <= g_developer) {
		Log("AddFaceForVertexNormal - bad face:\n");
		Log(" edgeabs=%d edgeend=%d\n", edgeabs, edgeend);
		for (int i = 0; i < f->numedges; i++) {
			int e = g_dsurfedges[f->firstedge + i];
			edgeshare_t* es = &g_edgeshare[abs(e)];
			int v0 = g_dedges[abs(e)].v[0], v1 = g_dedges[abs(e)].v[1];
			Log(" e=%d v0=%d(%f,%f,%f) v1=%d(%f,%f,%f) share0=%li share1=%li\n",
			    e,
			    v0,
			    g_dvertexes[v0].point[0],
			    g_dvertexes[v0].point[1],
			    g_dvertexes[v0].point[2],
			    v1,
			    g_dvertexes[v1].point[0],
			    g_dvertexes[v1].point[1],
			    g_dvertexes[v1].point[2],
			    (es->faces[0] == nullptr ? -1
			                             : es->faces[0] - g_dfaces.data()),
			    (es->faces[1] == nullptr ? -1
			                             : es->faces[1] - g_dfaces.data()));
		}
	}
}

int AddFaceForVertexNormal(
	int const edgeabs,
	int& edgeabsnext,
	int const edgeend,
	int& edgeendnext,
	dface_t* const f,
	dface_t*& fnext,
	float& angle,
	float3_array& normal
)
// Must guarantee these faces will form a loop or a chain, otherwise will
// result in endless loop.
//
//   e[end]/enext[endnext]
//  *
//  |\.
//  |a\ fnext
//  |  \,
//  | f \.
//  |    \.
//  e   enext
//
{
	normal = getPlaneFromFace(f)->normal;
	int vnum = g_dedges[edgeabs].v[edgeend];
	int iedge, iedgenext, edge, edgenext;
	int i, e, count1, count2;
	float dot;
	for (count1 = count2 = 0, i = 0; i < f->numedges; i++) {
		e = g_dsurfedges[f->firstedge + i];
		if (g_dedges[abs(e)].v[0] == g_dedges[abs(e)].v[1]) {
			continue;
		}
		if (abs(e) == edgeabs) {
			iedge = i;
			edge = e;
			count1++;
		} else if (g_dedges[abs(e)].v[0] == vnum
		           || g_dedges[abs(e)].v[1] == vnum) {
			iedgenext = i;
			edgenext = e;
			count2++;
		}
	}
	if (count1 != 1 || count2 != 1) {
		AddFaceForVertexNormal_printerror(edgeabs, edgeend, f);
		return -1;
	}
	float3_array vec1, vec2;
	int vnum11 = g_dedges[abs(edge)].v[edge > 0 ? 0 : 1];
	int vnum12 = g_dedges[abs(edge)].v[edge > 0 ? 1 : 0];
	int vnum21 = g_dedges[abs(edgenext)].v[edgenext > 0 ? 0 : 1];
	int vnum22 = g_dedges[abs(edgenext)].v[edgenext > 0 ? 1 : 0];
	if (vnum == vnum12 && vnum == vnum21 && vnum != vnum11
	    && vnum != vnum22) {
		vec1 = vector_subtract(
			g_dvertexes[vnum11].point, g_dvertexes[vnum].point
		);
		vec2 = vector_subtract(
			g_dvertexes[vnum22].point, g_dvertexes[vnum].point
		);
		edgeabsnext = abs(edgenext);
		edgeendnext = edgenext > 0 ? 0 : 1;
	} else if (vnum == vnum11 && vnum == vnum22 && vnum != vnum12
	           && vnum != vnum21) {
		vec1 = vector_subtract(
			g_dvertexes[vnum12].point, g_dvertexes[vnum].point
		);
		vec2 = vector_subtract(
			g_dvertexes[vnum21].point, g_dvertexes[vnum].point
		);
		edgeabsnext = abs(edgenext);
		edgeendnext = edgenext > 0 ? 1 : 0;
	} else {
		AddFaceForVertexNormal_printerror(edgeabs, edgeend, f);
		return -1;
	}
	normalize_vector(vec1);
	normalize_vector(vec2);
	dot = dot_product(vec1, vec2);
	dot = dot > 1 ? 1 : dot < -1 ? -1 : dot;
	angle = acos(dot);
	edgeshare_t* es = &g_edgeshare[edgeabsnext];
	if (!(es->faces[0] && es->faces[1])) {
		return 1;
	}
	if (es->faces[0] == f && es->faces[1] != f) {
		fnext = es->faces[1];
	} else if (es->faces[1] == f && es->faces[0] != f) {
		fnext = es->faces[0];
	} else {
		AddFaceForVertexNormal_printerror(edgeabs, edgeend, f);
		return -1;
	}
	return 0;
}

static bool TranslateTexToTex(
	int facenum, int edgenum, int facenum2, matrix_t& m, matrix_t& m_inverse
)
// This function creates a matrix that can translate texture coords in face1
// into texture coords in face2. It keeps all points in the common edge
// invariant. For example, if there is a point in the edge, and in the
// texture of face1, its (s,t)=(16,0), and in face2, its (s,t)=(128,64),
// then we must let matrix*(16,0,0)=(128,64,0)
{
	matrix_t worldtotex;
	matrix_t worldtotex2;

	dvertex_t* vert[2];
	float3_array face_vert[2];
	float3_array face2_vert[2];
	float3_array face_axis[2];
	float3_array face2_axis[2];
	float3_array const v_up = { 0, 0, 1 };
	matrix_t edgetotex, edgetotex2;
	matrix_t inv, inv2;

	TranslateWorldToTex(facenum, worldtotex);
	TranslateWorldToTex(facenum2, worldtotex2);

	dedge_t* e = &g_dedges[edgenum];
	for (int i = 0; i < 2; i++) {
		vert[i] = &g_dvertexes[e->v[i]];
		face_vert[i] = apply_matrix(worldtotex, vert[i]->point);
		face_vert[i][2] = 0; // this value is naturally close to 0 assuming
		                     // that the edge is on the face plane, but
		                     // let's make this more explicit.
		face2_vert[i] = apply_matrix(worldtotex2, vert[i]->point);
		face2_vert[i][2] = 0;
	}

	face_axis[0] = vector_subtract(face_vert[1], face_vert[0]);
	float len = vector_length(face_axis[0]);
	face_axis[1] = cross_product(v_up, face_axis[0]);
	if (CalcMatrixSign(worldtotex)
	    < 0.0) // the three vectors s, t, facenormal are in reverse order
	{
		face_axis[1] = negate_vector(face_axis[1]);
	}

	face2_axis[0] = vector_subtract(face2_vert[1], face2_vert[0]);
	float len2 = vector_length(face2_axis[0]);
	face2_axis[1] = cross_product(v_up, face2_axis[0]);
	if (CalcMatrixSign(worldtotex2) < 0.0) {
		face2_axis[1] = negate_vector(face2_axis[1]);
	}

	edgetotex.v[0] = face_axis[0]; // / v[0][0] v[1][0] \ is a rotation
	                               // (possibly with a reflection by
	                               // the edge)
	edgetotex.v[1] = face_axis[1]; // \ v[0][1] v[1][1] /
	edgetotex.v[2] = vector_scale(
		v_up, len
	); // encode the length into the 3rd value of the matrix
	edgetotex.v[3] = face_vert[0]; // Map (0,0) into the origin point

	edgetotex2.v[0] = face2_axis[0];
	edgetotex2.v[1] = face2_axis[1];
	edgetotex2.v[2] = vector_scale(v_up, len2);
	edgetotex2.v[3] = face2_vert[0];

	if (!InvertMatrix(edgetotex, inv) || !InvertMatrix(edgetotex2, inv2)) {
		return false;
	}
	m = MultiplyMatrix(edgetotex2, inv);
	m_inverse = MultiplyMatrix(edgetotex, inv2);

	return true;
}

void PairEdges() {
	int i, j, k;
	dface_t* f;
	edgeshare_t* e;

	g_edgeshare = {};

	f = g_dfaces.data();
	for (i = 0; i < g_numfaces; i++, f++) {
		if (g_texinfo[f->texinfo].has_special_flag()) {
			// special textures don't have lightmaps
			continue;
		}
		for (j = 0; j < f->numedges; j++) {
			k = g_dsurfedges[f->firstedge + j];
			if (k < 0) {
				e = &g_edgeshare[-k];

				hlassert(e->faces[1] == nullptr);
				e->faces[1] = f;
			} else {
				e = &g_edgeshare[k];

				hlassert(e->faces[0] == nullptr);
				e->faces[0] = f;
			}

			if (e->faces[0] && e->faces[1]) {
				// determine if coplanar
				if (e->faces[0]->planenum == e->faces[1]->planenum
				    && e->faces[0]->side == e->faces[1]->side) {
					e->coplanar = true;
					e->interface_normal
						= getPlaneFromFace(e->faces[0])->normal;

					e->cos_normals_angle = 1.0;
				} else {
					// see if they fall into a "smoothing group" based on
					// angle of the normals
					std::array<float3_array, 2> normals;

					normals[0] = getPlaneFromFace(e->faces[0])->normal;
					normals[1] = getPlaneFromFace(e->faces[1])->normal;

					e->cos_normals_angle = dot_product(
						normals[0], normals[1]
					);

					float smoothvalue;
					int m0 = g_texinfo[e->faces[0]->texinfo].miptex;
					int m1 = g_texinfo[e->faces[1]->texinfo].miptex;
					smoothvalue = std::max(
						g_smoothvalues[m0], g_smoothvalues[m1]
					);
					if (m0 != m1) {
						smoothvalue = std::max(
							smoothvalue, g_smoothing_threshold_2
						);
					}
					if (smoothvalue >= 1.0 - NORMAL_EPSILON) {
						smoothvalue = 2.0;
					}
					if (e->cos_normals_angle > (1.0 - NORMAL_EPSILON)) {
						e->coplanar = true;
						e->interface_normal
							= getPlaneFromFace(e->faces[0])->normal;
						e->cos_normals_angle = 1.0;
					} else if (e->cos_normals_angle >= std::max(
								   smoothvalue - NORMAL_EPSILON,
								   NORMAL_EPSILON
							   )) {
						e->interface_normal = vector_add(
							normals[0], normals[1]
						);
						normalize_vector(e->interface_normal);
					}
				}
				if (!vectors_almost_same(
						g_translucenttextures
							[g_texinfo[e->faces[0]->texinfo].miptex],
						g_translucenttextures
							[g_texinfo[e->faces[1]->texinfo].miptex]
					)) {
					e->coplanar = false;
					e->interface_normal = {};
				}
				{
					int miptex0, miptex1;
					miptex0 = g_texinfo[e->faces[0]->texinfo].miptex;
					miptex1 = g_texinfo[e->faces[1]->texinfo].miptex;
					if (fabs(
							g_lightingconeinfo[miptex0].power
							- g_lightingconeinfo[miptex1].power
						) > NORMAL_EPSILON
					    || fabs(
							   g_lightingconeinfo[miptex0].scale
							   - g_lightingconeinfo[miptex1].scale
						   ) > NORMAL_EPSILON) {
						e->coplanar = false;
						e->interface_normal = {};
					}
				}
				if (!vectors_almost_same(
						e->interface_normal, float3_array{ 0, 0, 0 }
					)) {
					e->smooth = true;
				}
				if (e->smooth) {
					// compute the matrix in advance
					if (!TranslateTexToTex(
							e->faces[0] - g_dfaces.data(),
							abs(k),
							e->faces[1] - g_dfaces.data(),
							e->textotex[0],
							e->textotex[1]
						)) {
						e->smooth = false;
						e->coplanar = false;
						e->interface_normal = {};

						dvertex_t* dv = &g_dvertexes[g_dedges[abs(k)].v[0]];
						Developer(
							developer_level::megaspam,
							"TranslateTexToTex failed on face %d and %d @(%f,%f,%f)",
							(int) (e->faces[0] - g_dfaces.data()),
							(int) (e->faces[1] - g_dfaces.data()),
							dv->point[0],
							dv->point[1],
							dv->point[2]
						);
					}
				}
			}
		}
	}
	{
		int edgeabs, edgeabsnext;
		int edgeend, edgeendnext;
		int d;
		dface_t *f, *fcurrent, *fnext;
		float angle, angles;
		float3_array normal, normals;
		float3_array edgenormal;
		int r, count;
		for (edgeabs = 0; edgeabs < MAX_MAP_EDGES; edgeabs++) {
			e = &g_edgeshare[edgeabs];
			if (!e->smooth) {
				continue;
			}
			edgenormal = e->interface_normal;
			if (g_dedges[edgeabs].v[0] == g_dedges[edgeabs].v[1]) {
				float3_array const errorpos = vector_add(
					g_dvertexes[g_dedges[edgeabs].v[0]].point,
					g_face_offset[e->faces[0] - g_dfaces.data()]

				);
				Developer(
					developer_level::warning,
					"PairEdges: invalid edge at (%f,%f,%f)",
					errorpos[0],
					errorpos[1],
					errorpos[2]
				);
				e->vertex_normal[0] = edgenormal;
				e->vertex_normal[1] = edgenormal;
			} else {
				dplane_t const * p0 = getPlaneFromFace(e->faces[0]);
				dplane_t const * p1 = getPlaneFromFace(e->faces[1]);
				intersecttest_t* test0 = CreateIntersectTest(
					p0, e->faces[0] - g_dfaces.data()
				);
				intersecttest_t* test1 = CreateIntersectTest(
					p1, e->faces[1] - g_dfaces.data()
				);
				for (edgeend = 0; edgeend < 2; edgeend++) {
					float3_array errorpos = vector_add(
						g_dvertexes[g_dedges[edgeabs].v[edgeend]].point,
						g_face_offset[e->faces[0] - g_dfaces.data()]
					);
					angles = 0;
					normals = {};

					for (d = 0; d < 2; d++) {
						f = e->faces[d];
						count = 0, fnext = f, edgeabsnext = edgeabs,
						edgeendnext = edgeend;
						while (1) {
							fcurrent = fnext;
							r = AddFaceForVertexNormal(
								edgeabsnext,
								edgeabsnext,
								edgeendnext,
								edgeendnext,
								fcurrent,
								fnext,
								angle,
								normal
							);
							count++;
							if (r == -1) {
								Developer(
									developer_level::warning,
									"PairEdges: face edges mislink at (%f,%f,%f)",
									errorpos[0],
									errorpos[1],
									errorpos[2]
								);
								break;
							}
							if (count >= 100) {
								Developer(
									developer_level::warning,
									"PairEdges: faces mislink at (%f,%f,%f)",
									errorpos[0],
									errorpos[1],
									errorpos[2]
								);
								break;
							}
							if (dot_product(normal, p0->normal)
							        <= NORMAL_EPSILON
							    || dot_product(normal, p1->normal)
							        <= NORMAL_EPSILON) {
								break;
							}
							float smoothvalue;
							int m0 = g_texinfo[f->texinfo].miptex;
							int m1 = g_texinfo[fcurrent->texinfo].miptex;
							smoothvalue = std::max(
								g_smoothvalues[m0], g_smoothvalues[m1]
							);
							if (m0 != m1) {
								smoothvalue = std::max(
									smoothvalue, g_smoothing_threshold_2
								);
							}
							if (smoothvalue >= 1.0 - NORMAL_EPSILON) {
								smoothvalue = 2.0;
							}
							if (dot_product(edgenormal, normal) < std::max(
									smoothvalue - NORMAL_EPSILON,
									NORMAL_EPSILON
								)) {
								break;
							}
							if (fcurrent != e->faces[0]
							    && fcurrent != e->faces[1]
							    && (TestFaceIntersect(
										test0, fcurrent - g_dfaces.data()
									)
							        || TestFaceIntersect(
										test1, fcurrent - g_dfaces.data()
									))) {
								Developer(
									developer_level::warning,
									"Overlapping faces around corner (%f,%f,%f)\n",
									errorpos[0],
									errorpos[1],
									errorpos[2]
								);
								break;
							}
							angles += angle;
							normals = vector_fma(normal, angle, normals);
							{
								bool in = false;
								if (fcurrent == e->faces[0]
								    || fcurrent == e->faces[1]) {
									in = true;
								}
								for (facelist_t* l
								     = e->vertex_facelist[edgeend];
								     l;
								     l = l->next) {
									if (fcurrent == l->face) {
										in = true;
									}
								}
								if (!in) {
									facelist_t* l = (facelist_t*) malloc(
										sizeof(facelist_t)
									);
									hlassume(
										l != nullptr, assume_msg::NoMemory
									);
									l->face = fcurrent;
									l->next = e->vertex_facelist[edgeend];
									e->vertex_facelist[edgeend] = l;
								}
							}
							if (r != 0 || fnext == f) {
								break;
							}
						}
					}

					if (angles < NORMAL_EPSILON) {
						e->vertex_normal[edgeend] = edgenormal;
						Developer(
							developer_level::warning,
							"PairEdges: no valid faces at (%f,%f,%f)",
							errorpos[0],
							errorpos[1],
							errorpos[2]
						);
					} else {
						normalize_vector(normals);
						e->vertex_normal[edgeend] = normals;
					}
				}
				FreeIntersectTest(test0);
				FreeIntersectTest(test1);
			}
			if (e->coplanar) {
				if (!vectors_almost_same(
						e->vertex_normal[0], e->interface_normal
					)
				    || !vectors_almost_same(
						e->vertex_normal[1], e->interface_normal
					)) {
					e->coplanar = false;
				}
			}
		}
	}
}

#define MAX_SINGLEMAP                                    \
	((MAX_SURFACE_EXTENT + 1) * (MAX_SURFACE_EXTENT + 1) \
	) // #define	MAX_SINGLEMAP	(18*18*4) //--vluzacn

enum class wallflags_t : std::uint8_t {
	none = 0,
	nudged = (1 << 0),
	blocked = (1 << 1), // This only happens when the entire face and
	                    // its surroundings are covered by solid or
	                    // opaque entities
	shadowed = (1 << 2),
};

constexpr wallflags_t operator&(wallflags_t a, wallflags_t b) noexcept {
	return wallflags_t(std::to_underlying(a) & std::to_underlying(b));
}

constexpr wallflags_t& operator&=(wallflags_t& a, wallflags_t b) noexcept {
	a = a & b;
	return a;
}

constexpr wallflags_t operator|(wallflags_t a, wallflags_t b) noexcept {
	return wallflags_t(std::to_underlying(a) | std::to_underlying(b));
}

constexpr wallflags_t& operator|=(wallflags_t& a, wallflags_t b) noexcept {
	a = a | b;
	return a;
}

constexpr bool are_flags_set(wallflags_t flags) noexcept {
	return flags != wallflags_t::none;
}

struct lightinfo_t final {
	float* light;
	float facedist;
	float3_array facenormal;
	bool translucent_b;
	float3_array translucent_v;
	int miptex;

	int numsurfpt;
	float3_array surfpt[MAX_SINGLEMAP];
	float3_array*
		surfpt_position; //[MAX_SINGLEMAP] // surfpt_position[] are valid
	                     // positions for light tracing, while surfpt[] are
	                     // positions for getting phong normal and doing
	                     // patch interpolation
	int* surfpt_surface; //[MAX_SINGLEMAP] // the face that owns this
	                     // position
	bool surfpt_lightoutside[MAX_SINGLEMAP];

	float3_array texorg;
	float3_array worldtotex[2]; // s = (world - texorg) . worldtotex[0]
	float3_array textoworld[2]; // world = texorg + s * textoworld[0]
	float3_array texnormal;

	float exactmins[2], exactmaxs[2];

	int texmins[2], texsize[2];
	int lightstyles[256];
	int surfnum;
	dface_t* face;
	int lmcache_density; // shared by both s and t direction
	int lmcache_offset;  // shared by both s and t direction
	int lmcache_side;
	std::array<float3_array, ALLSTYLES>*
		lmcache; // lm: short for lightmap // don't forget to free!
	float3_array* lmcache_normal; // record the phong normals
	wallflags_t* lmcache_wallflags;
	int lmcachewidth;
	int lmcacheheight;
};

// =====================================================================================
//  TextureNameFromFace
// =====================================================================================
static wad_texture_name TextureNameFromFace(dface_t const * const f) {
	return get_texture_by_number(f->texinfo);
}

// =====================================================================================
//  CalcFaceExtents
//      Fills in s->texmins[] and s->texsize[]
//      also sets exactmins[] and exactmaxs[]
// =====================================================================================
static void CalcFaceExtents(lightinfo_t* l) {
	int const facenum = l->surfnum;
	dface_t* s;
	float mins[2], maxs[2],
		val; // float           mins[2], maxs[2], val; //vluzacn
	int i, j, e;
	dvertex_t* v;
	texinfo_t* tex;

	s = l->face;

	mins[0] = mins[1] = 99999999.0f;
	maxs[0] = maxs[1] = -99999999.0f;

	tex = &g_texinfo[s->texinfo];

	for (i = 0; i < s->numedges; i++) {
		e = g_dsurfedges[s->firstedge + i];
		if (e >= 0) {
			v = g_dvertexes.data() + g_dedges[e].v[0];
		} else {
			v = g_dvertexes.data() + g_dedges[-e].v[1];
		}

		for (j = 0; j < 2; j++) {
			val = dot_product(v->point, tex->vecs[j].xyz)
				+ tex->vecs[j].offset;
			if (val < mins[j]) {
				mins[j] = val;
			}
			if (val > maxs[j]) {
				maxs[j] = val;
			}
		}
	}

	for (i = 0; i < 2; i++) {
		l->exactmins[i] = mins[i];
		l->exactmaxs[i] = maxs[i];
	}

	face_extents const bExtents{ get_face_extents(l->surfnum) };

	for (i = 0; i < 2; i++) {
		mins[i] = bExtents.mins[i];
		maxs[i] = bExtents.maxs[i];
		l->texmins[i] = bExtents.mins[i];
		l->texsize[i] = bExtents.maxs[i] - bExtents.mins[i];
	}

	if (!(tex->has_special_flag())) {
		if ((l->texsize[0] > MAX_SURFACE_EXTENT)
		    || (l->texsize[1] > MAX_SURFACE_EXTENT) || l->texsize[0] < 0
		    || l->texsize[1] < 0 //--vluzacn
		) {
			ThreadLock();
			PrintOnce(
				"\nfor Face %li (texture %s) at ",
				s - g_dfaces.data(),
				TextureNameFromFace(s).c_str()
			);

			for (i = 0; i < s->numedges; i++) {
				e = g_dsurfedges[s->firstedge + i];
				if (e >= 0) {
					v = g_dvertexes.data() + g_dedges[e].v[0];
				} else {
					v = g_dvertexes.data() + g_dedges[-e].v[1];
				}
				float3_array const pos = vector_add(
					v->point, g_face_offset[facenum]
				);
				Log("(%4.3f %4.3f %4.3f) ", pos[0], pos[1], pos[2]);
			}
			Log("\n");

			Error(
				"Bad surface extents (%d x %d)\nCheck the file ZHLTProblems.html for a detailed explanation of this problem",
				l->texsize[0],
				l->texsize[1]
			);
		}
	}
	// allocate sample light cache
	{
		if (g_extra && !g_fastmode) {
			l->lmcache_density = 3;
		} else {
			l->lmcache_density = 1;
		}
		l->lmcache_side = (int) ceil(
			(0.5 * g_blur * l->lmcache_density - 0.5) * (1 - NORMAL_EPSILON)
		);
		l->lmcache_offset = l->lmcache_side;
		l->lmcachewidth = l->texsize[0] * l->lmcache_density + 1
			+ 2 * l->lmcache_side;
		l->lmcacheheight = l->texsize[1] * l->lmcache_density + 1
			+ 2 * l->lmcache_side;
		l->lmcache = (std::array<float3_array, ALLSTYLES>*) malloc(
			l->lmcachewidth * l->lmcacheheight
			* sizeof(std::array<float3_array, ALLSTYLES>)
		);
		hlassume(l->lmcache != nullptr, assume_msg::NoMemory);
		l->lmcache_normal = (float3_array*) malloc(
			l->lmcachewidth * l->lmcacheheight * sizeof(float3_array)
		);
		hlassume(l->lmcache_normal != nullptr, assume_msg::NoMemory);
		l->lmcache_wallflags = (wallflags_t*) malloc(
			l->lmcachewidth * l->lmcacheheight * sizeof(wallflags_t)
		);
		hlassume(l->lmcache_wallflags != nullptr, assume_msg::NoMemory);
		l->surfpt_position = (float3_array*) malloc(
			MAX_SINGLEMAP * sizeof(float3_array)
		);
		l->surfpt_surface = (int*) malloc(MAX_SINGLEMAP * sizeof(int));
		hlassume(
			l->surfpt_position != nullptr && l->surfpt_surface != nullptr,
			assume_msg::NoMemory
		);
	}
}

// =====================================================================================
//  CalcFaceVectors
//      Fills in texorg, worldtotex. and textoworld
// =====================================================================================
static void CalcFaceVectors(lightinfo_t* l) {
	texinfo_t* tex;
	int i, j;
	float3_array texnormal;
	float distscale;
	float dist, len;

	tex = &g_texinfo[l->face->texinfo];

	// convert from float to double
	for (i = 0; i < 2; i++) {
		l->worldtotex[i] = tex->vecs[i].xyz;
	}

	// calculate a normal to the texture axis.  points can be moved along
	// this without changing their S/T
	texnormal = cross_product(tex->vecs[1].xyz, tex->vecs[0].xyz);
	normalize_vector(texnormal);

	// flip it towards plane normal
	distscale = dot_product(texnormal, l->facenormal);
	if (distscale == 0.0) {
		unsigned const facenum = l->face - g_dfaces.data();

		ThreadLock();
		Log("Malformed face (%d) normal @ \n", facenum);
		fast_winding w{ *l->face };
		w.add_offset_to_points(g_face_offset[w.size()]);
		w.Print();
		ThreadUnlock();

		hlassume(false, assume_msg::MalformedTextureFace);
	}

	if (distscale < 0) {
		distscale = -distscale;
		texnormal = negate_vector(texnormal);
	}

	// distscale is the ratio of the distance along the texture normal to
	// the distance along the plane normal
	distscale = 1.0 / distscale;

	for (i = 0; i < 2; i++) {
		l->textoworld[i] = cross_product(l->worldtotex[!i], l->facenormal);
		len = dot_product(l->textoworld[i], l->worldtotex[i]);
		l->textoworld[i] = vector_scale(l->textoworld[i], 1 / len);
	}

	// calculate texorg on the texture plane
	for (i = 0; i < 3; i++) {
		l->texorg[i] = -tex->vecs[0].offset * l->textoworld[0][i]
			- tex->vecs[1].offset * l->textoworld[1][i];
	}

	// project back to the face plane
	dist = dot_product(l->texorg, l->facenormal) - l->facedist;
	dist *= distscale;
	l->texorg = vector_fma(texnormal, -dist, l->texorg);
	l->texnormal = texnormal;
}

static void SetSurfaceFromST(
	lightinfo_t const * const l,
	float3_array& surface,
	float const s,
	float const t
) {
	int const facenum = l->surfnum;

	for (std::size_t j = 0; j < 3; ++j) {
		surface[j] = l->texorg[j] + l->textoworld[0][j] * s
			+ l->textoworld[1][j] * t;
	}

	// Adjust for origin-based models
	surface = vector_add(surface, g_face_offset[facenum]);
}

enum class light_flag {
	outside,        // Not lit
	shifted,        // used HuntForWorld on 100% dark face
	shifted_inside, // moved to neighbhor on 2nd cleanup pass
	normal,         // Normally lit with no movement
	pulled_inside,  // Pulled inside by bleed code adjustments
	simple_nudge,   // A simple nudge 1/3 or 2/3 towards center
	                // along S or T axist
};

// =====================================================================================
//  CalcPoints
//      For each texture aligned grid point, back project onto the plane
//      to get the world xyz value of the sample point
// =====================================================================================
static void SetSTFromSurf(
	lightinfo_t const * const l, float const * surf, float& s, float& t
) {
	int const facenum = l->surfnum;
	int j;

	s = t = 0;
	for (j = 0; j < 3; j++) {
		s += (surf[j] - g_face_offset[facenum][j] - l->texorg[j])
			* l->worldtotex[0][j];
		t += (surf[j] - g_face_offset[facenum][j] - l->texorg[j])
			* l->worldtotex[1][j];
	}
}

struct samplefragedge_t final {
	int edgenum; // g_dedges index
	int edgeside;
	int nextfacenum; // where to grow
	bool tried;

	float3_array point1;    // start point
	float3_array point2;    // end point
	float3_array direction; // normalized; from point1 to point2

	bool noseam;
	float distance; // distance from origin
	float distancereduction;
	float flippedangle;

	float ratio; // if ratio != 1, seam is unavoidable
	matrix_t prevtonext;
	matrix_t nexttoprev;
};

struct samplefragrect_t final {
	std::array<dplane_t, 4> planes;
};

struct samplefrag_t final {
	samplefrag_t* next;       // since this is a node in a list
	samplefrag_t* parentfrag; // where it grew from
	samplefragedge_t* parentedge;
	int facenum; // facenum

	float flippedangle; // copied from parent edge
	bool noseam;        // copied from parent edge

	matrix_t coordtomycoord; // v[2][2] > 0, v[2][0] = v[2][1] = v[0][2] =
	                         // v[1][2] = 0.0
	matrix_t mycoordtocoord;

	float3_array origin; // original s,t
	float3_array
		myorigin; // relative to the texture coordinate on that face
	samplefragrect_t rect; // original rectangle that forms the boundary
	samplefragrect_t
		myrect; // relative to the texture coordinate on that face

	fast_winding* winding; // a fragment of the original rectangle in the
	                       // texture coordinate plane; windings of
	                       // different frags should not overlap
	dplane_t
		windingplane; // normal = (0,0,1) or (0,0,-1); if this normal is
	                  // wrong, point_in_winding() will never return true
	fast_winding*
		mywinding; // relative to the texture coordinate on that face
	dplane_t mywindingplane;

	int numedges;            // # of candicates for the next growth
	samplefragedge_t* edges; // candicates for the next growth
};

struct samplefraginfo_t final {
	int maxsize;
	int size;
	samplefrag_t* head;
};

void ChopFrag(samplefrag_t* frag)
// fill winding, windingplane, mywinding, mywindingplane, numedges, edges
{
	// get the shape of the fragment by clipping the face using the
	// boundaries
	dface_t* f;
	matrix_t worldtotex;
	float3_array const v_up = { 0, 0, 1 };

	f = &g_dfaces[frag->facenum];
	fast_winding facewinding{ *f };

	TranslateWorldToTex(frag->facenum, worldtotex);
	frag->mywinding = new fast_winding();
	frag->mywinding->reserve_point_storage(facewinding.size());
	for (float3_array const & fwp : facewinding.points()) {
		float3_array point{ apply_matrix(worldtotex, fwp) };
		point[2] = 0.0;
		frag->mywinding->push_point(point);
	}
	frag->mywinding->RemoveColinearPoints();

	// This is the same as applying the worldtotex matrix to the
	// faceplane
	frag->mywindingplane.normal = v_up;
	if (CalcMatrixSign(worldtotex) < 0.0) {
		frag->mywindingplane.normal[2] *= -1;
	}
	frag->mywindingplane.dist = 0.0;

	for (int x = 0; x < 4 && frag->mywinding->size() > 0; x++) {
		frag->mywinding->mutating_clip(
			frag->myrect.planes[x].normal,
			frag->myrect.planes[x].dist,
			false
		);
	}

	frag->winding = new fast_winding();
	frag->winding->reserve_point_storage(frag->mywinding->size());
	for (float3_array const & mwp : frag->mywinding->points()) {
		frag->winding->push_point(apply_matrix(frag->mycoordtocoord, mwp));
	}
	frag->winding->RemoveColinearPoints();
	frag->windingplane.normal = frag->mywindingplane.normal;
	if (CalcMatrixSign(frag->mycoordtocoord) < 0.0) {
		frag->windingplane.normal[2] *= -1;
	}
	frag->windingplane.dist = 0.0;

	// find the edges where the fragment can grow in the future
	frag->numedges = 0;
	frag->edges = (samplefragedge_t*) malloc(
		f->numedges * sizeof(samplefragedge_t)
	);
	hlassume(frag->edges != nullptr, assume_msg::NoMemory);
	for (int i = 0; i < f->numedges; i++) {
		samplefragedge_t* e;
		edgeshare_t* es;
		dedge_t* de;
		dvertex_t* dv1;
		dvertex_t* dv2;
		float frac1, frac2;
		float edgelen;
		float dot, dot1, dot2;
		float3_array v, normal;
		matrix_t const * m;
		matrix_t const * m_inverse;

		e = &frag->edges[frag->numedges];

		// some basic info
		e->edgenum = abs(g_dsurfedges[f->firstedge + i]);
		e->edgeside = (g_dsurfedges[f->firstedge + i] < 0 ? 1 : 0);
		es = &g_edgeshare[e->edgenum];
		if (!es->smooth) {
			continue;
		}
		if (es->faces[e->edgeside] - g_dfaces.data() != frag->facenum) {
			Error("internal error 1 in GrowSingleSampleFrag");
		}
		m = &es->textotex[e->edgeside];
		m_inverse = &es->textotex[1 - e->edgeside];
		e->nextfacenum = es->faces[1 - e->edgeside] - g_dfaces.data();
		if (e->nextfacenum == frag->facenum) {
			continue; // an invalid edge (usually very short)
		}
		e->tried = false; // because the frag hasn't been linked into the
		                  // list yet

		// translate the edge points from world to the texture plane of the
		// original frag
		//   so the distances are able to be compared among edges from
		//   different frags
		de = &g_dedges[e->edgenum];
		dv1 = &g_dvertexes[de->v[e->edgeside]];
		dv2 = &g_dvertexes[de->v[1 - e->edgeside]];

		e->point1 = apply_matrix(
			frag->mycoordtocoord, apply_matrix(worldtotex, dv1->point)
		);
		e->point1[2] = 0.0;
		e->point2 = apply_matrix(
			frag->mycoordtocoord, apply_matrix(worldtotex, dv2->point)
		);
		e->point2[2] = 0.0;
		e->direction = vector_subtract(e->point2, e->point1);
		edgelen = normalize_vector(e->direction);
		if (edgelen <= ON_EPSILON) {
			continue;
		}

		// clip the edge
		frac1 = 0;
		frac2 = 1;
		for (int x = 0; x < 4; x++) {
			float dot1;
			float dot2;

			dot1 = dot_product(e->point1, frag->rect.planes[x].normal)
				- frag->rect.planes[x].dist;
			dot2 = dot_product(e->point2, frag->rect.planes[x].normal)
				- frag->rect.planes[x].dist;
			if (dot1 <= ON_EPSILON && dot2 <= ON_EPSILON) {
				frac1 = 1;
				frac2 = 0;
			} else if (dot1 < 0) {
				frac1 = std::max(frac1, dot1 / (dot1 - dot2));
			} else if (dot2 < 0) {
				frac2 = std::min(frac2, dot1 / (dot1 - dot2));
			}
		}
		if (edgelen * (frac2 - frac1) <= ON_EPSILON) {
			continue;
		}
		e->point2 = vector_fma(e->direction, edgelen * frac2, e->point1);
		e->point1 = vector_fma(e->direction, edgelen * frac1, e->point1);

		// calculate the distance, etc., which are used to determine its
		// priority
		e->noseam = frag->noseam;
		dot = dot_product(frag->origin, e->direction);
		dot1 = dot_product(e->point1, e->direction);
		dot2 = dot_product(e->point2, e->direction);
		dot = std::max(dot1, std::min(dot, dot2));
		v = vector_fma(e->direction, dot - dot1, e->point1);
		v = vector_subtract(v, frag->origin);
		e->distance = vector_length(v);
		normal = cross_product(e->direction, frag->windingplane.normal);
		normalize_vector(normal); // points inward
		e->distancereduction = dot_product(v, normal);
		e->flippedangle = frag->flippedangle
			+ acos(std::min(es->cos_normals_angle, (float) 1.0));

		// calculate the matrix
		e->ratio = (*m_inverse).v[2][2];
		if (e->ratio <= NORMAL_EPSILON
		    || (1 / e->ratio) <= NORMAL_EPSILON) {
			Developer(
				developer_level::spam,
				"TranslateTexToTex failed on face %d and %d @(%f,%f,%f)",
				frag->facenum,
				e->nextfacenum,
				dv1->point[0],
				dv1->point[1],
				dv1->point[2]
			);
			continue;
		}

		if (fabs(e->ratio - 1) < 0.005) {
			e->prevtonext = *m;
			e->nexttoprev = *m_inverse;
		} else {
			e->noseam = false;
			e->prevtonext = *m;
			e->nexttoprev = *m_inverse;
		}

		frag->numedges++;
	}
}

static samplefrag_t* GrowSingleFrag(
	samplefraginfo_t const * info,
	samplefrag_t* parent,
	samplefragedge_t* edge
) {
	samplefrag_t* frag;
	bool overlap;
	int numclipplanes;
	dplane_t* clipplanes;

	frag = (samplefrag_t*) malloc(sizeof(samplefrag_t));
	hlassume(frag != nullptr, assume_msg::NoMemory);

	// some basic info
	frag->next = nullptr;
	frag->parentfrag = parent;
	frag->parentedge = edge;
	frag->facenum = edge->nextfacenum;

	frag->flippedangle = edge->flippedangle;
	frag->noseam = edge->noseam;

	// calculate the matrix
	frag->coordtomycoord = MultiplyMatrix(
		edge->prevtonext, parent->coordtomycoord
	);
	frag->mycoordtocoord = MultiplyMatrix(
		parent->mycoordtocoord, edge->nexttoprev
	);

	// Fill in origin
	frag->origin = parent->origin;
	frag->myorigin = apply_matrix(frag->coordtomycoord, frag->origin);

	// fill in boundaries
	frag->rect = parent->rect;
	for (int x = 0; x < 4; x++) {
		// since a plane's parameters are in the dual coordinate space, we
		// translate the original absolute plane into this relative plane by
		// multiplying the inverse matrix
		ApplyMatrixOnPlane(
			frag->mycoordtocoord,
			frag->rect.planes[x].normal,
			frag->rect.planes[x].dist,
			frag->myrect.planes[x].normal,
			frag->myrect.planes[x].dist
		);
		float len = vector_length(frag->myrect.planes[x].normal);
		if (!len) {
			Developer(
				developer_level::megaspam,
				"couldn't translate sample boundaries on face %d",
				frag->facenum
			);
			free(frag);
			return nullptr;
		}
		frag->myrect.planes[x].normal = vector_scale(
			frag->myrect.planes[x].normal, 1.0f / len
		);
		frag->myrect.planes[x].dist /= len;
	}

	// chop windings and edges
	ChopFrag(frag);

	if (frag->winding->size() == 0 || frag->mywinding->size() == 0) {
		// empty
		delete frag->mywinding;
		delete frag->winding;
		free(frag->edges);
		free(frag);
		return nullptr;
	}

	// do overlap test

	overlap = false;
	clipplanes = (dplane_t*) malloc(
		frag->winding->size() * sizeof(dplane_t)
	);
	hlassume(clipplanes != nullptr, assume_msg::NoMemory);
	numclipplanes = 0;
	for (int x = 0; x < frag->winding->size(); x++) {
		clipplanes[numclipplanes].normal = cross_product(
			vector_subtract(
				frag->winding->point((x + 1) % frag->winding->size()),
				frag->winding->point(x)
			),
			frag->windingplane.normal
		);
		if (!normalize_vector(clipplanes[numclipplanes].normal)) {
			continue;
		}
		clipplanes[numclipplanes].dist = dot_product(
			frag->winding->point(x), clipplanes[numclipplanes].normal
		);
		numclipplanes++;
	}
	for (samplefrag_t* f2 = info->head; f2 && !overlap; f2 = f2->next) {
		fast_winding* w = new fast_winding(*f2->winding);
		for (int x = 0; x < numclipplanes && w->size() > 0; x++) {
			w->mutating_clip(
				clipplanes[x].normal,
				clipplanes[x].dist,
				false,
				4 * ON_EPSILON
			);
		}
		if (w->size() > 0) {
			overlap = true;
		}
		delete w;
	}
	free(clipplanes);
	if (overlap) {
		// in the original texture plane, this fragment overlaps with some
		// existing fragments
		delete frag->mywinding;
		delete frag->winding;
		free(frag->edges);
		free(frag);
		return nullptr;
	}

	return frag;
}

static bool FindBestEdge(
	samplefraginfo_t* info,
	samplefrag_t*& bestfrag,
	samplefragedge_t*& bestedge
) {
	samplefrag_t* f;
	samplefragedge_t* e;
	bool found;

	found = false;

	for (f = info->head; f; f = f->next) {
		for (e = f->edges; e < f->edges + f->numedges; e++) {
			if (e->tried) {
				continue;
			}

			bool better;

			if (!found) {
				better = true;
			} else if ((e->flippedangle < std::numbers::pi_v<float>
			                + NORMAL_EPSILON)
			           != (bestedge->flippedangle
			               < std::numbers::pi_v<float> + NORMAL_EPSILON)) {
				better
					= ((e->flippedangle < std::numbers::pi_v<float>
				            + NORMAL_EPSILON)
				       && !(
						   bestedge->flippedangle
						   < std::numbers::pi_v<float> + NORMAL_EPSILON
					   ));
			} else if (e->noseam != bestedge->noseam) {
				better = (e->noseam && !bestedge->noseam);
			} else if (fabs(e->distance - bestedge->distance)
			           > ON_EPSILON) {
				better = (e->distance < bestedge->distance);
			} else if (fabs(
						   e->distancereduction
						   - bestedge->distancereduction
					   )
			           > ON_EPSILON) {
				better
					= (e->distancereduction > bestedge->distancereduction);
			} else {
				better = e->edgenum < bestedge->edgenum;
			}

			if (better) {
				found = true;
				bestfrag = f;
				bestedge = e;
			}
		}
	}

	return found;
}

static samplefraginfo_t* CreateSampleFrag(
	int facenum, float s, float t, float const square[2][2], int maxsize
) {
	samplefraginfo_t* info;
	float3_array const v_s{ 1, 0, 0 };
	float3_array const v_t{ 0, 1, 0 };

	info = (samplefraginfo_t*) malloc(sizeof(samplefraginfo_t));
	hlassume(info != nullptr, assume_msg::NoMemory);
	info->maxsize = maxsize;
	info->size = 1;
	info->head = (samplefrag_t*) malloc(sizeof(samplefrag_t));
	hlassume(info->head != nullptr, assume_msg::NoMemory);

	info->head->next = nullptr;
	info->head->parentfrag = nullptr;
	info->head->parentedge = nullptr;
	info->head->facenum = facenum;

	info->head->flippedangle = 0.0;
	info->head->noseam = true;

	info->head->coordtomycoord = MatrixForScale({ 0.0f, 0.0f, 0.0f }, 1.0f);
	info->head->mycoordtocoord = MatrixForScale({ 0.0f, 0.0f, 0.0f }, 1.0f);

	info->head->origin[0] = s;
	info->head->origin[1] = t;
	info->head->origin[2] = 0.0;
	info->head->myorigin = info->head->origin;

	info->head->rect.planes[0].normal = v_s;
	info->head->rect.planes[0].dist = square[0][0]; // smin
	info->head->rect.planes[1].normal = vector_scale(v_s, -1.0f);
	info->head->rect.planes[1].dist = -square[1][0]; // smax
	info->head->rect.planes[2].normal = v_t;
	info->head->rect.planes[2].dist = square[0][1]; // tmin
	info->head->rect.planes[3].normal = vector_scale(v_t, -1.0f);
	info->head->rect.planes[3].dist = -square[1][1]; // tmax
	info->head->myrect = info->head->rect;

	ChopFrag(info->head);

	if (info->head->winding->size() == 0
	    || info->head->mywinding->size() == 0) {
		// empty
		delete info->head->mywinding;
		delete info->head->winding;
		free(info->head->edges);
		free(info->head);
		info->head = nullptr;
		info->size = 0;
	} else {
		// prune edges
		for (samplefragedge_t* e = info->head->edges;
		     e < info->head->edges + info->head->numedges;
		     e++) {
			if (e->nextfacenum == info->head->facenum) {
				e->tried = true;
			}
		}
	}

	while (info->size < info->maxsize) {
		samplefrag_t* bestfrag;
		samplefragedge_t* bestedge;
		samplefrag_t* newfrag;

		if (!FindBestEdge(info, bestfrag, bestedge)) {
			break;
		}

		newfrag = GrowSingleFrag(info, bestfrag, bestedge);
		bestedge->tried = true;

		if (newfrag) {
			newfrag->next = info->head;
			info->head = newfrag;
			info->size++;

			for (samplefrag_t* f = info->head; f; f = f->next) {
				for (samplefragedge_t* e = newfrag->edges;
				     e < newfrag->edges + newfrag->numedges;
				     e++) {
					if (e->nextfacenum == f->facenum) {
						e->tried = true;
					}
				}
			}
			for (samplefrag_t* f = info->head; f; f = f->next) {
				for (samplefragedge_t* e = f->edges;
				     e < f->edges + f->numedges;
				     e++) {
					if (e->nextfacenum == newfrag->facenum) {
						e->tried = true;
					}
				}
			}
		}
	}

	return info;
}

static void DeleteSampleFrag(samplefraginfo_t* fraginfo) {
	while (fraginfo->head) {
		samplefrag_t* f;

		f = fraginfo->head;
		fraginfo->head = f->next;
		delete f->mywinding;
		delete f->winding;
		free(f->edges);
		free(f);
	}
	free(fraginfo);
}

static light_flag SetSampleFromST(
	float3_array& point,
	float3_array& position, // a valid world position for light tracing
	int* surface, // the face used for phong normal and patch interpolation
	bool* nudged,
	lightinfo_t const * l,
	float original_s,
	float original_t,
	float const square[2][2], // {smin, tmin}, {smax, tmax}
	model_light_mode_flags lightmode
) {
	light_flag LuxelFlag;
	int facenum;
	dface_t* face;
	dplane_t const * faceplane;
	samplefraginfo_t* fraginfo;
	samplefrag_t* f;

	facenum = l->surfnum;
	face = l->face;
	faceplane = getPlaneFromFace(face);

	fraginfo = CreateSampleFrag(
		facenum, original_s, original_t, square, 100
	);

	bool found;
	samplefrag_t* bestfrag;
	float3_array bestpos;
	float bests, bestt;
	float best_dist;
	bool best_nudged;

	found = false;
	for (f = fraginfo->head; f; f = f->next) {
		float3_array pos;
		float s, t;
		float dist;

		bool nudged_one;
		if (!FindNearestPosition(
				f->facenum,
				f->mywinding,
				f->mywindingplane,
				f->myorigin[0],
				f->myorigin[1],
				pos,
				&s,
				&t,
				&dist,
				&nudged_one
			)) {
			continue;
		}

		bool better;

		if (!found) {
			better = true;
		} else if (nudged_one != best_nudged) {
			better = !nudged_one;
		} else if (fabs(dist - best_dist) > 2 * ON_EPSILON) {
			better = (dist < best_dist);
		} else if (f->noseam != bestfrag->noseam) {
			better = (f->noseam && !bestfrag->noseam);
		} else {
			better = (f->facenum < bestfrag->facenum);
		}

		if (better) {
			found = true;
			bestfrag = f;
			bestpos = pos;
			bests = s;
			bestt = t;
			best_dist = dist;
			best_nudged = nudged_one;
		}
	}

	if (found) {
		matrix_t worldtotex, textoworld;
		float3_array tex;

		TranslateWorldToTex(bestfrag->facenum, worldtotex);
		if (!InvertMatrix(worldtotex, textoworld)) {
			unsigned const facenum = bestfrag->facenum;
			ThreadLock();
			Log("Malformed face (%d) normal @ \n", facenum);
			fast_winding w{ g_dfaces[facenum] };
			w.add_offset_to_points(g_face_offset[facenum]);
			w.Print();
			ThreadUnlock();
			hlassume(false, assume_msg::MalformedTextureFace);
		}

		// point
		tex[0] = bests;
		tex[1] = bestt;
		tex[2] = 0.0;
		point = vector_add(
			apply_matrix(textoworld, tex), g_face_offset[bestfrag->facenum]
		);
		// position
		position = bestpos;
		// surface
		*surface = bestfrag->facenum;
		// whether nudged to fit
		*nudged = best_nudged;
		// returned value
		LuxelFlag = light_flag::normal;
	} else {
		SetSurfaceFromST(l, point, original_s, original_t);
		position = vector_fma(
			faceplane->normal, DEFAULT_HUNT_OFFSET, point
		);
		*surface = facenum;
		*nudged = true;
		LuxelFlag = light_flag::outside;
	}

	DeleteSampleFrag(fraginfo);

	return LuxelFlag;
}

static void CalcPoints(lightinfo_t* l) {
	int const facenum = l->surfnum;
	dface_t const * f = g_dfaces.data() + facenum;
	dplane_t const * p = getPlaneFromFace(f);
	model_light_mode_flags const lightmode = g_face_lightmode[facenum];
	int const h = l->texsize[1] + 1;
	int const w = l->texsize[0] + 1;
	float const starts = l->texmins[0] * TEXTURE_STEP;
	float const startt = l->texmins[1] * TEXTURE_STEP;
	light_flag LuxelFlags[MAX_SINGLEMAP];
	light_flag* pLuxelFlags;
	float us, ut;
	l->numsurfpt = w * h;
	for (int t = 0; t < h; t++) {
		for (int s = 0; s < w; s++) {
			float3_array& surf = l->surfpt[s + w * t];
			pLuxelFlags = &LuxelFlags[s + w * t];
			us = starts + s * TEXTURE_STEP;
			ut = startt + t * TEXTURE_STEP;
			float square[2][2];
			square[0][0] = us - TEXTURE_STEP;
			square[0][1] = ut - TEXTURE_STEP;
			square[1][0] = us + TEXTURE_STEP;
			square[1][1] = ut + TEXTURE_STEP;
			bool nudged;
			*pLuxelFlags = SetSampleFromST(
				surf,
				l->surfpt_position[s + w * t],
				&l->surfpt_surface[s + w * t],
				&nudged,
				l,
				us,
				ut,
				square,
				lightmode
			);
		}
	}
	{
		int s_other, t_other;
		light_flag* pLuxelFlags_other;
		bool adjusted;
		for (int i = 0; i < h + w; i++) { // propagate valid light samples
			adjusted = false;
			for (int t = 0; t < h; t++) {
				for (int s = 0; s < w; s++) {
					float3_array& surf = l->surfpt[s + w * t];
					pLuxelFlags = &LuxelFlags[s + w * t];
					if (*pLuxelFlags != light_flag::outside) {
						continue;
					}
					for (int n = 0; n < 4; n++) {
						switch (n) {
							case 0:
								s_other = s + 1;
								t_other = t;
								break;
							case 1:
								s_other = s - 1;
								t_other = t;
								break;
							case 2:
								s_other = s;
								t_other = t + 1;
								break;
							case 3:
								s_other = s;
								t_other = t - 1;
								break;
						}
						if (t_other < 0 || t_other >= h || s_other < 0
						    || s_other >= w) {
							continue;
						}
						float3_array const & surf_other
							= l->surfpt[s_other + w * t_other];
						pLuxelFlags_other
							= &LuxelFlags[s_other + w * t_other];
						if (*pLuxelFlags_other != light_flag::outside
						    && *pLuxelFlags_other != light_flag::shifted) {
							*pLuxelFlags = light_flag::shifted;
							surf = surf_other;
							l->surfpt_position[s + w * t]
								= l->surfpt_position[s_other + w * t_other];
							l->surfpt_surface[s + w * t]
								= l->surfpt_surface[s_other + w * t_other];
							adjusted = true;
							break;
						}
					}
				}
			}
			for (int t = 0; t < h; t++) {
				for (int s = 0; s < w; s++) {
					pLuxelFlags = &LuxelFlags[s + w * t];
					if (*pLuxelFlags == light_flag::shifted) {
						*pLuxelFlags = light_flag::shifted_inside;
					}
				}
			}
			if (!adjusted) {
				break;
			}
		}
	}
	for (int i = 0; i < MAX_SINGLEMAP; i++) {
		l->surfpt_lightoutside[i] = (LuxelFlags[i] == light_flag::outside);
	}
}

//==============================================================

struct sample_t final {
	float3_array pos;
	float3_array light;
	int surface; // this sample can grow into another face
};

struct facelight_t final {
	int numsamples;
	std::array<sample_t*, MAXLIGHTMAPS> samples;
};

static directlight_t* directlights[MAX_MAP_LEAFS];
static facelight_t facelight[MAX_MAP_FACES];
static int numdlights;

// =====================================================================================
//  CreateDirectLights
// =====================================================================================
void CreateDirectLights() {
	int leafnum;
	entity_t* e;
	float3_array dest;

	numdlights = 0;
	int styleused[ALLSTYLES];
	memset(styleused, 0, ALLSTYLES * sizeof(styleused[0]));
	styleused[0] = true;
	int numstyles = 1;

	//
	// surfaces
	//
	for (patch_t& patch : g_patches) {
		if (patch.emitstyle >= 0 && patch.emitstyle < ALLSTYLES) {
			if (styleused[patch.emitstyle] == false) {
				styleused[patch.emitstyle] = true;
				numstyles++;
			}
		}
		if (dot_product(patch.baselight, patch.texturereflectivity) / 3
		        > 0.0
		    && !(
				g_face_texlights[patch.faceNumber]
				&& has_key_value(
					g_face_texlights[patch.faceNumber], u8"_scale"
				)
				&& float_for_key(
					   *g_face_texlights[patch.faceNumber], u8"_scale"
				   ) <= 0
			)) // LRC
		{
			numdlights++;
			directlight_t* dl = (directlight_t*) calloc(
				1, sizeof(directlight_t)
			);

			hlassume(dl != nullptr, assume_msg::NoMemory);

			dl->origin = patch.origin;

			dleaf_t* const leaf = PointInLeaf(dl->origin);
			leafnum = leaf - g_dleafs.data();

			dl->next = directlights[leafnum];
			directlights[leafnum] = dl;
			dl->style = patch.emitstyle; // LRC
			dl->topatch = false;
			if (!patch.emitmode) {
				dl->topatch = true;
			}
			if (g_fastmode) {
				dl->topatch = true;
			}
			dl->patch_area = patch.area;
			dl->patch_emitter_range = patch.emitter_range;
			dl->patch = &patch;
			dl->texlightgap = g_texlightgap;
			if (g_face_texlights[patch.faceNumber]
			    && has_key_value(
					g_face_texlights[patch.faceNumber], u8"_texlightgap"
				)) {
				dl->texlightgap = float_for_key(
					*g_face_texlights[patch.faceNumber], u8"_texlightgap"
				);
			}
			dl->stopdot = 0.0;
			dl->stopdot2 = 0.0;
			if (g_face_texlights[patch.faceNumber]) {
				if (has_key_value(
						g_face_texlights[patch.faceNumber], u8"_cone"
					)) {
					dl->stopdot = float_for_key(
						*g_face_texlights[patch.faceNumber], u8"_cone"
					);
					dl->stopdot = dl->stopdot >= 90
						? 0
						: std::cos(
							  dl->stopdot
							  * (std::numbers::pi_v<float> / 180)
						  );
				}
				if (has_key_value(
						g_face_texlights[patch.faceNumber], u8"_cone2"
					)) {
					dl->stopdot2 = float_for_key(
						*g_face_texlights[patch.faceNumber], u8"_cone2"
					);
					dl->stopdot2 = dl->stopdot2 >= 90
						? 0
						: std::cos(
							  dl->stopdot2
							  * (std::numbers::pi_v<float> / 180)
						  );
				}
				if (dl->stopdot2 > dl->stopdot) {
					dl->stopdot2 = dl->stopdot;
				}
			}

			dl->type = emit_type::surface;
			dl->normal = getPlaneFromFaceNumber(patch.faceNumber)->normal;
			dl->intensity = patch.baselight; // LRC
			if (g_face_texlights[patch.faceNumber]) {
				if (has_key_value(
						g_face_texlights[patch.faceNumber], u8"_scale"
					)) {
					float scale = float_for_key(
						*g_face_texlights[patch.faceNumber], u8"_scale"
					);
					dl->intensity = vector_scale(dl->intensity, scale);
				}
			}
			dl->intensity = vector_scale(dl->intensity, patch.area);
			dl->intensity = vector_scale(dl->intensity, patch.exposure);
			dl->intensity
				= vector_scale(dl->intensity, 1 / std::numbers::pi_v<float>);
			dl->intensity = vector_multiply(
				dl->intensity, patch.texturereflectivity
			);

			dface_t* f = &g_dfaces[patch.faceNumber];
			if (g_face_entity[patch.faceNumber] != g_entities.data()
			    && get_texture_by_number(f->texinfo).is_water()) {
				numdlights++;
				directlight_t* dl2 = (directlight_t*) calloc(
					1, sizeof(directlight_t)
				);
				hlassume(dl2 != nullptr, assume_msg::NoMemory);
				*dl2 = *dl;
				dl2->origin = vector_fma(dl->normal, -2.0f, dl->origin);
				dl2->normal = negate_vector(dl->normal);
				dleaf_t* const leaf = PointInLeaf(dl2->origin);
				leafnum = leaf - g_dleafs.data();
				dl2->next = directlights[leafnum];
				directlights[leafnum] = dl2;
			}
		}
	}

	//
	// entities
	//
	for (unsigned i = 0; i < (unsigned) g_numentities; i++) {
		double r, g, b, scaler;
		int argCnt;

		e = &g_entities[i];

		std::u8string_view name = get_classname(*e);
		if (!name.starts_with(u8"light")) {
			continue;
		}
		{
			int style = IntForKey(e, u8"style");
			if (style < 0) {
				style = -style;
			}
			style = (unsigned char) style;
			if (style > 0 && style < ALLSTYLES
			    && has_key_value(e, u8"zhlt_stylecoring")) {
				g_corings[style] = float_for_key(*e, u8"zhlt_stylecoring");
			}
		}
		if (name == u8"light_shadow" || name == u8"light_bounce") {
			int style = IntForKey(e, u8"style");
			if (style < 0) {
				style = -style;
			}
			style = (unsigned char) style;
			if (style >= 0 && style < ALLSTYLES) {
				if (styleused[style] == false) {
					styleused[style] = true;
					numstyles++;
				}
			}
			continue;
		}
		if (name == u8"light_surface") {
			continue;
		}

		numdlights++;
		directlight_t* dl = (directlight_t*) calloc(
			1, sizeof(directlight_t)
		);

		hlassume(dl != nullptr, assume_msg::NoMemory);

		dl->origin = get_float3_for_key(*e, u8"origin");

		dleaf_t* const leaf = PointInLeaf(dl->origin);
		leafnum = leaf - g_dleafs.data();

		dl->next = directlights[leafnum];
		directlights[leafnum] = dl;

		dl->style = IntForKey(e, u8"style");
		if (dl->style < 0) {
			dl->style = -dl->style; // LRC
		}
		dl->style = (unsigned char) dl->style;
		if (dl->style >= ALLSTYLES) {
			Error(
				"invalid light style: style (%d) >= ALLSTYLES (%d)",
				dl->style,
				ALLSTYLES
			);
		}
		if (dl->style >= 0 && dl->style < ALLSTYLES) {
			if (styleused[dl->style] == false) {
				styleused[dl->style] = true;
				numstyles++;
			}
		}
		dl->topatch = false;
		if (bool_key_value(*e, u8"_fast") == 1) {
			dl->topatch = true;
		}
		if (g_fastmode) {
			dl->topatch = true;
		}
		char const * lightString
			= (char const *) value_for_key(e, u8"_light").data();
		// scanf into doubles, then assign, so it is float size independent
		r = g = b = scaler = 0;
		argCnt = sscanf(
			lightString, "%lf %lf %lf %lf", &r, &g, &b, &scaler
		);
		dl->intensity[0] = (float) r;
		if (argCnt == 1) {
			// The R,G,B values are all equal.
			dl->intensity[1] = dl->intensity[2] = (float) r;
		} else if (argCnt == 3 || argCnt == 4) {
			// Save the other two G,B values.
			dl->intensity[1] = (float) g;
			dl->intensity[2] = (float) b;

			// Did we also get an "intensity" scaler value too?
			if (argCnt == 4) {
				// Scale the normalized 0-255 R,G,B values by the intensity
				// scaler
				dl->intensity[0] = dl->intensity[0] / 255 * (float) scaler;
				dl->intensity[1] = dl->intensity[1] / 255 * (float) scaler;
				dl->intensity[2] = dl->intensity[2] / 255 * (float) scaler;
			}
		} else {
			Log("light at (%f,%f,%f) has bad or missing '_light' value: '%s'\n",
			    dl->origin[0],
			    dl->origin[1],
			    dl->origin[2],
			    lightString);
			continue;
		}

		dl->fade = float_for_key(*e, u8"_fade");
		if (dl->fade == 0.0) {
			dl->fade = g_fade;
		}

		std::u8string_view target = value_for_key(e, u8"target");

		if (name == u8"light_spot" || name == u8"light_environment"
		    || !target.empty()) {
			dl->type = emit_type::spotlight;
			dl->stopdot = float_for_key(*e, u8"_cone");
			if (!dl->stopdot) {
				dl->stopdot = 10;
			}
			dl->stopdot2 = float_for_key(*e, u8"_cone2");
			if (!dl->stopdot2) {
				dl->stopdot2 = dl->stopdot;
			}
			if (dl->stopdot2 < dl->stopdot) {
				dl->stopdot2 = dl->stopdot;
			}
			dl->stopdot2 = std::cos(
				dl->stopdot2 * (std::numbers::pi_v<float> / 180)
			);
			dl->stopdot = std::cos(
				dl->stopdot * (std::numbers::pi_v<float> / 180)
			);

			auto maybeE2 = find_target_entity(target);
			if (!target.empty() && !maybeE2) {
				Warning(
					"light at (%i %i %i) has missing target",
					(int) dl->origin[0],
					(int) dl->origin[1],
					(int) dl->origin[2]
				);
				target = u8"";
			}
			if (maybeE2) { // point towards target
				dest = get_float3_for_key(maybeE2.value(), u8"origin");
				dl->normal = vector_subtract(dest, dl->origin);
				normalize_vector(dl->normal);
			} else { // point down angle
				float3_array vAngles = get_float3_for_key(*e, u8"angles");

				float angle = float_for_key(*e, u8"angle");
				if (angle == ANGLE_UP) {
					dl->normal[0] = dl->normal[1] = 0;
					dl->normal[2] = 1;
				} else if (angle == ANGLE_DOWN) {
					dl->normal[0] = dl->normal[1] = 0;
					dl->normal[2] = -1;
				} else {
					// if we don't have a specific "angle" use the "angles"
					// YAW
					if (!angle) {
						angle = vAngles[1];
					}

					dl->normal[2] = 0;
					dl->normal[0] = std::cos(
						angle * (std::numbers::pi_v<float> / 180)
					);
					dl->normal[1] = std::sin(
						angle * (std::numbers::pi_v<float> / 180)
					);
				}

				angle = float_for_key(*e, u8"pitch");
				if (!angle) {
					// if we don't have a specific "pitch" use the "angles"
					// PITCH
					angle = vAngles[0];
				}

				dl->normal[2] = std::sin(
					angle * (std::numbers::pi_v<float> / 180)
				);
				dl->normal[0] *= std::cos(
					angle * (std::numbers::pi_v<float> / 180)
				);
				dl->normal[1] *= std::cos(
					angle * (std::numbers::pi_v<float> / 180)
				);
			}

			if (float_for_key(*e, u8"_sky")
			    || name == u8"light_environment") {
				// -----------------------------------------------------------------------------------
				// Changes by Adam Foster - afoster@compsoc.man.ac.uk
				// diffuse lighting hack - most of the following code nicked
				// from earlier need to get diffuse intensity from new
				// _diffuse_light key
				//
				// What does _sky do for spotlights, anyway?
				// -----------------------------------------------------------------------------------
				char const * diffuseLightString
					= (char const *) value_for_key(e, u8"_diffuse_light")
						  .data();
				r = g = b = scaler = 0;
				argCnt = sscanf(
					diffuseLightString,
					"%lf %lf %lf %lf",
					&r,
					&g,
					&b,
					&scaler
				);
				dl->diffuse_intensity[0] = (float) r;
				if (argCnt == 1) {
					// The R,G,B values are all equal.
					dl->diffuse_intensity[1] = dl->diffuse_intensity[2]
						= (float) r;
				} else if (argCnt == 3 || argCnt == 4) {
					// Save the other two G,B values.
					dl->diffuse_intensity[1] = (float) g;
					dl->diffuse_intensity[2] = (float) b;

					// Did we also get an "intensity" scaler value too?
					if (argCnt == 4) {
						// Scale the normalized 0-255 R,G,B values by the
						// intensity scaler
						dl->diffuse_intensity[0] = dl->diffuse_intensity[0]
							/ 255 * (float) scaler;
						dl->diffuse_intensity[1] = dl->diffuse_intensity[1]
							/ 255 * (float) scaler;
						dl->diffuse_intensity[2] = dl->diffuse_intensity[2]
							/ 255 * (float) scaler;
					}
				} else {
					// backwards compatibility with maps without
					// _diffuse_light

					dl->diffuse_intensity[0] = dl->intensity[0];
					dl->diffuse_intensity[1] = dl->intensity[1];
					dl->diffuse_intensity[2] = dl->intensity[2];
				}
				// -----------------------------------------------------------------------------------
				char const * diffuseLight2String = (char const *)
					ValueForKey(e, u8"_diffuse_light2");
				r = g = b = scaler = 0;
				argCnt = sscanf(
					diffuseLight2String,
					"%lf %lf %lf %lf",
					&r,
					&g,
					&b,
					&scaler
				);
				dl->diffuse_intensity2[0] = (float) r;
				if (argCnt == 1) {
					// The R,G,B values are all equal.
					dl->diffuse_intensity2[1] = dl->diffuse_intensity2[2]
						= (float) r;
				} else if (argCnt == 3 || argCnt == 4) {
					// Save the other two G,B values.
					dl->diffuse_intensity2[1] = (float) g;
					dl->diffuse_intensity2[2] = (float) b;

					// Did we also get an "intensity" scaler value too?
					if (argCnt == 4) {
						// Scale the normalized 0-255 R,G,B values by the
						// intensity scaler
						dl->diffuse_intensity2[0]
							= dl->diffuse_intensity2[0] / 255
							* (float) scaler;
						dl->diffuse_intensity2[1]
							= dl->diffuse_intensity2[1] / 255
							* (float) scaler;
						dl->diffuse_intensity2[2]
							= dl->diffuse_intensity2[2] / 255
							* (float) scaler;
					}
				} else {
					dl->diffuse_intensity2[0] = dl->diffuse_intensity[0];
					dl->diffuse_intensity2[1] = dl->diffuse_intensity[1];
					dl->diffuse_intensity2[2] = dl->diffuse_intensity[2];
				}

				dl->type = emit_type::skylight;
				dl->stopdot2 = float_for_key(
					*e, u8"_sky"
				); // hack stopdot2 to a sky key number
				dl->sunspreadangle = float_for_key(*e, u8"_spread");
				if (!g_allow_spread) {
					dl->sunspreadangle = 0;
				}
				if (dl->sunspreadangle < 0.0 || dl->sunspreadangle > 180) {
					Error(
						"Invalid spread angle '%s'. Please use a number between 0 and 180.\n",
						(char const *) ValueForKey(e, u8"_spread")
					);
				}
				if (dl->sunspreadangle > 0.0) {
					int i;
					float testangle = dl->sunspreadangle;
					if (dl->sunspreadangle < SUNSPREAD_THRESHOLD) {
						testangle = SUNSPREAD_THRESHOLD; // We will later
						                                 // centralize all
						                                 // the normals we
						                                 // have collected.
					}
					{
						float totalweight = 0;
						int count;
						float testdot = cos(
							testangle * (std::numbers::pi_v<float> / 180)
						);
						for (count = 0, i = 0;
						     i < g_numskynormals[SUNSPREAD_SKYLEVEL];
						     i++) {
							float3_array& testnormal
								= g_skynormals[SUNSPREAD_SKYLEVEL][i];
							float dot = dot_product(dl->normal, testnormal);
							if (dot >= testdot - NORMAL_EPSILON) {
								totalweight += std::max(
												   (float) 0, dot - testdot
											   )
									* g_skynormalsizes
										[SUNSPREAD_SKYLEVEL]
										[i]; // This is not the right
								             // formula when
								             // dl->sunspreadangle <
								             // SUNSPREAD_THRESHOLD, but it
								             // gives almost the same result
								             // as the right one.
								count++;
							}
						}
						if (count <= 10 || totalweight <= NORMAL_EPSILON) {
							Error(
								"collect spread normals: internal error: can not collect enough normals."
							);
						}
						dl->numsunnormals = count;
						dl->sunnormals = (float3_array*) malloc(
							count * sizeof(float3_array)
						);
						dl->sunnormalweights = (float*) malloc(
							count * sizeof(float)
						);
						hlassume(
							dl->sunnormals != nullptr, assume_msg::NoMemory
						);
						hlassume(
							dl->sunnormalweights != nullptr,
							assume_msg::NoMemory
						);
						for (count = 0, i = 0;
						     i < g_numskynormals[SUNSPREAD_SKYLEVEL];
						     i++) {
							float3_array& testnormal
								= g_skynormals[SUNSPREAD_SKYLEVEL][i];
							float dot = dot_product(dl->normal, testnormal);
							if (dot >= testdot - NORMAL_EPSILON) {
								if (count >= dl->numsunnormals) {
									Error(
										"collect spread normals: internal error."
									);
								}
								dl->sunnormals[count] = testnormal;
								dl->sunnormalweights[count]
									= std::max((float) 0, dot - testdot)
									* g_skynormalsizes[SUNSPREAD_SKYLEVEL]
													  [i]
									/ totalweight;
								count++;
							}
						}
						if (count != dl->numsunnormals) {
							Error("collect spread normals: internal error."
							);
						}
					}
					if (dl->sunspreadangle < SUNSPREAD_THRESHOLD) {
						for (i = 0; i < dl->numsunnormals; i++) {
							float3_array tmp = vector_scale(
								dl->sunnormals[i],
								1
									/ dot_product(
										dl->sunnormals[i], dl->normal
									)
							);
							tmp = vector_subtract(tmp, dl->normal);
							dl->sunnormals[i] = vector_fma(
								tmp,
								dl->sunspreadangle / SUNSPREAD_THRESHOLD,
								dl->normal
							);
							normalize_vector(dl->sunnormals[i]);
						}
					}
				} else {
					dl->numsunnormals = 1;
					dl->sunnormals = (float3_array*) malloc(
						sizeof(float3_array)
					);
					dl->sunnormalweights = (float*) malloc(sizeof(float));
					hlassume(
						dl->sunnormals != nullptr, assume_msg::NoMemory
					);
					hlassume(
						dl->sunnormalweights != nullptr,
						assume_msg::NoMemory
					);
					dl->sunnormals[0] = dl->normal;
					dl->sunnormalweights[0] = 1.0;
				}
			}
		} else {
			dl->type = emit_type::point;
		}

		if (dl->type != emit_type::skylight) {
			// why? --vluzacn
			float l1 = fast_sqrt(std::max(
						   dl->intensity[0],
						   std::max(dl->intensity[1], dl->intensity[2])
					   ))
				/ 10;

			dl->intensity[0] *= l1;
			dl->intensity[1] *= l1;
			dl->intensity[2] *= l1;
		}
	}

	int countnormallights = 0, countfastlights = 0;
	{
		for (int l = 0; l < 1 + g_dmodels[0].visleafs; l++) {
			for (directlight_t* dl = directlights[l]; dl; dl = dl->next) {
				switch (dl->type) {
					case emit_type::surface:
					case emit_type::point:
					case emit_type::spotlight:
						if (!vectors_almost_same(
								dl->intensity, float3_array{}
							)) {
							if (dl->topatch) {
								countfastlights++;
							} else {
								countnormallights++;
							}
						}
						break;
					case emit_type::skylight:
						if (!vectors_almost_same(
								dl->intensity, float3_array{}
							)) {
							if (dl->topatch) {
								countfastlights++;
								if (dl->sunspreadangle > 0.0) {
									countfastlights--;
									countfastlights += dl->numsunnormals;
								}
							} else {
								countnormallights++;
								if (dl->sunspreadangle > 0.0) {
									countnormallights--;
									countnormallights += dl->numsunnormals;
								}
							}
						}
						if (g_indirect_sun > 0
						    && !vectors_almost_same(
								dl->diffuse_intensity, float3_array{}
							)) {
							if (g_softsky) {
								countfastlights
									+= g_numskynormals[SKYLEVEL_SOFTSKYON];
							} else {
								countfastlights
									+= g_numskynormals[SKYLEVEL_SOFTSKYOFF];
							}
						}
						break;
					default:
						hlassume(false, assume_msg::BadLightType);
						break;
				}
			}
		}
	}
	Log("%i direct lights and %i fast direct lights\n",
	    countnormallights,
	    countfastlights);
	Log("%i light styles\n", numstyles);
	// move all emit_type::skylight to leaf 0 (the solid leaf)
	if (g_sky_lighting_fix) {
		directlight_t* skylights = nullptr;
		int l;
		for (l = 0; l < 1 + g_dmodels[0].visleafs; l++) {
			directlight_t** pdl;
			directlight_t* dl;
			for (dl = directlights[l], pdl = &directlights[l]; dl;
			     dl = *pdl) {
				if (dl->type == emit_type::skylight) {
					*pdl = dl->next;
					dl->next = skylights;
					skylights = dl;
				} else {
					pdl = &dl->next;
				}
			}
		}
		directlight_t* dl;
		while ((dl = directlights[0]) != nullptr) {
			// since they are in leaf 0, they won't emit a light anyway
			directlights[0] = dl->next;
			free(dl);
		}
		directlights[0] = skylights;
	}
	if (g_sky_lighting_fix) {
		int countlightenvironment = 0;
		int countinfosunlight = 0;
		for (int i = 0; i < g_numentities; i++) {
			entity_t* e = &g_entities[i];
			if (classname_is(e, u8"light_environment")) {
				countlightenvironment++;
			}
			if (classname_is(e, u8"info_sunlight")) {
				countinfosunlight++;
			}
		}
		if (countlightenvironment > 1 && countinfosunlight == 0) {
			// because the map is lit by more than one light_environments,
			// but the game can only recognize one of them when setting
			// sv_skycolor and sv_skyvec.
			Warning(
				"More than one light_environments are in use. Add entity info_sunlight to clarify the sunlight's brightness for in-game model(.mdl) rendering."
			);
		}
	}
}

// =====================================================================================
//  DeleteDirectLights
// =====================================================================================
void DeleteDirectLights() {
	int l;
	directlight_t* dl;

	for (l = 0; l < 1 + g_dmodels[0].visleafs; l++) {
		dl = directlights[l];
		while (dl) {
			directlights[l] = dl->next;
			free(dl);
			dl = directlights[l];
		}
	}

	// AJM: todo: strip light entities out at this point
	// vluzacn: hlvis and hlrad must not modify entity data, because the
	// following procedures are supposed to produce the same bsp file:
	//  1> hlcsg -> hlbsp -> hlvis -> hlrad  (a normal compile)
	//  2) hlcsg -> hlbsp -> hlvis -> hlrad -> hlcsg -onlyents
	//  3) hlcsg -> hlbsp -> hlvis -> hlrad -> hlcsg -onlyents -> hlrad
}

// =====================================================================================
//  GatherSampleLight
// =====================================================================================
int g_numskynormals[SKYLEVELMAX + 1];
float3_array* g_skynormals[SKYLEVELMAX + 1];
float* g_skynormalsizes[SKYLEVELMAX + 1];
using point_t = double3_array;

struct edge_t final {
	bool divided;
	std::array<int, 2> point;
	std::array<int, 2> child;
};

struct triangle_t final {
	std::array<int, 3> edge;
	std::array<int, 3> dir;
};

void CopyToSkynormals(
	int skylevel,
	std::span<point_t const> points,
	std::span<edge_t const> edges,
	std::span<triangle_t const> triangles
) {
	hlassume(points.size() == (1 << (2 * skylevel)) + 2, assume_msg::first);
	hlassume(
		edges.size() == (1 << (2 * skylevel)) * 4 - 4, assume_msg::first
	);
	hlassume(
		triangles.size() == (1 << (2 * skylevel)) * 2, assume_msg::first
	);
	g_numskynormals[skylevel] = points.size();
	g_skynormals[skylevel] = (float3_array*) malloc(
		points.size() * sizeof(float3_array)
	);
	g_skynormalsizes[skylevel] = (float*) malloc(
		points.size() * sizeof(float)
	);
	hlassume(g_skynormals[skylevel] != nullptr, assume_msg::NoMemory);
	hlassume(g_skynormalsizes[skylevel] != nullptr, assume_msg::NoMemory);
	for (std::size_t j = 0; j < points.size(); j++) {
		g_skynormals[skylevel][j] = to_float3(points[j]);
		g_skynormalsizes[skylevel][j] = 0;
	}
	double totalsize = 0;
	for (std::size_t j = 0; j < triangles.size(); j++) {
		std::array<int, 3> pt;
		for (std::size_t k = 0; k < 3; k++) {
			pt[k] = edges[triangles[j].edge[k]].point[triangles[j].dir[k]];
		}
		double3_array tmp = cross_product(points[pt[0]], points[pt[1]]);
		double currentsize = dot_product(tmp, points[pt[2]]);
		hlassume(currentsize > 0, assume_msg::first);
		g_skynormalsizes[skylevel][pt[0]] += currentsize / 3.0;
		g_skynormalsizes[skylevel][pt[1]] += currentsize / 3.0;
		g_skynormalsizes[skylevel][pt[2]] += currentsize / 3.0;
		totalsize += currentsize;
	}
	for (std::size_t j = 0; j < points.size(); j++) {
		g_skynormalsizes[skylevel][j] /= totalsize;
	}
}

void BuildDiffuseNormals() {
	// These arrays are too big to fit in the stack (at least when compiling
	// with Clang 18 for MacOS)
	struct BuildDiffuseNormals_data {
		std::size_t numPoints;
		std::array<point_t, (1 << (2 * SKYLEVELMAX)) + 2> points;

		std::size_t numEdges;
		std::array<edge_t, ((1 << (2 * SKYLEVELMAX)) * 4 - 4)> edges;

		std::size_t numTriangles;
		std::array<triangle_t, (1 << (2 * SKYLEVELMAX)) * 2> triangles;
	};

	auto data = std::make_unique_for_overwrite<BuildDiffuseNormals_data>();
	auto& points{ data->points };
	auto& numPoints{ data->numPoints };
	auto& edges{ data->edges };
	auto& numEdges{ data->numEdges };
	auto& triangles{ data->triangles };
	auto& numTriangles{ data->numTriangles };

	numPoints = 6;
	numEdges = 12;
	numTriangles = 8;

	g_numskynormals[0] = 0;
	g_skynormals[0] = nullptr; // don't use this
	g_skynormalsizes[0] = nullptr;

	points[0][0] = 1, points[0][1] = 0, points[0][2] = 0;
	points[1][0] = -1, points[1][1] = 0, points[1][2] = 0;
	points[2][0] = 0, points[2][1] = 1, points[2][2] = 0;
	points[3][0] = 0, points[3][1] = -1, points[3][2] = 0;
	points[4][0] = 0, points[4][1] = 0, points[4][2] = 1;
	points[5][0] = 0, points[5][1] = 0, points[5][2] = -1;

	edges[0].point[0] = 0, edges[0].point[1] = 2, edges[0].divided = false;
	edges[1].point[0] = 2, edges[1].point[1] = 1, edges[1].divided = false;
	edges[2].point[0] = 1, edges[2].point[1] = 3, edges[2].divided = false;
	edges[3].point[0] = 3, edges[3].point[1] = 0, edges[3].divided = false;
	edges[4].point[0] = 2, edges[4].point[1] = 4, edges[4].divided = false;
	edges[5].point[0] = 4, edges[5].point[1] = 3, edges[5].divided = false;
	edges[6].point[0] = 3, edges[6].point[1] = 5, edges[6].divided = false;
	edges[7].point[0] = 5, edges[7].point[1] = 2, edges[7].divided = false;
	edges[8].point[0] = 4, edges[8].point[1] = 0, edges[8].divided = false;
	edges[9].point[0] = 0, edges[9].point[1] = 5, edges[9].divided = false;
	edges[10].point[0] = 5, edges[10].point[1] = 1,
	edges[10].divided = false;
	edges[11].point[0] = 1, edges[11].point[1] = 4,
	edges[11].divided = false;

	triangles[0].edge[0] = 0, triangles[0].dir[0] = 0,
	triangles[0].edge[1] = 4, triangles[0].dir[1] = 0,
	triangles[0].edge[2] = 8, triangles[0].dir[2] = 0;
	triangles[1].edge[0] = 1, triangles[1].dir[0] = 0,
	triangles[1].edge[1] = 11, triangles[1].dir[1] = 0,
	triangles[1].edge[2] = 4, triangles[1].dir[2] = 1;
	triangles[2].edge[0] = 2, triangles[2].dir[0] = 0,
	triangles[2].edge[1] = 5, triangles[2].dir[1] = 1,
	triangles[2].edge[2] = 11, triangles[2].dir[2] = 1;
	triangles[3].edge[0] = 3, triangles[3].dir[0] = 0,
	triangles[3].edge[1] = 8, triangles[3].dir[1] = 1,
	triangles[3].edge[2] = 5, triangles[3].dir[2] = 0;
	triangles[4].edge[0] = 0, triangles[4].dir[0] = 1,
	triangles[4].edge[1] = 9, triangles[4].dir[1] = 0,
	triangles[4].edge[2] = 7, triangles[4].dir[2] = 0;
	triangles[5].edge[0] = 1, triangles[5].dir[0] = 1,
	triangles[5].edge[1] = 7, triangles[5].dir[1] = 1,
	triangles[5].edge[2] = 10, triangles[5].dir[2] = 0;
	triangles[6].edge[0] = 2, triangles[6].dir[0] = 1,
	triangles[6].edge[1] = 10, triangles[6].dir[1] = 1,
	triangles[6].edge[2] = 6, triangles[6].dir[2] = 1;
	triangles[7].edge[0] = 3, triangles[7].dir[0] = 1,
	triangles[7].edge[1] = 6, triangles[7].dir[1] = 0,
	triangles[7].edge[2] = 9, triangles[7].dir[2] = 1;
	CopyToSkynormals(
		1,
		std::span(points.data(), numPoints),
		std::span(edges.data(), numEdges),
		std::span(triangles.data(), numTriangles)
	);
	for (std::size_t i = 1; i < SKYLEVELMAX; ++i) {
		std::size_t oldnumedges = numEdges;
		for (std::size_t j = 0; j < oldnumedges; ++j) {
			if (edges[j].divided) {
				continue;
			}
			hlassume(
				numPoints < (1 << (2 * SKYLEVELMAX)) + 2, assume_msg::first
			);
			point_t mid = vector_add(
				points[edges[j].point[0]], points[edges[j].point[1]]
			);
			double len = vector_length(mid);
			hlassume(len > 0.2, assume_msg::first);
			mid = vector_scale(mid, 1 / len);
			int p2 = numPoints;
			points[numPoints] = mid;
			numPoints++;
			hlassume(
				numEdges < (1 << (2 * SKYLEVELMAX)) * 4 - 4,
				assume_msg::first
			);
			edges[j].child[0] = numEdges;
			edges[numEdges].divided = false;
			edges[numEdges].point[0] = edges[j].point[0];
			edges[numEdges].point[1] = p2;
			++numEdges;
			hlassume(
				numEdges < (1 << (2 * SKYLEVELMAX)) * 4 - 4,
				assume_msg::first
			);
			edges[j].child[1] = numEdges;
			edges[numEdges].divided = false;
			edges[numEdges].point[0] = p2;
			edges[numEdges].point[1] = edges[j].point[1];
			++numEdges;
			edges[j].divided = true;
		}
		std::size_t oldnumtriangles = numTriangles;
		for (std::size_t j = 0; j < oldnumtriangles; ++j) {
			std::array<std::int32_t, 3> mid;
			for (std::size_t k = 0; k < 3; ++k) {
				hlassume(
					numTriangles < (1 << (2 * SKYLEVELMAX)) * 2,
					assume_msg::first
				);
				mid[k]
					= edges[edges[triangles[j].edge[k]].child[0]].point[1];
				triangles[numTriangles].edge[0]
					= edges[triangles[j].edge[k]]
						  .child[1 - triangles[j].dir[k]];
				triangles[numTriangles].dir[0] = triangles[j].dir[k];
				triangles[numTriangles].edge[1]
					= edges[triangles[j].edge[(k + 1) % 3]]
						  .child[triangles[j].dir[(k + 1) % 3]];
				triangles[numTriangles].dir[1]
					= triangles[j].dir[(k + 1) % 3];
				triangles[numTriangles].edge[2] = numEdges + k;
				triangles[numTriangles].dir[2] = 1;
				++numTriangles;
			}
			for (std::size_t k = 0; k < 3; ++k) {
				hlassume(
					numEdges < (1 << (2 * SKYLEVELMAX)) * 4 - 4,
					assume_msg::first
				);
				triangles[j].edge[k] = numEdges;
				triangles[j].dir[k] = 0;
				edges[numEdges].divided = false;
				edges[numEdges].point[0] = mid[k];
				edges[numEdges].point[1] = mid[(k + 1) % 3];
				++numEdges;
			}
		}
		CopyToSkynormals(
			i + 1,
			std::span(points.data(), numPoints),
			std::span(edges.data(), numEdges),
			std::span(triangles.data(), numTriangles)
		);
	}
}

static void GatherSampleLight(
	float3_array const & pos,
	byte const * const pvs,
	float3_array const & normal,
	std::array<float3_array, ALLSTYLES>& sample,
	std::array<unsigned char, ALLSTYLES>& styles,
	int step,
	int miptex,
	int texlightgap_surfacenum
) {
	int i;
	directlight_t* l;
	float3_array delta;
	float dot, dot2;
	float dist;
	float ratio;
	int style_index;
	int step_match;
	bool sky_used = false;
	float3_array testline_origin;
	std::array<float3_array, ALLSTYLES> adds{};
	int style;
	bool lighting_diversify;
	float lighting_power;
	float lighting_scale;
	lighting_power = g_lightingconeinfo[miptex].power;
	lighting_scale = g_lightingconeinfo[miptex].scale;
	lighting_diversify = (lighting_power != 1.0 || lighting_scale != 1.0);
	float3_array texlightgap_textoworld[2];
	// calculates textoworld
	{
		dface_t* f = &g_dfaces[texlightgap_surfacenum];
		dplane_t const * dp = getPlaneFromFace(f);
		texinfo_t* tex = &g_texinfo[f->texinfo];
		float len;

		for (std::size_t x = 0; x < 2; ++x) {
			texlightgap_textoworld[x] = cross_product(
				tex->vecs[1 - x].xyz, dp->normal
			);
			len = dot_product(texlightgap_textoworld[x], tex->vecs[x].xyz);
			if (fabs(len) < NORMAL_EPSILON) {
				texlightgap_textoworld[x] = {};
			} else {
				texlightgap_textoworld[x] = vector_scale(
					texlightgap_textoworld[x], 1 / len
				);
			}
		}
	}

	for (i = 0; i < 1 + g_dmodels[0].visleafs; i++) {
		l = directlights[i];
		if (!l) {
			continue;
		}
		bool const x = i == 0 ? g_sky_lighting_fix
							  : pvs[(i - 1) >> 3] & (1 << ((i - 1) & 7));
		if (!x) {
			continue;
		}
		for (; l; l = l->next) {
			// skylights work fundamentally differently than normal
			// lights
			if (l->type == emit_type::skylight) {
				if (!g_sky_lighting_fix) {
					if (sky_used) {
						continue;
					}
					sky_used = true;
				}
				do // add sun light
				{
					// check step
					step_match = (int) l->topatch;
					if (step != step_match) {
						continue;
					}
					// check intensity
					if (!(l->intensity[0] || l->intensity[1]
					      || l->intensity[2])) {
						continue;
					}
					// loop over the normals
					for (int j = 0; j < l->numsunnormals; j++) {
						// make sure the angle is okay
						dot = -dot_product(normal, l->sunnormals[j]);
						if (dot <= NORMAL_EPSILON) // ON_EPSILON /
						                           // 10 //--vluzacn
						{
							continue;
						}

						// search back to see if we can hit a sky
						// brush
						delta = vector_scale(
							l->sunnormals[j], -hlrad_bogus_range
						);
						delta = vector_add(pos, delta);
						float3_array skyhit;
						skyhit = delta;
						if (TestLine(pos, delta, skyhit)
						    != contents_t::SKY) {
							continue; // occluded
						}

						float3_array transparency;
						int opaquestyle;
						if (TestSegmentAgainstOpaqueList(
								pos, skyhit, transparency, opaquestyle
							)) {
							continue;
						}

						float3_array add_one;
						if (lighting_diversify) {
							dot = lighting_scale
								* std::pow(dot, lighting_power);
						}
						add_one = vector_scale(
							l->intensity, dot * l->sunnormalweights[j]
						);
						add_one = vector_multiply(add_one, transparency);
						// add to the total brightness of this
						// sample
						style = l->style;
						if (opaquestyle != -1) {
							if (style == 0 || style == opaquestyle) {
								style = opaquestyle;
							} else {
								continue; // dynamic light of other
								          // styles hits this
								          // toggleable opaque
								          // entity, then it
								          // completely vanishes.
							}
						}
						adds[style] = vector_add(adds[style], add_one);
					} // (loop over the normals)
				} while (0);
				do // add sky light
				{
					// check step
					step_match = 0;
					if (g_softsky) {
						step_match = 1;
					}
					if (g_fastmode) {
						step_match = 1;
					}
					if (step != step_match) {
						continue;
					}
					// check intensity
					if (g_indirect_sun <= 0.0
					    || vectors_almost_same(
							   l->diffuse_intensity, float3_array{}
						   )
					        && vectors_almost_same(
								l->diffuse_intensity2, float3_array{}
							)) {
						continue;
					}

					float3_array sky_intensity;

					// loop over the normals
					float3_array* skynormals = g_skynormals
						[g_softsky ? SKYLEVEL_SOFTSKYON
					               : SKYLEVEL_SOFTSKYOFF];
					float* skyweights = g_skynormalsizes
						[g_softsky ? SKYLEVEL_SOFTSKYON
					               : SKYLEVEL_SOFTSKYOFF];
					for (int j = 0; j < g_numskynormals
					                    [g_softsky ? SKYLEVEL_SOFTSKYON
					                               : SKYLEVEL_SOFTSKYOFF];
					     j++) {
						// make sure the angle is okay
						dot = -dot_product(normal, skynormals[j]);
						if (dot <= NORMAL_EPSILON) // ON_EPSILON /
						                           // 10 //--vluzacn
						{
							continue;
						}

						// search back to see if we can hit a sky
						// brush
						delta = vector_scale(
							skynormals[j], -hlrad_bogus_range
						);
						delta = vector_add(delta, pos);
						float3_array skyhit;
						skyhit = delta;
						if (TestLine(pos, delta, skyhit)
						    != contents_t::SKY) {
							continue; // occluded
						}

						float3_array transparency;
						int opaquestyle;
						if (TestSegmentAgainstOpaqueList(
								pos, skyhit, transparency, opaquestyle
							)) {
							continue;
						}

						float factor = std::min(
							std::max(
								(float) 0.0,
								(1 - dot_product(l->normal, skynormals[j]))
									/ 2
							),
							(float) 1.0
						); // how far this piece of sky has deviated
						   // from the sun
						sky_intensity = vector_fma(
							l->diffuse_intensity2,
							factor,
							vector_scale(l->diffuse_intensity, 1 - factor)

						);
						sky_intensity = vector_scale(
							sky_intensity,
							skyweights[j] * g_indirect_sun / 2
						);
						float3_array add_one;
						if (lighting_diversify) {
							dot = lighting_scale
								* std::pow(dot, lighting_power);
						}
						add_one = vector_scale(sky_intensity, dot);
						add_one = vector_multiply(add_one, transparency);
						// add to the total brightness of this
						// sample
						style = l->style;
						if (opaquestyle != -1) {
							if (style == 0 || style == opaquestyle) {
								style = opaquestyle;
							} else {
								continue; // dynamic light of other
								          // styles hits this
								          // toggleable opaque
								          // entity, then it
								          // completely vanishes.
							}
						}
						adds[style] = vector_add(adds[style], add_one);
					} // (loop over the normals)

				} while (0);

			} else // not emit_type::skylight
			{
				step_match = (int) l->topatch;
				if (step != step_match) {
					continue;
				}
				if (!(l->intensity[0] || l->intensity[1] || l->intensity[2]
				    )) {
					continue;
				}
				testline_origin = l->origin;

				delta = vector_subtract(l->origin, pos);
				if (l->type == emit_type::surface) {
					// move emitter back to its plane
					delta = vector_fma(
						l->normal, -PATCH_HUNT_OFFSET, delta
					);
				}
				dist = normalize_vector(delta);
				dot = dot_product(delta, normal);
				//                        if (dot <= 0.0)
				//                            continue;

				if (dist < 1.0) {
					dist = 1.0;
				}

				float3_array add{};
				switch (l->type) {
					case emit_type::point: {
						if (dot <= NORMAL_EPSILON) {
							continue;
						}
						if (lighting_diversify) {
							dot = lighting_scale
								* std::pow(dot, lighting_power);
						}
						float const denominator = dist * dist * l->fade;
						ratio = dot / denominator;
						add = vector_scale(l->intensity, ratio);
						break;
					}

					case emit_type::surface: {
						bool light_behind_surface = false;
						if (dot <= NORMAL_EPSILON) {
							light_behind_surface = true;
						}
						if (lighting_diversify && !light_behind_surface) {
							dot = lighting_scale
								* std::pow(dot, lighting_power);
						}
						dot2 = -dot_product(delta, l->normal);
						// discard the texlight if the spot is too
						// close to the texlight plane
						if (l->texlightgap > 0) {
							float test;

							test = dot2 * dist; // distance from spot
							                    // to texlight plane;
							test -= l->texlightgap
								* fabs(dot_product(
									l->normal,
									texlightgap_textoworld[0]
								)); // maximum distance reduction if
							        // the spot is allowed to shift
							        // l->texlightgap pixels along s
							        // axis
							test -= l->texlightgap
								* fabs(dot_product(
									l->normal,
									texlightgap_textoworld[1]
								)); // maximum distance reduction if
							        // the spot is allowed to shift
							        // l->texlightgap pixels along t
							        // axis
							if (test < -ON_EPSILON) {
								continue;
							}
						}
						if (dot2 * dist <= MINIMUM_PATCH_DISTANCE) {
							continue;
						}
						float range = l->patch_emitter_range;
						if (l->stopdot > 0.0) // stopdot2 > 0.0 or
						                      // stopdot > 0.0
						{
							float range_scale;
							range_scale = 1 - l->stopdot2 * l->stopdot2;
							range_scale = 1
								/ std::sqrt(std::max(
									(float) NORMAL_EPSILON, range_scale
								));
							// range_scale = 1 / sin (cone2)
							range_scale = std::min(
								range_scale, (float) 2
							); // restrict this to 2, because
							   // skylevel has limit.
							range *= range_scale; // because smaller
							                      // cones are more
							                      // likely to
							                      // create the ugly
							                      // grid effect.

							if (dot2 <= l->stopdot2 + NORMAL_EPSILON) {
								if (dist >= range) // use the old method,
								                   // which will merely
								                   // give 0 in this case
								{
									continue;
								}
								ratio = 0.0;
							} else if (dot2 <= l->stopdot) {
								ratio = dot * dot2 * (dot2 - l->stopdot2)
									/ (dist * dist
								       * (l->stopdot - l->stopdot2));
							} else {
								ratio = dot * dot2 / (dist * dist);
							}
						} else {
							ratio = dot * dot2 / (dist * dist);
						}

						// analogous to the one in MakeScales
						// 0.4f is tested to be able to fully
						// eliminate bright spots
						if (ratio * l->patch_area > 0.4f) {
							ratio = 0.4f / l->patch_area;
						}
						if (dist < range - ON_EPSILON) { // do things slow
							if (light_behind_surface) {
								dot = 0.0;
								ratio = 0.0;
							}
							GetAlternateOrigin(
								pos, normal, l->patch, testline_origin
							);
							float sightarea;
							int skylevel = l->patch->emitter_skylevel;
							if (l->stopdot > 0.0) // stopdot2 > 0.0 or
							                      // stopdot > 0.0
							{
								float3_array const & emitnormal
									= getPlaneFromFaceNumber(
										  l->patch->faceNumber
									)
										  ->normal;
								if (l->stopdot2 >= 0.8) // about 37deg
								{
									skylevel += 1; // because the
									               // range is
									               // larger
								}
								sightarea = CalcSightArea_SpotLight(
									pos,
									normal,
									l->patch->winding,
									emitnormal,
									l->stopdot,
									l->stopdot2,
									skylevel,
									lighting_power,
									lighting_scale
								); // because we have doubled the
								   // range
							} else {
								sightarea = CalcSightArea(
									pos,
									normal,
									l->patch->winding,
									skylevel,
									lighting_power,
									lighting_scale
								);
							}

							float frac = dist / range;
							frac = (frac - 0.5)
								* 2; // make a smooth transition
							         // between the two methods
							frac = std::max(
								(float) 0, std::min(frac, (float) 1)
							);

							float ratio2
								= (sightarea / l->patch_area
							    ); // because l->patch->area has
							       // been multiplied into
							       // l->intensity
							ratio = frac * ratio + (1 - frac) * ratio2;
						} else if (light_behind_surface) {
							continue;
						}
						add = vector_scale(l->intensity, ratio);
						break;
					}

					case emit_type::spotlight: {
						if (dot <= NORMAL_EPSILON) {
							continue;
						}
						dot2 = -dot_product(delta, l->normal);
						if (dot2 <= l->stopdot2) {
							continue; // outside light cone
						}

						// Inverse square falloff
						if (lighting_diversify) {
							dot = lighting_scale
								* std::pow(dot, lighting_power);
						}
						float const denominator = dist * dist * l->fade;
						ratio = dot * dot2 / denominator;

						if (dot2 <= l->stopdot) {
							ratio *= (dot2 - l->stopdot2)
								/ (l->stopdot - l->stopdot2);
						}
						add = vector_scale(l->intensity, ratio);
						break;
					}

					default: {
						hlassume(false, assume_msg::BadLightType);
						break;
					}
				}
				if (TestLine(pos, testline_origin) != contents_t::EMPTY) {
					continue;
				}
				float3_array transparency;
				int opaquestyle;
				if (TestSegmentAgainstOpaqueList(
						pos, testline_origin, transparency, opaquestyle
					)) {
					continue;
				}
				add = vector_multiply(add, transparency);
				// add to the total brightness of this sample
				style = l->style;
				if (opaquestyle != -1) {
					if (style == 0 || style == opaquestyle) {
						style = opaquestyle;
					} else {
						continue; // dynamic light of other styles
						          // hits this toggleable opaque
						          // entity, then it completely
						          // vanishes.
					}
				}
				adds[style] = vector_add(adds[style], add);
			} // end emit_type::skylight
		}
	}

	for (style = 0; style < ALLSTYLES; ++style) {
		if (vector_max_element(adds[style]) > g_corings[style] * 0.1) {
			for (style_index = 0; style_index < ALLSTYLES; style_index++) {
				if (styles[style_index] == style
				    || styles[style_index] == 255) {
					break;
				}
			}

			if (style_index == ALLSTYLES) // shouldn't happen
			{
				if (++stylewarningcount >= stylewarningnext) {
					stylewarningnext = stylewarningcount * 2;
					Warning(
						"Too many direct light styles on a face(%f,%f,%f)",
						pos[0],
						pos[1],
						pos[2]
					);
					Warning(
						" total %d warnings for too many styles",
						stylewarningcount
					);
				}
				return;
			}

			if (styles[style_index] == 255) {
				styles[style_index] = style;
			}

			sample[style_index] = vector_add(
				sample[style_index], adds[style]
			);
		} else if (vector_max_element(adds[style])
		           > g_maxdiscardedlight + NORMAL_EPSILON) {
			ThreadLock();
			if (vector_max_element(adds[style])
			    > g_maxdiscardedlight + NORMAL_EPSILON) {
				g_maxdiscardedlight = vector_max_element(adds[style]);
				g_maxdiscardedpos = pos;
			}
			ThreadUnlock();
		}
	}
}

// =====================================================================================
//  AddSampleToPatch
//      Take the sample's collected light and add it back into the
//      apropriate patch for the radiosity pass.
// =====================================================================================
static void AddSamplesToPatches(
	sample_t const ** samples,
	std::array<unsigned char, ALLSTYLES> const & styles,
	int facenum,
	lightinfo_t const * l
) {
	patch_t* patch;
	int i, j, m, k;
	int numtexwindings;

	numtexwindings = 0;
	for (patch = g_face_patches[facenum]; patch; patch = patch->next) {
		numtexwindings++;
	}
	std::unique_ptr<fast_winding[]> texwindings
		= std::make_unique<fast_winding[]>(numtexwindings);
	hlassume(texwindings != nullptr, assume_msg::NoMemory);

	// translate world winding into winding in s,t plane
	for (j = 0, patch = g_face_patches[facenum]; j < numtexwindings;
	     j++, patch = patch->next) {
		fast_winding& w{ texwindings[j] };
		w.reserve_point_storage(patch->winding->size());
		for (int x = 0; x < patch->winding->size(); x++) {
			float s, t;
			SetSTFromSurf(l, patch->winding->point(x).data(), s, t);
			w.push_point({ s, t, 0.0f });
		}
		w.RemoveColinearPoints();
	}

	for (i = 0; i < l->numsurfpt; i++) {
		// prepare clip planes
		float s_vec, t_vec;
		s_vec = l->texmins[0] * TEXTURE_STEP
			+ (i % (l->texsize[0] + 1)) * TEXTURE_STEP;
		t_vec = l->texmins[1] * TEXTURE_STEP
			+ (i / (l->texsize[0] + 1)) * TEXTURE_STEP;

		dplane_t clipplanes[4]{};
		clipplanes[0].normal[0] = 1;
		clipplanes[0].dist = s_vec - 0.5 * TEXTURE_STEP;
		clipplanes[1].normal[0] = -1;
		clipplanes[1].dist = -(s_vec + 0.5 * TEXTURE_STEP);
		clipplanes[2].normal[1] = 1;
		clipplanes[2].dist = t_vec - 0.5 * TEXTURE_STEP;
		clipplanes[3].normal[1] = -1;
		clipplanes[3].dist = -(t_vec + 0.5 * TEXTURE_STEP);

		// clip each patch
		for (j = 0, patch = g_face_patches[facenum]; j < numtexwindings;
		     j++, patch = patch->next) {
			fast_winding w{ texwindings[j] };
			for (k = 0; k < 4; k++) {
				if (w.size()) {
					w.mutating_clip(
						clipplanes[k].normal, clipplanes[k].dist, false
					);
				}
			}
			if (w.size()) {
				// add sample to patch
				float area = w.getArea() / (TEXTURE_STEP * TEXTURE_STEP);
				patch->samples += area;
				for (m = 0; m < ALLSTYLES && styles[m] != 255; m++) {
					int style = styles[m];
					sample_t const * s = &samples[m][i];
					for (k = 0; k < ALLSTYLES
					     && (*patch->totalstyle_all)[k] != 255;
					     k++) {
						if ((*patch->totalstyle_all)[k] == style) {
							break;
						}
					}
					if (k == ALLSTYLES) {
						if (++stylewarningcount >= stylewarningnext) {
							stylewarningnext = stylewarningcount * 2;
							Warning(
								"Too many direct light styles on a face(?,?,?)\n"
							);
							Warning(
								" total %d warnings for too many styles",
								stylewarningcount
							);
						}
					} else {
						if ((*patch->totalstyle_all)[k] == 255) {
							(*patch->totalstyle_all)[k] = style;
						}
						(*patch->samplelight_all)[k] = vector_fma(
							s->light, area, (*patch->samplelight_all)[k]
						);
					}
				}
			}
		}
	}
}

// =====================================================================================
//  GetPhongNormal
// =====================================================================================
void GetPhongNormal(
	int facenum, float3_array const & spot, float3_array& phongnormal
) {
	dface_t const * f = g_dfaces.data() + facenum;
	dplane_t const * p = getPlaneFromFace(f);

	float3_array facenormal{ p->normal };
	phongnormal = facenormal;

	{
		// Calculate modified point normal for surface
		// Use the edge normals iff they are defined.  Bend the surface
		// towards the edge normal(s) Crude first attempt: find nearest edge
		// normal and do a simple interpolation with facenormal. Second
		// attempt: find edge points+center that bound the point and do a
		// three-point triangulation(baricentric) Better third attempt:
		// generate the point normals for all vertices and do baricentric
		// triangulation.

		for (int j = 0; j < f->numedges; j++) {
			float3_array p1;
			float3_array p2;
			float3_array v1;
			float3_array v2;
			float3_array vspot;
			unsigned prev_edge;
			unsigned next_edge;
			float a1;
			float a2;
			float aa;
			float bb;
			float ab;

			if (j) {
				prev_edge = f->firstedge
					+ ((j + f->numedges - 1) % f->numedges);
			} else {
				prev_edge = f->firstedge + f->numedges - 1;
			}

			if ((j + 1) != f->numedges) {
				next_edge = f->firstedge + ((j + 1) % f->numedges);
			} else {
				next_edge = f->firstedge;
			}

			int e = g_dsurfedges[f->firstedge + j];
			int e1 = g_dsurfedges[prev_edge];
			int e2 = g_dsurfedges[next_edge];

			edgeshare_t* es = &g_edgeshare[abs(e)];
			edgeshare_t* es1 = &g_edgeshare[abs(e1)];
			edgeshare_t* es2 = &g_edgeshare[abs(e2)];

			if ((!es->smooth || es->coplanar)
			    && (!es1->smooth || es1->coplanar)
			    && (!es2->smooth || es2->coplanar)) {
				continue;
			}

			if (e > 0) {
				p1 = g_dvertexes[g_dedges[e].v[0]].point;
				p2 = g_dvertexes[g_dedges[e].v[1]].point;
			} else {
				p1 = g_dvertexes[g_dedges[-e].v[1]].point;
				p2 = g_dvertexes[g_dedges[-e].v[0]].point;
			}

			// Adjust for origin-based models
			p1 = vector_add(p1, g_face_offset[facenum]);
			p2 = vector_add(p2, g_face_offset[facenum]);
			// Split every edge into two parts
			for (std::size_t s = 0; s < 2; ++s) {
				float3_array s1 = s == 0 ? p1 : p2;
				float3_array const edgeCenter = midpoint_between(p1, p2);

				v1 = vector_subtract(s1, g_face_centroids[facenum]);
				v2 = vector_subtract(edgeCenter, g_face_centroids[facenum]);
				vspot = vector_subtract(spot, g_face_centroids[facenum]);

				aa = dot_product(v1, v1);
				bb = dot_product(v2, v2);
				ab = dot_product(v1, v2);
				a1 = (bb * dot_product(v1, vspot)
				      - ab * dot_product(vspot, v2))
					/ (aa * bb - ab * ab);
				a2 = (dot_product(vspot, v2) - a1 * ab) / bb;

				// Test center to sample vector for inclusion between center
				// to vertex vectors (Use dot product of vectors)
				if (a1 >= -0.01 && a2 >= -0.01) {
					// calculate distance from edge to pos
					float3_array n1;
					float3_array n2;

					if (es->smooth) {
						if (s == 0) {
							n1 = es->vertex_normal[e > 0 ? 0 : 1];
						} else {
							n1 = es->vertex_normal[e > 0 ? 1 : 0];
						}
					} else if (s == 0 && es1->smooth) {
						n1 = es1->vertex_normal[e1 > 0 ? 1 : 0];
					} else if (s == 1 && es2->smooth) {
						n1 = es2->vertex_normal[e2 > 0 ? 0 : 1];
					} else {
						n1 = facenormal;
					}

					if (es->smooth) {
						n2 = es->interface_normal;
					} else {
						n2 = facenormal;
					}

					// Interpolate between the center and edge normals based
					// on sample position
					phongnormal = vector_scale(facenormal, 1.0f - a1 - a2);
					phongnormal = vector_add(
						phongnormal, vector_scale(n1, a1)
					);
					phongnormal = vector_add(
						phongnormal, vector_scale(n2, a2)
					);
					normalize_vector(phongnormal);
					break;
				}
			}
		}
	}
}

auto const s_circuscolors = std::array{
	float3_array{ 100000.0, 100000.0, 100000.0 }, // white
	float3_array{ 100000.0, 0.0, 0.0 },           // red
	float3_array{ 0.0, 100000.0, 0.0 },           // green
	float3_array{ 0.0, 0.0, 100000.0 },           // blue
	float3_array{ 0.0, 100000.0, 100000.0 },      // cyan
	float3_array{ 100000.0, 0.0, 100000.0 },      // magenta
	float3_array{ 100000.0, 100000.0, 0.0 }       // yellow
};

// =====================================================================================
//  BuildFacelights
// =====================================================================================
void CalcLightmap(
	lightinfo_t* l, std::array<unsigned char, ALLSTYLES>& styles
) {
	int facenum;
	int i, j;
	byte pvs[(MAX_MAP_LEAFS + 7) / 8];
	int lastoffset;
	byte pvs2[(MAX_MAP_LEAFS + 7) / 8];
	int lastoffset2;

	facenum = l->surfnum;
	memset(
		l->lmcache,
		0,
		l->lmcachewidth * l->lmcacheheight
			* sizeof(std::array<float3_array, ALLSTYLES>)
	);

	// for each sample whose light we need to calculate
	for (i = 0; i < l->lmcachewidth * l->lmcacheheight; i++) {
		float s, t;
		float s_vec, t_vec;
		int nearest_s, nearest_t;
		float3_array spot;
		float square[2][2];  // the max possible range in which this sample
		                     // point affects the lighting on a face
		float3_array surfpt; // the point on the surface (with no
		                     // HUNT_OFFSET applied), used for getting phong
		                     // normal and doing patch interpolation
		int surface;
		float3_array pointnormal;
		bool blocked;
		float3_array spot2;
		float3_array pointnormal2;
		std::array<float3_array, ALLSTYLES>* sampled;
		float3_array* normal_out;
		bool nudged;
		wallflags_t* wallflags_out;

		// prepare input parameter and output parameter
		{
			s = ((i % l->lmcachewidth) - l->lmcache_offset)
				/ (float) l->lmcache_density;
			t = ((i / l->lmcachewidth) - l->lmcache_offset)
				/ (float) l->lmcache_density;
			s_vec = l->texmins[0] * TEXTURE_STEP + s * TEXTURE_STEP;
			t_vec = l->texmins[1] * TEXTURE_STEP + t * TEXTURE_STEP;
			nearest_s = std::max(
				0, std::min((int) floor(s + 0.5), l->texsize[0])
			);
			nearest_t = std::max(
				0, std::min((int) floor(t + 0.5), l->texsize[1])
			);
			sampled = &l->lmcache[i];
			normal_out = &l->lmcache_normal[i];
			wallflags_out = &l->lmcache_wallflags[i];
			//
			// The following graph illustrates the range in which a sample
			// point can affect the lighting of a face when g_blur = 1.5 and
			// g_extra = on
			//              X : the sample point. They are placed on every
			//              TEXTURE_STEP/lmcache_density (=16.0/3) texture
			//              pixels. We calculate light for each sample
			//              point, which is the main time sink.
			//              + : the lightmap pixel. They are placed on every
			//              TEXTURE_STEP (=16.0) texture pixels, which is
			//              hard coded inside the GoldSrc engine. Their
			//              brightness are averaged from the sample points
			//              in a square with size g_blur*TEXTURE_STEP. o :
			//              indicates that this lightmap pixel is affected
			//              by the sample point 'X'. The higher g_blur, the
			//              more 'o'.
			//       |/ / / | : indicates that the brightness of this area
			//       is affected by the lightmap pixels 'o' and hence by the
			//       sample point 'X'. This is because the engine uses
			//       bilinear interpolation to display the lightmap.
			//
			//    ==============================================================================================================================================
			//    || +     +     +     +     +     + || +     +     +     +
			//    +     + || +     +     +     +     +     + || +     + + +
			//    +     + ||
			//    ||                                 || || || ||
			//    ||                                 || || || ||
			//    || +     +-----+-----+     +     + || +
			//    +-----+-----+-----+     + || +     +-----+-----+-----+ +
			//    || +     +     +-----+-----+     + ||
			//    ||       | / / / / / |             ||       | / / / / / /
			//    / / |       ||       | / / / / / / / / |       || | / / /
			//    / / |       ||
			//    ||       |/ / / / / /|             ||       |/ / / / / / /
			//    / /|       ||       |/ / / / / / / / /|       || |/ / / /
			//    / /|       ||
			//    || +     + / / X / / +     +     + || +     + / / o X / o
			//    / / +     + || +     + / / o / X o / / +     + || +     +
			//    + / / X / / +     + ||
			//    ||       |/ / / / / /|             ||       |/ / / / / / /
			//    / /|       ||       |/ / / / / / / / /|       || |/ / / /
			//    / /|       ||
			//    ||       | / / / / / |             ||       | / / / / / /
			//    / / |       ||       | / / / / / / / / |       || | / / /
			//    / / |       ||
			//    || +     +-----+-----+     +     + || +
			//    +-----+-----+-----+     + || +     +-----+-----+-----+ +
			//    || +     +     +-----+-----+     + ||
			//    ||                                 || || || ||
			//    ||                                 || || || ||
			//    || +     +     +     +     +     + || +     +     +     +
			//    +     + || +     +     +     +     +     + || +     + + +
			//    +     + ||
			//    ==============================================================================================================================================
			//    || +     +     +     +     +     + || +     +     +     +
			//    +     + || +     +     +     +     +     + || +     + + +
			//    +     + ||
			//    ||                                 || || || ||
			//    ||                                 || || || ||
			//    || +     +-----+-----+     +     + || +
			//    +-----+-----+-----+     + || +     +-----+-----+-----+ +
			//    || +     +     +-----+-----+     + ||
			//    ||       | / / / / / |             ||       | / / / / / /
			//    / / |       ||       | / / / / / / / / |       || | / / /
			//    / / |       ||
			//    ||       |/ / / / / /|             ||       |/ / / / / / /
			//    / /|       ||       |/ / / / / / / / /|       || |/ / / /
			//    / /|       ||
			//    || +     + / / o / / +     +     + || +     + / / o / / o
			//    / / +     + || +     + / / o / / o / / +     + || +     +
			//    + / / o / / +     + ||
			//    ||       |/ / /X/ / /|             ||       |/ / / /X/ / /
			//    / /|       ||       |/ / / / /X/ / / /|       || |/ / /X/
			//    / /|       ||
			//    ||       | / / / / / |             ||       | / / / / / /
			//    / / |       ||       | / / / / / / / / |       || | / / /
			//    / / |       ||
			//    || +     +/ / /o/ / /+     +     + || +     +/ / /o/ / /o/
			//    / /+     + || +     +/ / /o/ / /o/ / /+     + || +     +
			//    +/ / /o/ / /+     + ||
			//    ||       | / / / / / |             ||       | / / / / / /
			//    / / |       ||       | / / / / / / / / |       || | / / /
			//    / / |       ||
			//    ||       |/ / / / / /|             ||       |/ / / / / / /
			//    / /|       ||       |/ / / / / / / / /|       || |/ / / /
			//    / /|       ||
			//    || +     +-----+-----+     +     + || +
			//    +-----+-----+-----+     + || +     +-----+-----+-----+ +
			//    || +     +     +-----+-----+     + ||
			//    ==============================================================================================================================================
			//
			square[0][0] = l->texmins[0] * TEXTURE_STEP
				+ ceil(
					  s
					  - (l->lmcache_side + 0.5) / (float) l->lmcache_density
				  ) * TEXTURE_STEP
				- TEXTURE_STEP;
			square[0][1] = l->texmins[1] * TEXTURE_STEP
				+ ceil(
					  t
					  - (l->lmcache_side + 0.5) / (float) l->lmcache_density
				  ) * TEXTURE_STEP
				- TEXTURE_STEP;
			square[1][0] = l->texmins[0] * TEXTURE_STEP
				+ floor(
					  s
					  + (l->lmcache_side + 0.5) / (float) l->lmcache_density
				  ) * TEXTURE_STEP
				+ TEXTURE_STEP;
			square[1][1] = l->texmins[1] * TEXTURE_STEP
				+ floor(
					  t
					  + (l->lmcache_side + 0.5) / (float) l->lmcache_density
				  ) * TEXTURE_STEP
				+ TEXTURE_STEP;
		}
		// find world's position for the sample
		{
			{
				blocked = false;
				if (SetSampleFromST(
						surfpt,
						spot,
						&surface,
						&nudged,
						l,
						s_vec,
						t_vec,
						square,
						g_face_lightmode[facenum]
					)
				    == light_flag::outside) {
					j = nearest_s + (l->texsize[0] + 1) * nearest_t;
					if (l->surfpt_lightoutside[j]) {
						blocked = true;
					} else {
						// the area this light sample has effect on is
						// completely covered by solid, so take whatever
						// valid position.
						surfpt = l->surfpt[j];
						spot = l->surfpt_position[j];
						surface = l->surfpt_surface[j];
					}
				}
			}
			if (l->translucent_b) {
				dplane_t const * surfaceplane = getPlaneFromFaceNumber(
					surface
				);
				fast_winding surfacewinding{ g_dfaces[surface] };

				spot2 = spot;
				surfacewinding.add_offset_to_points(g_face_offset[surface]);
				if (!point_in_winding_noedge(
						surfacewinding, *surfaceplane, spot2, 0.2
					)) {
					snap_to_winding_noedge(
						surfacewinding, *surfaceplane, spot2, 0.2, 4 * 0.2
					);
				}
				spot2 = vector_fma(
					surfaceplane->normal,
					-(g_translucentdepth + 2 * DEFAULT_HUNT_OFFSET),
					spot2
				);
			}
			*wallflags_out = wallflags_t::none;
			if (blocked) {
				*wallflags_out
					|= (wallflags_t::blocked | wallflags_t::nudged);
			}
			if (nudged) {
				*wallflags_out |= wallflags_t::nudged;
			}
		}
		// calculate normal for the sample
		{
			GetPhongNormal(surface, surfpt, pointnormal);
			if (l->translucent_b) {
				pointnormal2 = negate_vector(pointnormal);
			}
			*normal_out = pointnormal;
		}
		// calculate visibility for the sample
		{
			if (!g_visdatasize) {
				if (i == 0) {
					memset(pvs, 255, (g_dmodels[0].visleafs + 7) / 8);
				}
			} else {
				dleaf_t* const leaf = PointInLeaf(spot);
				int thisoffset = leaf->visofs;
				if (i == 0 || thisoffset != lastoffset) {
					if (thisoffset == -1) {
						memset(pvs, 0, (g_dmodels[0].visleafs + 7) / 8);
					} else {
						DecompressVis(
							(byte const *) &g_dvisdata[leaf->visofs],
							pvs,
							sizeof(pvs)
						);
					}
				}
				lastoffset = thisoffset;
			}
			if (l->translucent_b) {
				if (!g_visdatasize) {
					if (i == 0) {
						memset(pvs2, 255, (g_dmodels[0].visleafs + 7) / 8);
					}
				} else {
					dleaf_t* leaf2 = PointInLeaf(spot2);
					int thisoffset2 = leaf2->visofs;
					if (i == 0 || thisoffset2 != lastoffset2) {
						if (thisoffset2 == -1) {
							memset(
								pvs2, 0, (g_dmodels[0].visleafs + 7) / 8
							);
						} else {
							DecompressVis(
								(byte*) &g_dvisdata[leaf2->visofs],
								pvs2,
								sizeof(pvs2)
							);
						}
					}
					lastoffset2 = thisoffset2;
				}
			}
		}
		// gather light
		{
			if (!blocked) {
				GatherSampleLight(
					spot,
					pvs,
					pointnormal,
					*sampled,
					styles,
					0,
					l->miptex,
					surface
				);
			}
			if (l->translucent_b) {
				std::array<float3_array, ALLSTYLES> sampled2{};
				if (!blocked) {
					GatherSampleLight(
						spot2,
						pvs2,
						pointnormal2,
						sampled2,
						styles,
						0,
						l->miptex,
						surface
					);
				}
				for (j = 0; j < ALLSTYLES && styles[j] != 255; j++) {
					for (int x = 0; x < 3; x++) {
						(*sampled)[j][x] = (1.0 - l->translucent_v[x])
								* (*sampled)[j][x]
							+ l->translucent_v[x] * sampled2[j][x];
					}
				}
			}
			if (g_drawnudge) {
				for (j = 0; j < ALLSTYLES && styles[j] != 255; j++) {
					if (blocked && styles[j] == 0) {
						(*sampled)[j][0] = 200;
						(*sampled)[j][1] = 0;
						(*sampled)[j][2] = 0;
					} else if (nudged
					           && styles[j] == 0) // we assume style 0 is
					                              // always present
					{
						(*sampled)[j].fill(100.0f);
					} else {
						(*sampled)[j] = {};
					}
				}
			}
		}
	}
}

void BuildFacelights(int const facenum) {
	dface_t* f;
	sample_t* fl_samples[ALLSTYLES];
	lightinfo_t l;
	int i;
	int k;
	sample_t* s;
	patch_t* patch;
	dplane_t const * plane;
	byte pvs[(MAX_MAP_LEAFS + 7) / 8];
	int thisoffset = -1, lastoffset = -1;
	int lightmapwidth;
	int lightmapheight;
	int size;
	float3_array spot2, normal2;
	float3_array delta;
	byte pvs2[(MAX_MAP_LEAFS + 7) / 8];
	int thisoffset2 = -1, lastoffset2 = -1;

	wallflags_t* sample_wallflags;

	f = &g_dfaces[facenum];

	//
	// some surfaces don't need lightmaps
	//
	f->lightofs = -1;

	if (g_texinfo[f->texinfo].has_special_flag()) {
		f->styles.fill(255);
		return; // non-lit texture
	}

	std::array<unsigned char, ALLSTYLES> f_styles;
	f_styles.fill(255);
	f_styles[0] = 0;
	if (g_face_patches[facenum] && g_face_patches[facenum]->emitstyle) {
		f_styles[1] = g_face_patches[facenum]->emitstyle;
	}

	l = lightinfo_t{};

	l.surfnum = facenum;
	l.face = f;

	l.translucent_v = g_translucenttextures[g_texinfo[f->texinfo].miptex];
	l.translucent_b = !vectors_almost_same(l.translucent_v, float3_array{});
	l.miptex = g_texinfo[f->texinfo].miptex;

	//
	// rotate plane
	//
	plane = getPlaneFromFace(f);
	l.facenormal = plane->normal;
	l.facedist = plane->dist;

	CalcFaceVectors(&l);
	CalcFaceExtents(&l);
	CalcPoints(&l);
	CalcLightmap(&l, f_styles);

	lightmapwidth = l.texsize[0] + 1;
	lightmapheight = l.texsize[1] + 1;

	size = lightmapwidth * lightmapheight;
	hlassume(size <= MAX_SINGLEMAP, assume_msg::exceeded_MAX_SINGLEMAP);

	facelight[facenum].numsamples = l.numsurfpt;

	for (k = 0; k < ALLSTYLES; k++) {
		fl_samples[k] = (sample_t*) calloc(l.numsurfpt, sizeof(sample_t));
		hlassume(fl_samples[k] != nullptr, assume_msg::NoMemory);
	}
	for (patch = g_face_patches[facenum]; patch; patch = patch->next) {
		hlassume(
			patch->totalstyle_all = (std::array<unsigned char, ALLSTYLES>*)
				malloc(sizeof(std::array<unsigned char, ALLSTYLES>)),
			assume_msg::NoMemory
		);
		hlassume(
			patch->samplelight_all = (std::array<float3_array, ALLSTYLES>*)
				malloc(sizeof(std::array<float3_array, ALLSTYLES>)),
			assume_msg::NoMemory
		);
		hlassume(
			patch->totallight_all = (std::array<float3_array, ALLSTYLES>*)
				malloc(sizeof(std::array<float3_array, ALLSTYLES>)),
			assume_msg::NoMemory
		);
		hlassume(
			patch->directlight_all = (std::array<float3_array, ALLSTYLES>*)
				malloc(sizeof(std::array<float3_array, ALLSTYLES>)),
			assume_msg::NoMemory
		);
		patch->totalstyle_all->fill(255);
		(*patch->totalstyle_all)[0] = 0;
		patch->samplelight_all->fill({});
		patch->totallight_all->fill({});
		patch->directlight_all->fill({});
	}

	sample_wallflags = (wallflags_t*) malloc(
		(2 * l.lmcache_side + 1) * (2 * l.lmcache_side + 1)
		* sizeof(wallflags_t)
	);
	float3_array const * spot = &l.surfpt[0];
	for (i = 0; i < l.numsurfpt; i++, ++spot) {
		for (k = 0; k < ALLSTYLES; k++) {
			fl_samples[k][i].pos = *spot;
			fl_samples[k][i].surface = l.surfpt_surface[i];
		}

		int s, t, pos;
		int s_center, t_center;
		float sizehalf;
		float weighting, subsamples;
		float3_array centernormal;
		float weighting_correction;
		int pass;
		s_center = (i % lightmapwidth) * l.lmcache_density
			+ l.lmcache_offset;
		t_center = (i / lightmapwidth) * l.lmcache_density
			+ l.lmcache_offset;
		sizehalf = 0.5 * g_blur * l.lmcache_density;
		subsamples = 0.0;
		centernormal
			= l.lmcache_normal[s_center + l.lmcachewidth * t_center];
		if (g_bleedfix && !g_drawnudge) {
			int s_origin = s_center;
			int t_origin = t_center;
			for (s = s_center - l.lmcache_side;
			     s <= s_center + l.lmcache_side;
			     s++) {
				for (t = t_center - l.lmcache_side;
				     t <= t_center + l.lmcache_side;
				     t++) {
					wallflags_t* pwallflags
						= &sample_wallflags
							  [(s - s_center + l.lmcache_side)
					           + (2 * l.lmcache_side + 1)
					               * (t - t_center + l.lmcache_side)];
					*pwallflags
						= l.lmcache_wallflags[s + l.lmcachewidth * t];
				}
			}
			// project the "shadow" from the origin point
			for (s = s_center - l.lmcache_side;
			     s <= s_center + l.lmcache_side;
			     s++) {
				for (t = t_center - l.lmcache_side;
				     t <= t_center + l.lmcache_side;
				     t++) {
					wallflags_t* pwallflags
						= &sample_wallflags
							  [(s - s_center + l.lmcache_side)
					           + (2 * l.lmcache_side + 1)
					               * (t - t_center + l.lmcache_side)];
					int coord[2] = { s - s_origin, t - t_origin };
					int axis = abs(coord[0]) >= abs(coord[1]) ? 0 : 1;
					int sign = coord[axis] >= 0 ? 1 : -1;
					bool blocked1 = false;
					bool blocked2 = false;
					for (int dist = 1; dist < abs(coord[axis]); dist++) {
						int test1[2];
						int test2[2];
						test1[axis] = test2[axis] = sign * dist;
						double intercept = (double) coord[1 - axis]
							* (double) test1[axis] / (double) coord[axis];
						test1[1 - axis] = (int) floor(intercept + 0.01);
						test2[1 - axis] = (int) ceil(intercept - 0.01);
						if (abs(test1[0] + s_origin - s_center)
						        > l.lmcache_side
						    || abs(test1[1] + t_origin - t_center)
						        > l.lmcache_side
						    || abs(test2[0] + s_origin - s_center)
						        > l.lmcache_side
						    || abs(test2[1] + t_origin - t_center)
						        > l.lmcache_side) {
							Warning(
								"HLRAD_AVOIDWALLBLEED: internal error. Contact vluzacn@163.com concerning this issue."
							);
							continue;
						}
						wallflags_t wallflags1 = sample_wallflags
							[(test1[0] + s_origin - s_center
						      + l.lmcache_side)
						     + (2 * l.lmcache_side + 1)
						         * (test1[1] + t_origin - t_center
						            + l.lmcache_side)];
						wallflags_t wallflags2 = sample_wallflags
							[(test2[0] + s_origin - s_center
						      + l.lmcache_side)
						     + (2 * l.lmcache_side + 1)
						         * (test2[1] + t_origin - t_center
						            + l.lmcache_side)];
						if (are_flags_set(
								wallflags1 & wallflags_t::nudged
							)) {
							blocked1 = true;
						}
						if (are_flags_set(
								wallflags2 & wallflags_t::nudged
							)) {
							blocked2 = true;
						}
					}
					if (blocked1 && blocked2) {
						*pwallflags |= wallflags_t::shadowed;
					}
				}
			}
		}
		for (pass = 0; pass < 2; pass++) {
			for (s = s_center - l.lmcache_side;
			     s <= s_center + l.lmcache_side;
			     s++) {
				for (t = t_center - l.lmcache_side;
				     t <= t_center + l.lmcache_side;
				     t++) {
					weighting
						= (std::min((float) 0.5, sizehalf - (s - s_center))
					       - std::max(
							   (float) -0.5, -sizehalf - (s - s_center)
						   ))
						* (std::min((float) 0.5, sizehalf - (t - t_center))
					       - std::max(
							   (float) -0.5, -sizehalf - (t - t_center)
						   ));
					if (g_bleedfix && !g_drawnudge) {
						wallflags_t wallflags = sample_wallflags
							[(s - s_center + l.lmcache_side)
						     + (2 * l.lmcache_side + 1)
						         * (t - t_center + l.lmcache_side)];
						if (are_flags_set(
								wallflags
								& (wallflags_t::blocked
						           | wallflags_t::shadowed)
							)) {
							continue;
						}
						if (are_flags_set(
								wallflags & wallflags_t::nudged
							)) {
							if (pass == 0) {
								continue;
							}
						}
					}
					pos = s + l.lmcachewidth * t;
					// when blur distance (g_blur) is large, the subsample
					// can be very far from the original lightmap sample
					// (aligned with interval TEXTURE_STEP (16.0)) in some
					// cases such as a thin cylinder, the subsample can even
					// grow into the opposite side as a result, when exposed
					// to a directional light, the light on the cylinder may
					// "leak" into the opposite dark side this correction
					// limits the effect of blur distance when the normal
					// changes very fast this correction will not break the
					// smoothness that HLRAD_GROWSAMPLE ensures
					weighting_correction = dot_product(
						l.lmcache_normal[pos], centernormal
					);
					weighting_correction = (weighting_correction > 0)
						? weighting_correction * weighting_correction
						: 0;
					weighting = weighting * weighting_correction;
					for (std::size_t j = 0;
					     j < ALLSTYLES && f_styles[j] != 255;
					     j++) {
						fl_samples[j][i].light = vector_fma(
							l.lmcache[pos][j],
							weighting,
							fl_samples[j][i].light
						);
					}
					subsamples += weighting;
				}
			}
			if (subsamples > NORMAL_EPSILON) {
				break;
			} else {
				subsamples = 0.0;
				for (std::size_t j = 0; j < ALLSTYLES && f_styles[j] != 255;
				     j++) {
					fl_samples[j][i].light = {};
				}
			}
		}
		if (subsamples > 0) {
			for (std::size_t j = 0; j < ALLSTYLES && f_styles[j] != 255;
			     j++) {
				fl_samples[j][i].light = vector_scale(
					fl_samples[j][i].light, 1.0f / subsamples
				);
			}
		}
	} // end of i loop
	free(sample_wallflags);

	// average up the direct light on each patch for radiosity
	AddSamplesToPatches(
		(sample_t const **) fl_samples, f_styles, facenum, &l
	);
	{
		for (patch = g_face_patches[facenum]; patch; patch = patch->next) {
			// LRC:
			unsigned istyle;
			if (patch->samples <= ON_EPSILON * ON_EPSILON) {
				patch->samples = 0.0;
			}
			if (patch->samples) {
				for (istyle = 0; istyle < ALLSTYLES
				     && (*patch->totalstyle_all)[istyle] != 255;
				     istyle++) {
					float3_array v = vector_scale(
						(*patch->samplelight_all)[istyle],
						1.0f / patch->samples
					);
					(*patch->directlight_all)[istyle] = vector_add(
						(*patch->directlight_all)[istyle], v
					);
				}
			}
			// LRC (ends)
		}
	}
	for (patch = g_face_patches[facenum]; patch; patch = patch->next) {
		// get the PVS for the pos to limit the number of checks
		if (!g_visdatasize) {
			memset(pvs, 255, (g_dmodels[0].visleafs + 7) / 8);
			lastoffset = -1;
		} else {
			dleaf_t* leaf = PointInLeaf(patch->origin);

			thisoffset = leaf->visofs;
			if (patch == g_face_patches[facenum]
			    || thisoffset != lastoffset) {
				if (thisoffset == -1) {
					memset(pvs, 0, (g_dmodels[0].visleafs + 7) / 8);
				} else {
					DecompressVis(
						(byte*) &g_dvisdata[leaf->visofs], pvs, sizeof(pvs)
					);
				}
			}
			lastoffset = thisoffset;
		}
		if (l.translucent_b) {
			if (!g_visdatasize) {
				memset(pvs2, 255, (g_dmodels[0].visleafs + 7) / 8);
				lastoffset2 = -1;
			} else {
				spot2 = vector_fma(
					l.facenormal,
					-(g_translucentdepth + 2 * PATCH_HUNT_OFFSET),
					patch->origin
				);
				dleaf_t* leaf2 = PointInLeaf(spot2);

				thisoffset2 = leaf2->visofs;
				if (l.numsurfpt == 0 || thisoffset2 != lastoffset2) {
					if (thisoffset2 == -1) {
						memset(pvs2, 0, (g_dmodels[0].visleafs + 7) / 8);
					} else {
						DecompressVis(
							(byte*) &g_dvisdata[leaf2->visofs],
							pvs2,
							sizeof(pvs2)
						);
					}
				}
				lastoffset2 = thisoffset2;
			}
			std::array<float3_array, ALLSTYLES> frontsampled{};
			std::array<float3_array, ALLSTYLES> backsampled{};
			normal2 = negate_vector(l.facenormal);
			GatherSampleLight(
				patch->origin,
				pvs,
				l.facenormal,
				frontsampled,
				(*patch->totalstyle_all),
				1,
				l.miptex,
				facenum
			);
			GatherSampleLight(
				spot2,
				pvs2,
				normal2,
				backsampled,
				(*patch->totalstyle_all),
				1,
				l.miptex,
				facenum
			);
			for (std::size_t j = 0;
			     j < ALLSTYLES && (*patch->totalstyle_all)[j] != 255;
			     j++) {
				for (int x = 0; x < 3; x++) {
					(*patch->totallight_all)[j][x] += (1.0
					                                   - l.translucent_v[x])
							* frontsampled[j][x]
						+ l.translucent_v[x] * backsampled[j][x];
				}
			}
		} else {
			GatherSampleLight(
				patch->origin,
				pvs,
				l.facenormal,
				*patch->totallight_all,
				*patch->totalstyle_all,
				1,
				l.miptex,
				facenum
			);
		}
	}

	// add an ambient term if desired
	if (g_ambient[0] || g_ambient[1] || g_ambient[2]) {
		for (std::size_t j = 0; j < ALLSTYLES && f_styles[j] != 255; j++) {
			if (f_styles[j] == 0) {
				s = fl_samples[j];
				for (i = 0; i < l.numsurfpt; i++, s++) {
					s->light = vector_add(s->light, g_ambient);
				}
				break;
			}
		}
	}

	// add circus lighting for finding black lightmaps
	if (g_circus) {
		for (std::size_t j = 0; j < ALLSTYLES && f_styles[j] != 255; j++) {
			if (f_styles[j] == 0) {
				int amt = 7;

				s = fl_samples[j];

				while ((l.numsurfpt % amt) == 0) {
					amt--;
				}
				if (amt < 2) {
					amt = 7;
				}

				for (i = 0; i < l.numsurfpt; i++, s++) {
					if ((s->light[0] == 0) && (s->light[1] == 0)
					    && (s->light[2] == 0)) {
						s->light = vector_add(
							s->light, s_circuscolors[i % amt]
						);
					}
				}
				break;
			}
		}
	}

	// light from dlight_threshold and above is sent out, but the
	// texture itself should still be full bright

	// if( vector_average( face_patches[facenum]->baselight ) >=
	// dlight_threshold)       // Now all lighted surfaces glow
	// Texlights
	if (g_face_patches[facenum]) {
		std::size_t j;
		for (j = 0; j < ALLSTYLES && f_styles[j] != 255; j++) {
			if (f_styles[j] == g_face_patches[facenum]->emitstyle) {
				break;
			}
		}
		if (j == ALLSTYLES) {
			if (++stylewarningcount >= stylewarningnext) {
				stylewarningnext = stylewarningcount * 2;
				Warning("Too many direct light styles on a face(?,?,?)");
				Warning(
					" total %d warnings for too many styles",
					stylewarningcount
				);
			}
		} else {
			if (f_styles[j] == 255) {
				f_styles[j] = g_face_patches[facenum]->emitstyle;
			}

			s = fl_samples[j];
			for (i = 0; i < l.numsurfpt; i++, s++) {
				s->light = vector_add(
					s->light, g_face_patches[facenum]->baselight
				);
			}
		}
	}

	// samples
	{
		facelight_t* fl = &facelight[facenum];
		float maxlights[ALLSTYLES];
		for (std::size_t j = 0; j < ALLSTYLES && f_styles[j] != 255; j++) {
			maxlights[j] = 0;
			for (i = 0; i < fl->numsamples; i++) {
				float b = vector_max_element(fl_samples[j][i].light);
				maxlights[j] = std::max(maxlights[j], b);
			}
			if (maxlights[j] <= g_corings[f_styles[j]]
			        * 0.1) // light is too dim, discard this style to
			               // reduce RAM usage
			{
				if (maxlights[j] > g_maxdiscardedlight + NORMAL_EPSILON) {
					ThreadLock();
					if (maxlights[j]
					    > g_maxdiscardedlight + NORMAL_EPSILON) {
						g_maxdiscardedlight = maxlights[j];
						g_maxdiscardedpos = g_face_centroids[facenum];
					}
					ThreadUnlock();
				}
				maxlights[j] = 0;
			}
		}
		for (k = 0; k < MAXLIGHTMAPS; k++) {
			int bestindex = -1;
			if (k == 0) {
				bestindex = 0;
			} else {
				float bestmaxlight = 0;
				for (std::size_t j = 1; j < ALLSTYLES && f_styles[j] != 255;
				     j++) {
					if (maxlights[j] > bestmaxlight + NORMAL_EPSILON) {
						bestmaxlight = maxlights[j];
						bestindex = j;
					}
				}
			}
			if (bestindex != -1) {
				maxlights[bestindex] = 0;
				f->styles[k] = f_styles[bestindex];
				fl->samples[k] = (sample_t*) malloc(
					fl->numsamples * sizeof(sample_t)
				);
				hlassume(fl->samples[k] != nullptr, assume_msg::NoMemory);
				memcpy(
					fl->samples[k],
					fl_samples[bestindex],
					fl->numsamples * sizeof(sample_t)
				);
			} else {
				f->styles[k] = 255;
				fl->samples[k] = nullptr;
			}
		}
		for (std::size_t j = 1; j < ALLSTYLES && f_styles[j] != 255; j++) {
			if (maxlights[j] > g_maxdiscardedlight + NORMAL_EPSILON) {
				ThreadLock();
				if (maxlights[j] > g_maxdiscardedlight + NORMAL_EPSILON) {
					g_maxdiscardedlight = maxlights[j];
					g_maxdiscardedpos = g_face_centroids[facenum];
				}
				ThreadUnlock();
			}
		}
		for (std::size_t j = 0; j < ALLSTYLES; j++) {
			free(fl_samples[j]);
		}
	}
	// patches
	for (patch = g_face_patches[facenum]; patch; patch = patch->next) {
		float maxlights[ALLSTYLES];
		for (std::size_t j = 0;
		     j < ALLSTYLES && (*patch->totalstyle_all)[j] != 255;
		     j++) {
			maxlights[j] = vector_max_element((*patch->totallight_all)[j]);
		}
		for (k = 0; k < MAXLIGHTMAPS; k++) {
			int bestindex = -1;
			if (k == 0) {
				bestindex = 0;
			} else {
				float bestmaxlight = 0;
				for (std::size_t j = 1;
				     j < ALLSTYLES && (*patch->totalstyle_all)[j] != 255;
				     j++) {
					if (maxlights[j] > bestmaxlight + NORMAL_EPSILON) {
						bestmaxlight = maxlights[j];
						bestindex = j;
					}
				}
			}
			if (bestindex != -1) {
				maxlights[bestindex] = 0;
				patch->totalstyle[k] = (*patch->totalstyle_all)[bestindex];
				patch->totallight[k] = (*patch->totallight_all)[bestindex];
			} else {
				patch->totalstyle[k] = 255;
			}
		}
		for (std::size_t j = 1;
		     j < ALLSTYLES && (*patch->totalstyle_all)[j] != 255;
		     j++) {
			if (maxlights[j] > g_maxdiscardedlight + NORMAL_EPSILON) {
				ThreadLock();
				if (maxlights[j] > g_maxdiscardedlight + NORMAL_EPSILON) {
					g_maxdiscardedlight = maxlights[j];
					g_maxdiscardedpos = patch->origin;
				}
				ThreadUnlock();
			}
		}
		for (std::size_t j = 0;
		     j < ALLSTYLES && (*patch->totalstyle_all)[j] != 255;
		     j++) {
			maxlights[j] = vector_max_element((*patch->directlight_all)[j]);
		}
		for (k = 0; k < MAXLIGHTMAPS; k++) {
			int bestindex = -1;
			if (k == 0) {
				bestindex = 0;
			} else {
				float bestmaxlight = 0;
				for (std::size_t j = 1;
				     j < ALLSTYLES && (*patch->totalstyle_all)[j] != 255;
				     j++) {
					if (maxlights[j] > bestmaxlight + NORMAL_EPSILON) {
						bestmaxlight = maxlights[j];
						bestindex = j;
					}
				}
			}
			if (bestindex != -1) {
				maxlights[bestindex] = 0;
				patch->directstyle[k] = (*patch->totalstyle_all)[bestindex];
				patch->directlight[k] = (*patch->directlight_all
				)[bestindex];
			} else {
				patch->directstyle[k] = 255;
			}
		}
		for (std::size_t j = 1;
		     j < ALLSTYLES && (*patch->totalstyle_all)[j] != 255;
		     j++) {
			if (maxlights[j] > g_maxdiscardedlight + NORMAL_EPSILON) {
				ThreadLock();
				if (maxlights[j] > g_maxdiscardedlight + NORMAL_EPSILON) {
					g_maxdiscardedlight = maxlights[j];
					g_maxdiscardedpos = patch->origin;
				}
				ThreadUnlock();
			}
		}
		free(patch->totalstyle_all);
		patch->totalstyle_all = nullptr;
		free(patch->samplelight_all);
		patch->samplelight_all = nullptr;
		free(patch->totallight_all);
		patch->totallight_all = nullptr;
		free(patch->directlight_all);
		patch->directlight_all = nullptr;
	}
	free(l.lmcache);
	free(l.lmcache_normal);
	free(l.lmcache_wallflags);
	free(l.surfpt_position);
	free(l.surfpt_surface);
}

// =====================================================================================
//  PrecompLightmapOffsets
// =====================================================================================
void PrecompLightmapOffsets() {
	int facenum;
	dface_t* f;
	facelight_t* fl;
	int lightstyles;

	patch_t* patch;

	std::size_t newLightDataSize = 0;

	for (facenum = 0; facenum < g_numfaces; facenum++) {
		f = &g_dfaces[facenum];
		fl = &facelight[facenum];

		if (g_texinfo[f->texinfo].has_special_flag()) {
			continue; // non-lit texture
		}

		{
			int i, j, k;
			std::array<float, ALLSTYLES> maxlights;
			{
				std::array<float3_array, ALLSTYLES> maxlights1{};
				std::array<float3_array, ALLSTYLES> maxlights2{};
				for (k = 0; k < MAXLIGHTMAPS && f->styles[k] != 255; k++) {
					for (i = 0; i < fl->numsamples; i++) {
						maxlights1[f->styles[k]] = vector_maximums(
							maxlights1[f->styles[k]],
							fl->samples[k][i].light
						);
					}
				}
				int numpatches;
				int const * patches;
				GetTriangulationPatches(
					facenum, &numpatches, &patches
				); // collect patches and their neighbors

				for (i = 0; i < numpatches; i++) {
					patch = &g_patches[patches[i]];
					for (k = 0;
					     k < MAXLIGHTMAPS && patch->totalstyle[k] != 255;
					     k++) {
						maxlights2[patch->totalstyle[k]] = vector_maximums(
							maxlights2[patch->totalstyle[k]],
							patch->totallight[k]
						);
					}
				}
				for (j = 0; j < ALLSTYLES; j++) {
					float3_array v = vector_add(
						maxlights1[j], maxlights2[j]
					);
					maxlights[j] = vector_max_element(v);
					if (maxlights[j] <= g_corings[j] * 0.01) {
						if (maxlights[j]
						    > g_maxdiscardedlight + NORMAL_EPSILON) {
							g_maxdiscardedlight = maxlights[j];
							g_maxdiscardedpos = g_face_centroids[facenum];
						}
						maxlights[j] = 0;
					}
				}
			}
			unsigned char oldstyles[MAXLIGHTMAPS];
			sample_t* oldsamples[MAXLIGHTMAPS];
			for (k = 0; k < MAXLIGHTMAPS; k++) {
				oldstyles[k] = f->styles[k];
				oldsamples[k] = fl->samples[k];
			}
			for (k = 0; k < MAXLIGHTMAPS; k++) {
				unsigned char beststyle = 255;
				if (k == 0) {
					beststyle = 0;
				} else {
					float bestmaxlight = 0;
					for (j = 1; j < ALLSTYLES; j++) {
						if (maxlights[j] > bestmaxlight + NORMAL_EPSILON) {
							bestmaxlight = maxlights[j];
							beststyle = j;
						}
					}
				}
				if (beststyle != 255) {
					maxlights[beststyle] = 0;
					f->styles[k] = beststyle;
					fl->samples[k] = (sample_t*) malloc(
						fl->numsamples * sizeof(sample_t)
					);
					hlassume(
						fl->samples[k] != nullptr, assume_msg::NoMemory
					);
					for (i = 0; i < MAXLIGHTMAPS && oldstyles[i] != 255;
					     i++) {
						if (oldstyles[i] == f->styles[k]) {
							break;
						}
					}
					if (i < MAXLIGHTMAPS && oldstyles[i] != 255) {
						memcpy(
							fl->samples[k],
							oldsamples[i],
							fl->numsamples * sizeof(sample_t)
						);
					} else {
						memcpy(
							fl->samples[k],
							oldsamples[0],
							fl->numsamples * sizeof(sample_t)
						); // copy 'sample.pos' from style 0 to the new
						   // style - because 'sample.pos' is actually
						   // the same for all styles! (why did we
						   // decide to store it in many places?)
						for (j = 0; j < fl->numsamples; j++) {
							fl->samples[k][j].light = {};
						}
					}
				} else {
					f->styles[k] = 255;
					fl->samples[k] = nullptr;
				}
			}
			for (j = 1; j < ALLSTYLES; j++) {
				if (maxlights[j] > g_maxdiscardedlight + NORMAL_EPSILON) {
					g_maxdiscardedlight = maxlights[j];
					g_maxdiscardedpos = g_face_centroids[facenum];
				}
			}
			for (k = 0; k < MAXLIGHTMAPS && oldstyles[k] != 255; k++) {
				free(oldsamples[k]);
			}
		}

		for (lightstyles = 0; lightstyles < MAXLIGHTMAPS; lightstyles++) {
			if (f->styles[lightstyles] == 255) {
				break;
			}
		}

		if (!lightstyles) {
			continue;
		}

		f->lightofs = newLightDataSize;
		newLightDataSize += fl->numsamples * 3 * lightstyles;
		hlassume(
			newLightDataSize <= g_max_map_lightdata,
			assume_msg::exceeded_MAX_MAP_LIGHTING
		); // lightdata
	}
	g_dlightdata.resize(newLightDataSize, std::byte(0));
}

void ReduceLightmap() {
	std::vector<std::byte> oldlightdata;
	using std::swap;
	swap(oldlightdata, g_dlightdata);

	int facenum;
	for (facenum = 0; facenum < g_numfaces; facenum++) {
		dface_t* f = &g_dfaces[facenum];
		facelight_t* fl = &facelight[facenum];
		if (g_texinfo[f->texinfo].has_special_flag()) {
			continue; // non-lit texture
		}
		// just need to zero the lightmap so that it won't contribute to
		// lightdata size
		if (IntForKey(g_face_entity[facenum], u8"zhlt_striprad")) {
			f->lightofs = g_dlightdata.size();
			for (int k = 0; k < MAXLIGHTMAPS; k++) {
				f->styles[k] = 255;
			}
			continue;
		}
		if (f->lightofs == -1) {
			continue;
		}

		int i, k;
		int oldofs;
		unsigned char oldstyles[MAXLIGHTMAPS];
		oldofs = f->lightofs;
		f->lightofs = g_dlightdata.size();
		for (k = 0; k < MAXLIGHTMAPS; k++) {
			oldstyles[k] = f->styles[k];
			f->styles[k] = 255;
		}
		int numstyles = 0;
		for (k = 0; k < MAXLIGHTMAPS && oldstyles[k] != 255; k++) {
			unsigned char maxb = 0;
			for (i = 0; i < fl->numsamples; i++) {
				std::byte* v
					= &oldlightdata
						  [oldofs + fl->numsamples * 3 * k + i * 3];
				maxb = std::max({ maxb,
				                  (std::uint8_t) v[0],
				                  (std::uint8_t) v[1],
				                  (std::uint8_t) v[2] });
			}
			if (maxb <= 0) // black
			{
				continue;
			}
			f->styles[numstyles] = oldstyles[k];
			hlassume(
				g_dlightdata.size() + fl->numsamples * 3 * (numstyles + 1)
					<= g_max_map_lightdata,
				assume_msg::exceeded_MAX_MAP_LIGHTING
			);

			std::span toAppend{
				&oldlightdata[oldofs + fl->numsamples * 3 * k],
				std::size_t(fl->numsamples) * 3zu
			};
			g_dlightdata.insert(
				g_dlightdata.end(), toAppend.begin(), toAppend.end()
			);
			numstyles++;
		}
	}
}

// Change the sample light right under a mdl file entity's origin.
// Use this when "mdl" in shadow has incorrect brightness.

constexpr std::int32_t MLH_MAXFACECOUNT = 16;
constexpr std::int32_t MLH_MAXSAMPLECOUNT = 4;
constexpr float MLH_LEFT = 0;
constexpr float MLH_RIGHT = 1;

struct mdllight_face_style_t final {
	bool exist;
	int seq;
};

struct mdllight_face_sample_t final {
	int num;
	float3_array pos;
	std::byte*(style[ALLSTYLES]);
};

struct mdllight_face_t final {
	int num;

	std::array<mdllight_face_style_t, ALLSTYLES> styles;

	std::array<mdllight_face_sample_t, MLH_MAXSAMPLECOUNT> samples;

	int samplecount;
};

struct mdllight_t final {
	float3_array origin;
	float3_array floor;

	std::array<mdllight_face_t, MLH_MAXFACECOUNT> faces;

	int facecount;
};

int MLH_AddFace(mdllight_t* ml, int facenum) {
	dface_t* f = &g_dfaces[facenum];
	int i, j;
	for (i = 0; i < ml->facecount; i++) {
		if (ml->faces[i].num == facenum) {
			return -1;
		}
	}
	if (ml->facecount >= MLH_MAXFACECOUNT) {
		return -1;
	}
	i = ml->facecount;
	ml->facecount++;
	ml->faces[i].num = facenum;
	ml->faces[i].samplecount = 0;
	for (j = 0; j < ALLSTYLES; j++) {
		ml->faces[i].styles[j].exist = false;
	}
	for (j = 0; j < MAXLIGHTMAPS && f->styles[j] != 255; j++) {
		ml->faces[i].styles[f->styles[j]].exist = true;
		ml->faces[i].styles[f->styles[j]].seq = j;
	}
	return i;
}

void MLH_AddSample(
	mdllight_t* ml,
	int facenum,
	int w,
	int h,
	int s,
	int t,
	float3_array const & pos
) {
	dface_t* f = &g_dfaces[facenum];
	int i, j;
	int r = MLH_AddFace(ml, facenum);
	if (r == -1) {
		return;
	}
	int size = w * h;
	int num = s + w * t;
	for (i = 0; i < ml->faces[r].samplecount; i++) {
		if (ml->faces[r].samples[i].num == num) {
			return;
		}
	}
	if (ml->faces[r].samplecount >= MLH_MAXSAMPLECOUNT) {
		return;
	}
	i = ml->faces[r].samplecount;
	ml->faces[r].samplecount++;
	ml->faces[r].samples[i].num = num;
	ml->faces[r].samples[i].pos = pos;
	for (j = 0; j < ALLSTYLES; j++) {
		if (ml->faces[r].styles[j].exist) {
			ml->faces[r].samples[i].style[j]
				= &g_dlightdata
					  [f->lightofs
			           + (num + size * ml->faces[r].styles[j].seq) * 3];
		}
	}
}

void MLH_CalcExtents(dface_t const * f, int* texturemins, int* extents) {
	face_extents const bExtents{ get_face_extents(f - g_dfaces.data()) };

	for (std::size_t i = 0; i < 2; ++i) {
		texturemins[i] = bExtents.mins[i] * TEXTURE_STEP;
		extents[i] = (bExtents.maxs[i] - bExtents.mins[i]) * TEXTURE_STEP;
	}
}

void MLH_GetSamples_r(
	mdllight_t* ml,
	int nodenum,
	float3_array const & start,
	float3_array const & end
) {
	if (nodenum < 0) {
		return;
	}
	dnode_t* node = &g_dnodes[nodenum];
	dplane_t* plane;
	float front, back, frac;
	int side;
	plane = &g_dplanes[node->planenum];
	front = dot_product(start, plane->normal) - plane->dist;
	back = dot_product(end, plane->normal) - plane->dist;
	side = front < 0;
	if ((back < 0) == side) {
		MLH_GetSamples_r(ml, node->children[side], start, end);
		return;
	}
	frac = front / (front - back);
	float3_array const mid{ start[0] + (end[0] - start[0]) * frac,
		                    start[1] + (end[1] - start[1]) * frac,
		                    start[2] + (end[2] - start[2]) * frac };
	MLH_GetSamples_r(ml, node->children[side], start, mid);
	if (ml->facecount > 0) {
		return;
	}
	{
		int i;
		for (i = 0; i < node->numfaces; i++) {
			dface_t* f = &g_dfaces[node->firstface + i];
			texinfo_t* tex = &g_texinfo[f->texinfo];
			if (get_texture_by_number(f->texinfo).is_ordinary_sky()) {
				continue;
			}
			if (f->lightofs == -1) {
				continue;
			}
			int s = (int) (dot_product(mid, tex->vecs[0].xyz)
			               + tex->vecs[0].offset);
			int t = (int) (dot_product(mid, tex->vecs[1].xyz)
			               + tex->vecs[1].offset);
			int texturemins[2], extents[2];
			MLH_CalcExtents(f, texturemins, extents);
			if (s < texturemins[0] || t < texturemins[1]) {
				continue;
			}
			int ds = s - texturemins[0];
			int dt = t - texturemins[1];
			if (ds > extents[0] || dt > extents[1]) {
				continue;
			}
			ds >>= 4;
			dt >>= 4;
			MLH_AddSample(
				ml,
				node->firstface + i,
				extents[0] / TEXTURE_STEP + 1,
				extents[1] / TEXTURE_STEP + 1,
				ds,
				dt,
				mid
			);
			break;
		}
	}
	if (ml->facecount > 0) {
		ml->floor = mid;
		return;
	}
	MLH_GetSamples_r(ml, node->children[!side], mid, end);
}

void MLH_mdllightCreate(mdllight_t* ml) {
	// code from Quake
	ml->facecount = 0;
	ml->floor = ml->origin;
	float3_array const p = ml->origin;
	float3_array end = ml->origin;
	end[2] -= 2048;
	MLH_GetSamples_r(ml, 0, p, end);
}

static int
MLH_CopyLight(float3_array const & from, float3_array const & to) {
	int i, j, k, count = 0;
	mdllight_t mlfrom, mlto;
	mlfrom.origin = from;
	mlto.origin = to;
	MLH_mdllightCreate(&mlfrom);
	MLH_mdllightCreate(&mlto);
	if (mlfrom.facecount == 0 || mlfrom.faces[0].samplecount == 0) {
		return -1;
	}
	for (i = 0; i < mlto.facecount; ++i) {
		for (j = 0; j < mlto.faces[i].samplecount; ++j, ++count) {
			for (k = 0; k < ALLSTYLES; ++k) {
				if (mlto.faces[i].styles[k].exist
				    && mlfrom.faces[0].styles[k].exist) {
					std::copy_n(
						mlfrom.faces[0].samples[0].style[k],
						3,
						mlto.faces[i].samples[j].style[k]
					);
					Developer(
						developer_level::spam,
						"Mdl Light Hack: face (%d) sample (%d) style (%d) position (%f,%f,%f)\n",
						mlto.faces[i].num,
						mlto.faces[i].samples[j].num,
						k,
						mlto.faces[i].samples[j].pos[0],
						mlto.faces[i].samples[j].pos[1],
						mlto.faces[i].samples[j].pos[2]
					);
				}
			}
		}
	}
	Developer(
		developer_level::message,
		"Mdl Light Hack: %d sample light copied from (%f,%f,%f) to (%f,%f,%f)\n",
		count,
		mlfrom.floor[0],
		mlfrom.floor[1],
		mlfrom.floor[2],
		mlto.floor[0],
		mlto.floor[1],
		mlto.floor[2]
	);
	return count;
}

void MdlLightHack() {
	int ient;
	float3_array origin1, origin2;
	int used = 0, countent = 0, countsample = 0, r;
	for (ient = 0; ient < g_numentities; ++ient) {
		entity_t const & ent1 = g_entities[ient];
		std::u8string_view target = value_for_key(
			&ent1, u8"zhlt_copylight"
		);
		if (target.empty()) {
			continue;
		}
		used = 1;
		auto const maybeEnt2 = find_target_entity(target);
		if (!maybeEnt2) {
			Warning(
				"target entity '%s' not found", (char const *) target.data()
			);
			continue;
		}
		entity_t const & ent2{ maybeEnt2.value().get() };
		origin1 = get_float3_for_key(ent1, u8"origin");
		origin2 = get_float3_for_key(ent2, u8"origin");
		r = MLH_CopyLight(origin2, origin1);
		if (r < 0) {
			Warning(
				"can not copy light from (%f,%f,%f)",
				origin2[0],
				origin2[1],
				origin2[2]
			);
		} else {
			countent += 1;
			countsample += r;
		}
	}
	if (used) {
		Log("Adjust mdl light: modified %d samples for %d entities\n",
		    countsample,
		    countent);
	}
}

struct facelightlist_t final {
	int facenum;
	facelightlist_t* next;
};

static facelightlist_t* g_dependentfacelights[MAX_MAP_FACES];

// =====================================================================================
//  CreateFacelightDependencyList
// =====================================================================================
void CreateFacelightDependencyList() {
	int facenum;
	dface_t* f;
	facelight_t* fl;
	int i;
	int k;
	int surface;
	facelightlist_t* item;

	for (i = 0; i < MAX_MAP_FACES; i++) {
		g_dependentfacelights[i] = nullptr;
	}

	// for each face
	for (facenum = 0; facenum < g_numfaces; facenum++) {
		f = &g_dfaces[facenum];
		fl = &facelight[facenum];
		if (g_texinfo[f->texinfo].has_special_flag()) {
			continue;
		}

		for (k = 0; k < MAXLIGHTMAPS && f->styles[k] != 255; k++) {
			for (i = 0; i < fl->numsamples; i++) {
				surface = fl->samples[k][i]
							  .surface; // that surface contains at least
				                        // one sample from this face
				if (0 <= surface && surface < g_numfaces) {
					// insert this face into the dependency list of that
					// surface
					for (item = g_dependentfacelights[surface];
					     item != nullptr;
					     item = item->next) {
						if (item->facenum == facenum) {
							break;
						}
					}
					if (item) {
						continue;
					}

					item = (facelightlist_t*) malloc(sizeof(facelightlist_t)
					);
					hlassume(item != nullptr, assume_msg::NoMemory);
					item->facenum = facenum;
					item->next = g_dependentfacelights[surface];
					g_dependentfacelights[surface] = item;
				}
			}
		}
	}
}

// =====================================================================================
//  FreeFacelightDependencyList
// =====================================================================================
void FreeFacelightDependencyList() {
	int i;
	facelightlist_t* item;

	for (i = 0; i < MAX_MAP_FACES; i++) {
		while (g_dependentfacelights[i]) {
			item = g_dependentfacelights[i];
			g_dependentfacelights[i] = item->next;
			free(item);
		}
	}
}

// =====================================================================================
//  AddPatchLights
//    This function is run multithreaded
// =====================================================================================
void AddPatchLights(int facenum) {
	dface_t* f;
	facelightlist_t* item;
	dface_t* f_other;
	facelight_t* fl_other;
	int k;
	int i;
	sample_t* samp;

	f = &g_dfaces[facenum];

	if (g_texinfo[f->texinfo].has_special_flag()) {
		return;
	}

	for (item = g_dependentfacelights[facenum]; item != nullptr;
	     item = item->next) {
		f_other = &g_dfaces[item->facenum];
		fl_other = &facelight[item->facenum];
		for (k = 0; k < MAXLIGHTMAPS && f_other->styles[k] != 255; k++) {
			for (i = 0; i < fl_other->numsamples; i++) {
				samp = &fl_other->samples[k][i];
				if (samp->surface
				    != facenum) { // the sample is not in this surface
					continue;
				}

				{
					float3_array v;

					int style = f_other->styles[k];
					InterpolateSampleLight(
						samp->pos, samp->surface, style, v
					);

					v = vector_add(samp->light, v);
					if (vector_max_element(v)
					    >= g_corings[f_other->styles[k]]) {
						samp->light = v;
					} else if (vector_max_element(v)
					           > g_maxdiscardedlight + NORMAL_EPSILON) {
						ThreadLock();
						if (vector_max_element(v)
						    > g_maxdiscardedlight + NORMAL_EPSILON) {
							g_maxdiscardedlight = vector_max_element(v);
							g_maxdiscardedpos = samp->pos;
						}
						ThreadUnlock();
					}
				}
			} // loop samples
		}
	}
}

// =====================================================================================
//  FinalLightFace
//      Add the indirect lighting on top of the direct lighting and save
//      into final map format
// =====================================================================================
void FinalLightFace(int const facenum) {
	if (facenum == 0 && g_drawsample) {
		std::filesystem::path const sampleFilePath{
			path_to_temp_file_with_extension(g_Mapname, u8"_sample.pts")
				.c_str()
		};
		Log("Writing '%s' ...\n", sampleFilePath.c_str());
		FILE* f;
		f = fopen(sampleFilePath.c_str(), "w");
		if (f) {
			int i, j;
			for (i = 0; i < g_numfaces; ++i) {
				facelight_t const * fl = &facelight[i];
				for (j = 0; j < fl->numsamples; ++j) {
					float3_array v = fl->samples[0][j].pos;
					float3_array dist = vector_subtract(
						v, g_drawsample_origin
					);
					if (dot_product(dist, dist)
					    < g_drawsample_radius * g_drawsample_radius) {
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
			}
			fclose(f);
			Log("OK.\n");
		} else {
			Log("Error.\n");
		}
	}
	sample_t* samp;
	dface_t* f;

	float temp_rand;

	f = &g_dfaces[facenum];
	facelight_t* fl = &facelight[facenum];

	if (g_texinfo[f->texinfo].has_special_flag()) {
		return; // non-lit texture
	}

	int lightstyles;
	for (lightstyles = 0; lightstyles < MAXLIGHTMAPS; lightstyles++) {
		if (f->styles[lightstyles] == 255) {
			break;
		}
	}

	float_color_element minLight = std::max(
		(float) g_minlight,
		float_for_key(*g_face_entity[facenum], u8"_minlight")
	);

	wad_texture_name const texname{ get_texture_by_number(f->texinfo) };
	minLight = std::max(minLight, texname.get_minlight().value_or(0.0f));

	for (minlight_i it = s_minlights.begin(); it != s_minlights.end();
	     ++it) {
		if (texname == it->name) {
			minLight = std::max(minLight, it->value);
			break;
		}
	}

	minLight = std::clamp(minLight, 0.0f, 1.0f);

	for (int k = 0; k < lightstyles; k++) {
		for (int j = 0; j < fl->numsamples; j++) {
			samp = fl->samples[k] + j;

			if (f->styles[0] != 0) {
				Warning("wrong f->styles[0]");
			}
			float3_array lb = vector_maximums(samp->light, float3_array{});

			lb = vector_scale(lb, g_lighting_scale);

			lb[0] = std::pow(lb[0] / 255.0f, g_lighting_gamma);
			lb[1] = std::pow(lb[1] / 255.0f, g_lighting_gamma);
			lb[2] = std::pow(lb[2] / 255.0f, g_lighting_gamma);

			// Clamp values
			{
				float max = vector_max_element(lb);
				float const maxLight = g_limitthreshold / 255.0f;
				bool const isOverbright = max > maxLight;
				if (isOverbright) {
					// Make it just fullbright - since HL no longer supports
					// overbright lighting (gl_overbright)
					lb = vector_scale(lb, maxLight / max);
				} else if (g_drawoverload) {
					// Darken points that are not fullbright
					lb = vector_scale(lb, 0.1f);
				} else if (max < minLight) {
					if (max > 0) {
						lb = vector_scale(lb, minLight / max);
					} else {
						lb = { minLight, minLight, minLight };
					}
				}
			}

			std::byte* colors
				= &g_dlightdata
					  [f->lightofs + k * fl->numsamples * 3 + j * 3];

			int8_rgb lbi;
			for (int i = 0; i < 3; ++i) {
				lbi[i] = (std::uint8_t
				) std::lround(std::clamp(lb[i], 0.0f, 1.0f) * 255);
			}
			colors[0] = (std::byte) lbi[0];
			colors[1] = (std::byte) lbi[1];
			colors[2] = (std::byte) lbi[2];
		}
	}
}

static float3_array totallight_default = { 0, 0, 0 };

// Get the right totalLight value from a patch
float3_array get_total_light(patch_t const & patch, int style) noexcept {
	for (std::size_t i = 0; i < MAXLIGHTMAPS && patch.totalstyle[i] != 255;
	     ++i) {
		if (patch.totalstyle[i] == style) {
			return patch.totallight[i];
		}
	}
	return totallight_default;
}
