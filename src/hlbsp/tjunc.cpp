#include "hlbsp.h"
#include "log.h"

#include <cstring>
#include <optional>
#include <vector>

using wvert_count = std::uint32_t;
using wedge_count = std::uint32_t;

struct wvert_t final {
	double t;
	wvert_count prevWvert;
	wvert_count nextWvert;
};

struct wedge_t final {
	std::optional<wedge_count> nextWedge;
	double3_array dir;
	double3_array origin;
	wvert_count headWvert;
};

static std::size_t tjuncs;
static std::size_t tjuncfaces;

static std::vector<wvert_t> wverts;
static std::vector<wedge_t> wedges;

//============================================================================

constexpr std::size_t NUM_HASH = 4096;

// The elements are indicies of the wedges array
std::array<std::optional<wedge_count>, NUM_HASH> wedge_hash;

constexpr double hash_min{ -8000 };
static double3_array hash_scale;
// It's okay if the coordinates go under hash_min, because they are hashed
// in a cyclic way (modulus by hash_numslots) So please don't change the
// hardcoded hash_min and scale
static int hash_numslots[3];
constexpr std::size_t MAX_HASH_NEIGHBORS = 4;

static void
InitHash(double3_array const & mins, double3_array const & maxs) {
	constexpr double size{ 16000.0 };

	wedge_hash = {};

	double const volume = size * size;

	double const scale = std::sqrt(volume / NUM_HASH);

	hash_numslots[0] = (int) floor(size / scale);
	hash_numslots[1] = (int) floor(size / scale);
	while (hash_numslots[0] * hash_numslots[1] > NUM_HASH) {
		Developer(
			developer_level::warning,
			"hash_numslots[0] * hash_numslots[1] > NUM_HASH"
		);
		hash_numslots[0]--;
		hash_numslots[1]--;
	}

	hash_scale[0] = hash_numslots[0] / size;
	hash_scale[1] = hash_numslots[1] / size;
}

static int HashVec(
	double3_array const & vec,
	vector_inplace<int, MAX_HASH_NEIGHBORS>& hashneighbors
) {
	hashneighbors.clear();

	std::array<int, 2> slot;
	std::array<double, 2> normalized;
	std::array<double, 2> slotdiff;

	for (int i = 0; i < 2; ++i) {
		normalized[i] = hash_scale[i] * (vec[i] - hash_min);
		slot[i] = (int) floor(normalized[i]);
		slotdiff[i] = normalized[i] - (double) slot[i];

		slot[i] = (slot[i] + hash_numslots[i]) % hash_numslots[i];
		slot[i] = (slot[i] + hash_numslots[i])
			% hash_numslots[i]; // do it twice to handle negative values
	}

	int const h = slot[0] * hash_numslots[1] + slot[1];

	for (int x = -1; x <= 1; ++x) {
		if ((x == -1 && slotdiff[0] > hash_scale[0] * (2 * ON_EPSILON))
		    || (x == 1 && slotdiff[0] < 1 - hash_scale[0] * (2 * ON_EPSILON)
		    )) {
			continue;
		}
		for (int y = -1; y <= 1; ++y) {
			if ((y == -1 && slotdiff[1] > hash_scale[1] * (2 * ON_EPSILON))
			    || (y == 1
			        && slotdiff[1] < 1 - hash_scale[1] * (2 * ON_EPSILON)
			    )) {
				continue;
			}
			hashneighbors.emplace_back(
				((slot[0] + x + hash_numslots[0]) % hash_numslots[0])
					* hash_numslots[1]
				+ (slot[1] + y + hash_numslots[1]) % hash_numslots[1]
			);
		}
	}

	return h;
}

//============================================================================

static bool CanonicalVector(double3_array& vec) {
	if (normalize_vector(vec)) {
		if (vec[0] > NORMAL_EPSILON) {
			return true;
		} else if (vec[0] < -NORMAL_EPSILON) {
			vec = negate_vector(vec);
			return true;
		} else {
			vec[0] = 0;
		}

		if (vec[1] > NORMAL_EPSILON) {
			return true;
		} else if (vec[1] < -NORMAL_EPSILON) {
			vec = negate_vector(vec);
			return true;
		} else {
			vec[1] = 0;
		}

		if (vec[2] > NORMAL_EPSILON) {
			return true;
		} else if (vec[2] < -NORMAL_EPSILON) {
			vec = negate_vector(vec);
			return true;
		} else {
			vec[2] = 0;
		}
		return false;
	}
	return false;
}

static wedge_t* FindEdge(
	double3_array const & p1,
	double3_array const & p2,
	double* t1,
	double* t2
) {
	double3_array dir = vector_subtract(p2, p1);
	if (!CanonicalVector(dir)) {
#if _DEBUG
		Warning(
			"CanonicalVector: degenerate @ (%4.3f %4.3f %4.3f )\n",
			p1[0],
			p1[1],
			p1[2]
		);
#endif
	}

	*t1 = dot_product(p1, dir);
	*t2 = dot_product(p2, dir);

	double3_array const origin = vector_fma(dir, -*t1, p1);

	if (*t1 > *t2) {
		using std::swap;
		swap(*t1, *t2);
	}

	vector_inplace<int, MAX_HASH_NEIGHBORS> hashneighbors;

	int h = HashVec(origin, hashneighbors);

	for (int neighbour : hashneighbors) {
		for (std::optional<wedge_count> maybeWedgeIndex
		     = wedge_hash[neighbour];
		     maybeWedgeIndex.has_value();
		     maybeWedgeIndex = wedges[maybeWedgeIndex.value()].nextWedge) {
			wedge_t& wedge = wedges[maybeWedgeIndex.value()];

			if (fabs(wedge.origin[0] - origin[0]) > EQUAL_EPSILON
			    || fabs(wedge.origin[1] - origin[1]) > EQUAL_EPSILON
			    || fabs(wedge.origin[2] - origin[2]) > EQUAL_EPSILON) {
				continue;
			}
			if (fabs(wedge.dir[0] - dir[0]) > NORMAL_EPSILON
			    || fabs(wedge.dir[1] - dir[1]) > NORMAL_EPSILON
			    || fabs(wedge.dir[2] - dir[2]) > NORMAL_EPSILON) {
				continue;
			}

			return &wedge;
		}
	}

	wedge_count const wedgeIndex = wedges.size();
	wedge_t& wedge = wedges.emplace_back();

	wedge.nextWedge = wedge_hash[h];
	wedge_hash[h] = wedgeIndex;

	wedge.origin = origin;
	wedge.dir = dir;

	wvert_count const headWvertIndex = wverts.size();
	wvert_t& head = wverts.emplace_back();
	head.nextWvert = head.prevWvert = headWvertIndex;
	head.t = 99999;

	wedge.headWvert = headWvertIndex;
	return &wedge;
}

static void AddVert(wedge_t const * const w, double const t) {
	wvert_count wvertIndex = wverts[w->headWvert].nextWvert;
	while (true) {
		wvert_t const & wvert = wverts[wvertIndex];
		if (fabs(wvert.t - t) < ON_EPSILON) {
			return;
		}
		if (wvert.t > t) {
			break;
		}
		wvertIndex = wvert.nextWvert;
	};

	// Insert a new wvert_t before v

	wvert_count newvIndex = wverts.size();
	wvert_t& newv = wverts.emplace_back();
	wvert_t& v = wverts[wvertIndex];

	newv.t = t;
	newv.nextWvert = wvertIndex;
	newv.prevWvert = v.prevWvert;
	wverts[v.prevWvert].nextWvert = newvIndex;
	v.prevWvert = newvIndex;
}

/*
 * ===============
 * AddEdge
 * ===============
 */
static void AddEdge(double3_array const & p1, double3_array const & p2) {
	double t1;
	double t2;

	wedge_t* w = FindEdge(p1, p2, &t1, &t2);
	AddVert(w, t1);
	AddVert(w, t2);
}

/*
 * ===============
 * AddFaceEdges
 *
 * ===============
 */
static void AddFaceEdges(face_t const * const f) {
	for (int i = 0; i < f->pts.size(); i++) {
		int j = (i + 1) % f->pts.size();
		AddEdge(f->pts[i], f->pts[j]);
	}
}

//============================================================================

static byte superfacebuf[1024 * 16];
static face_t* superface = (face_t*) superfacebuf;
static std::size_t MAX_SUPERFACEEDGES = (sizeof(superfacebuf)
                                         - sizeof(face_t)
                                         + sizeof(superface->pts))
	/ sizeof(double3_array);
static face_t* newlist;

//// TODO: IS THIS SPLITTING NECESSARY??????
static void SplitFaceForTjunc(face_t* f, face_t* original) {
	face_t* chain{ nullptr };
	while (true) {
		hlassume(
			f->original == nullptr, assume_msg::ValidPointer
		); // "SplitFaceForTjunc: f->original"

		if (f->pts.size() <= MAXPOINTS) { // the face is now small enough
			                              // without more cutting
			// so copy it back to the original
			*original = *f;
			original->original = chain;
			original->next = newlist;
			newlist = original;
			return;
		}

		++tjuncfaces;

	restart:
		// find the last corner
		double3_array dir = vector_subtract(f->pts.back(), f->pts.front());
		normalize_vector(dir);
		int lastcorner;
		for (lastcorner = f->pts.size() - 1; lastcorner > 0; lastcorner--) {
			double3_array test = vector_subtract(
				f->pts[lastcorner - 1], f->pts[lastcorner]
			);
			normalize_vector(test);
			double const v = dot_product(test, dir);
			if (v < 1.0 - ON_EPSILON || v > 1.0 + ON_EPSILON) {
				break;
			}
		}

		// find the first corner
		dir = vector_subtract(f->pts[1], f->pts[0]);
		normalize_vector(dir);
		int firstcorner;
		for (firstcorner = 1; firstcorner < f->pts.size() - 1;
		     firstcorner++) {
			double3_array test = vector_subtract(
				f->pts[firstcorner + 1], f->pts[firstcorner]
			);
			normalize_vector(test);
			double const v = dot_product(test, dir);
			if (v < 1.0 - ON_EPSILON || v > 1.0 + ON_EPSILON) {
				break;
			}
		}

		if (firstcorner + 2 >= MAXPOINTS) {
			// rotate the point winding
			double3_array test = f->pts[0];
			for (int i = 1; i < f->pts.size(); i++) {
				f->pts[i - 1] = f->pts[i];
			}
			f->pts.back() = test;
			goto restart;
		}

		// cut off as big a piece as possible, less than MAXPOINTS, and not
		// past lastcorner

		face_t* const newface = new face_t{ NewFaceFromFace(*f) };

		hlassume(
			f->original == nullptr, assume_msg::ValidPointer
		); // "SplitFaceForTjunc: f->original"

		newface->original = chain;
		chain = newface;
		newface->next = newlist;
		newlist = newface;
		std::size_t pointsToMoveToNewFace;
		if (f->pts.size() - firstcorner <= MAXPOINTS) {
			pointsToMoveToNewFace = firstcorner + 2;
		} else if (lastcorner + 2 < MAXPOINTS
		           && f->pts.size() - lastcorner <= MAXPOINTS) {
			pointsToMoveToNewFace = lastcorner + 2;
		} else {
			pointsToMoveToNewFace = MAXPOINTS;
		}

		newface->pts.assign_range(std::span{ f->pts.begin(),
		                                     pointsToMoveToNewFace });

		f->pts.erase(
			f->pts.begin() + 1,

			f->pts.begin() + 1 + pointsToMoveToNewFace - 2
		);
	};
}

/*
 * ===============
 * FixFaceEdges
 *
 * ===============
 */
static void FixFaceEdges(face_t* f) {
	*superface = *f;

restart:
	for (int i = 0; i < superface->pts.size(); i++) {
		int j = (i + 1) % superface->pts.size();

		double t1;
		double t2;
		wedge_t const * const w = FindEdge(
			superface->pts[i], superface->pts[j], &t1, &t2
		);

		wvert_count vIndex;
		for (vIndex = wverts[w->headWvert].nextWvert;
		     wverts[vIndex].t < t1 + ON_EPSILON;
		     vIndex = wverts[vIndex].nextWvert)
			;

		wvert_t const & v = wverts[vIndex];

		if (v.t < t2 - ON_EPSILON) {
			++tjuncs;
			// insert a new vertex here
			superface->pts.emplace(
				superface->pts.begin() + j,
				vector_fma(w->dir, v.t, w->origin)
			);
			hlassume(
				superface->pts.size() < MAX_SUPERFACEEDGES,
				assume_msg::MAX_SUPERFACEEDGES
			);
			goto restart;
		}
	}

	if (superface->pts.size() <= MAXPOINTS) {
		*f = *superface;
		f->next = newlist;
		newlist = f;
		return;
	}

	// the face needs to be split into multiple faces because of too many
	// edges

	SplitFaceForTjunc(superface, f);
}

//============================================================================

static void tjunc_find_r(node_t* node) {
	if (node->planenum == PLANENUM_LEAF) {
		return;
	}

	for (face_t* f = node->faces; f; f = f->next) {
		AddFaceEdges(f);
	}

	tjunc_find_r(node->children[0]);
	tjunc_find_r(node->children[1]);
}

static void tjunc_fix_r(node_t* node) {
	if (node->planenum == PLANENUM_LEAF) {
		return;
	}

	newlist = nullptr;
	face_t* next;
	for (face_t* f = node->faces; f; f = next) {
		next = f->next;
		FixFaceEdges(f);
	}

	node->faces = newlist;

	tjunc_fix_r(node->children[0]);
	tjunc_fix_r(node->children[1]);
}

/*
 * ===========
 * tjunc
 *
 * ===========
 */
void tjunc(node_t* headnode) {
	Verbose("---- tjunc ----\n");

	if (g_notjunc) {
		return;
	}

	double3_array maxs;

	//
	// identify all points on common edges
	//

	// origin points won't allways be inside the map, so extend the hash
	// area
	for (int i = 0; i < 3; i++) {
		if (fabs(headnode->maxs[i]) > fabs(headnode->mins[i])) {
			maxs[i] = fabs(headnode->maxs[i]);
		} else {
			maxs[i] = fabs(headnode->mins[i]);
		}
	}
	double3_array mins = negate_vector(maxs);

	InitHash(mins, maxs);

	wedges.clear();
	wverts.clear();

	tjunc_find_r(headnode);

	Verbose(
		"%zu world edges %zu edge points\n", wverts.size(), wverts.size()
	);

	//
	// add extra vertexes on edges where needed
	//
	tjuncs = tjuncfaces = 0;

	tjunc_fix_r(headnode);

	Verbose("%zu edges added by tjunctions\n", tjuncs);
	Verbose("%zu faces added by tjunctions\n", tjuncfaces);
}
