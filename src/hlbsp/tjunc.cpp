#include "bsp5.h"

#include <cstring>

struct wvert_t final {
	double t;
	wvert_t* prev;
	wvert_t* next;
};

struct wedge_t final {
	wedge_t* next;
	double3_array dir;
	double3_array origin;
	wvert_t head;
};

static int numwedges;
static int numwverts;
static int tjuncs;
static int tjuncfaces;

#define MAX_WVERTS 0x4'0000
#define MAX_WEDGES 0x2'0000

static wvert_t wverts[MAX_WVERTS];
static wedge_t wedges[MAX_WEDGES];

//============================================================================

#define NUM_HASH 4096

std::array<wedge_t*, NUM_HASH> wedge_hash;

constexpr double hash_min{ -8000 };
static double3_array hash_scale;
// It's okay if the coordinates go under hash_min, because they are hashed
// in a cyclic way (modulus by hash_numslots) So please don't change the
// hardcoded hash_min and scale
static int hash_numslots[3];
#define MAX_HASH_NEIGHBORS 4

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
	double3_array const & vec, int* num_hashneighbors, int* hashneighbors
) {
	int h;
	int i;
	int x;
	int y;
	int slot[2];
	double normalized[2];
	double slotdiff[2];

	for (i = 0; i < 2; i++) {
		normalized[i] = hash_scale[i] * (vec[i] - hash_min);
		slot[i] = (int) floor(normalized[i]);
		slotdiff[i] = normalized[i] - (double) slot[i];

		slot[i] = (slot[i] + hash_numslots[i]) % hash_numslots[i];
		slot[i] = (slot[i] + hash_numslots[i])
			% hash_numslots[i]; // do it twice to handle negative values
	}

	h = slot[0] * hash_numslots[1] + slot[1];

	*num_hashneighbors = 0;
	for (x = -1; x <= 1; x++) {
		if ((x == -1 && slotdiff[0] > hash_scale[0] * (2 * ON_EPSILON))
			|| (x == 1 && slotdiff[0] < 1 - hash_scale[0] * (2 * ON_EPSILON)
			)) {
			continue;
		}
		for (y = -1; y <= 1; y++) {
			if ((y == -1 && slotdiff[1] > hash_scale[1] * (2 * ON_EPSILON))
				|| (y == 1
					&& slotdiff[1] < 1 - hash_scale[1] * (2 * ON_EPSILON)
				)) {
				continue;
			}
			if (*num_hashneighbors >= MAX_HASH_NEIGHBORS) {
				Error("HashVec: internal error.");
			}
			hashneighbors[*num_hashneighbors]
				= ((slot[0] + x + hash_numslots[0]) % hash_numslots[0])
					* hash_numslots[1]
				+ (slot[1] + y + hash_numslots[1]) % hash_numslots[1];
			(*num_hashneighbors)++;
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
		//        hlassert(false);
		return false;
	}
	//    hlassert(false);
	return false;
}

static wedge_t* FindEdge(
	double3_array const & p1,
	double3_array const & p2,
	double* t1,
	double* t2
) {
	double3_array origin;
	double3_array dir;
	wedge_t* w;
	double temp;
	int h;
	int num_hashneighbors;
	int hashneighbors[MAX_HASH_NEIGHBORS];

	dir = vector_subtract(p2, p1);
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

	origin = vector_fma(dir, -*t1, p1);

	if (*t1 > *t2) {
		temp = *t1;
		*t1 = *t2;
		*t2 = temp;
	}

	h = HashVec(origin, &num_hashneighbors, hashneighbors);

	for (int i = 0; i < num_hashneighbors; ++i) {
		for (w = wedge_hash[hashneighbors[i]]; w; w = w->next) {
			if (fabs(w->origin[0] - origin[0]) > EQUAL_EPSILON
				|| fabs(w->origin[1] - origin[1]) > EQUAL_EPSILON
				|| fabs(w->origin[2] - origin[2]) > EQUAL_EPSILON) {
				continue;
			}
			if (fabs(w->dir[0] - dir[0]) > NORMAL_EPSILON
				|| fabs(w->dir[1] - dir[1]) > NORMAL_EPSILON
				|| fabs(w->dir[2] - dir[2]) > NORMAL_EPSILON) {
				continue;
			}

			return w;
		}
	}

	hlassume(numwedges < MAX_WEDGES, assume_MAX_WEDGES);
	w = &wedges[numwedges];
	numwedges++;

	w->next = wedge_hash[h];
	wedge_hash[h] = w;

	w->origin = origin;
	w->dir = dir;
	w->head.next = w->head.prev = &w->head;
	w->head.t = 99999;
	return w;
}

/*
 * ===============
 * AddVert
 *
 * ===============
 */

static void AddVert(wedge_t const * const w, double const t) {
	wvert_t* v;
	wvert_t* newv;

	v = w->head.next;
	do {
		if (fabs(v->t - t) < ON_EPSILON) {
			return;
		}
		if (v->t > t) {
			break;
		}
		v = v->next;
	} while (1);

	// insert a new wvert before v
	hlassume(numwverts < MAX_WVERTS, assume_MAX_WVERTS);

	newv = &wverts[numwverts];
	numwverts++;

	newv->t = t;
	newv->next = v;
	newv->prev = v->prev;
	v->prev->next = newv;
	v->prev = newv;
}

/*
 * ===============
 * AddEdge
 * ===============
 */
static void AddEdge(double3_array const & p1, double3_array const & p2) {
	wedge_t* w;
	double t1;
	double t2;

	w = FindEdge(p1, p2, &t1, &t2);
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
	int i, j;

	for (i = 0; i < f->numpoints; i++) {
		j = (i + 1) % f->numpoints;
		AddEdge(f->pts[i], f->pts[j]);
	}
}

//============================================================================

static byte superfacebuf[1024 * 16];
static face_t* superface = (face_t*) superfacebuf;
static int MAX_SUPERFACEEDGES = (sizeof(superfacebuf) - sizeof(face_t)
								 + sizeof(superface->pts))
	/ sizeof(double3_array);
static face_t* newlist;

static void SplitFaceForTjunc(face_t* f, face_t* original) {
	int i;
	face_t* newface;
	face_t* chain;
	double3_array dir, test;
	double v;
	int firstcorner, lastcorner;

#ifdef _DEBUG
	static int counter = 0;

	Log("SplitFaceForTjunc %d\n", counter++);
#endif

	chain = nullptr;
	do {
		hlassume(
			f->original == nullptr, assume_ValidPointer
		); // "SplitFaceForTjunc: f->original"

		if (f->numpoints <= MAXPOINTS) { // the face is now small enough
										 // without more cutting
			// so copy it back to the original
			*original = *f;
			original->original = chain;
			original->next = newlist;
			newlist = original;
			return;
		}

		tjuncfaces++;

	restart:
		// find the last corner
		dir = vector_subtract(f->pts[f->numpoints - 1], f->pts[0]);
		normalize_vector(dir);
		for (lastcorner = f->numpoints - 1; lastcorner > 0; lastcorner--) {
			test = vector_subtract(
				f->pts[lastcorner - 1], f->pts[lastcorner]
			);
			normalize_vector(test);
			v = dot_product(test, dir);
			if (v < 1.0 - ON_EPSILON || v > 1.0 + ON_EPSILON) {
				break;
			}
		}

		// find the first corner
		dir = vector_subtract(f->pts[1], f->pts[0]);
		normalize_vector(dir);
		for (firstcorner = 1; firstcorner < f->numpoints - 1;
			 firstcorner++) {
			test = vector_subtract(
				f->pts[firstcorner + 1], f->pts[firstcorner]
			);
			normalize_vector(test);
			v = dot_product(test, dir);
			if (v < 1.0 - ON_EPSILON || v > 1.0 + ON_EPSILON) {
				break;
			}
		}

		if (firstcorner + 2 >= MAXPOINTS) {
			// rotate the point winding
			test = f->pts[0];
			for (i = 1; i < f->numpoints; i++) {
				f->pts[i - 1] = f->pts[i];
			}
			f->pts[f->numpoints - 1] = test;
			goto restart;
		}

		// cut off as big a piece as possible, less than MAXPOINTS, and not
		// past lastcorner

		newface = NewFaceFromFace(f);

		hlassume(
			f->original == nullptr, assume_ValidPointer
		); // "SplitFaceForTjunc: f->original"

		newface->original = chain;
		chain = newface;
		newface->next = newlist;
		newlist = newface;
		if (f->numpoints - firstcorner <= MAXPOINTS) {
			newface->numpoints = firstcorner + 2;
		} else if (lastcorner + 2 < MAXPOINTS
				   && f->numpoints - lastcorner <= MAXPOINTS) {
			newface->numpoints = lastcorner + 2;
		} else {
			newface->numpoints = MAXPOINTS;
		}

		for (i = 0; i < newface->numpoints; i++) {
			newface->pts[i] = f->pts[i];
		}

		for (i = newface->numpoints - 1; i < f->numpoints; i++) {
			f->pts[i - (newface->numpoints - 2)] = f->pts[i];
		}
		f->numpoints -= (newface->numpoints - 2);
	} while (1);
}

/*
 * ===============
 * FixFaceEdges
 *
 * ===============
 */
static void FixFaceEdges(face_t* f) {
	int i;
	int j;
	int k;
	wedge_t* w;
	wvert_t* v;

	*superface = *f;

restart:
	for (i = 0; i < superface->numpoints; i++) {
		j = (i + 1) % superface->numpoints;

		double t1;
		double t2;
		w = FindEdge(superface->pts[i], superface->pts[j], &t1, &t2);

		for (v = w->head.next; v->t < t1 + ON_EPSILON; v = v->next)
			;

		if (v->t < t2 - ON_EPSILON) {
			tjuncs++;
			// insert a new vertex here
			for (k = superface->numpoints; k > j; k--) {
				superface->pts[k] = superface->pts[k - 1];
			}
			superface->pts[j] = vector_fma(w->dir, v->t, w->origin);
			superface->numpoints++;
			hlassume(
				superface->numpoints < MAX_SUPERFACEEDGES,
				assume_MAX_SUPERFACEEDGES
			);
			goto restart;
		}
	}

	if (superface->numpoints <= MAXPOINTS) {
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
	face_t* f;

	if (node->planenum == PLANENUM_LEAF) {
		return;
	}

	for (f = node->faces; f; f = f->next) {
		AddFaceEdges(f);
	}

	tjunc_find_r(node->children[0]);
	tjunc_find_r(node->children[1]);
}

static void tjunc_fix_r(node_t* node) {
	face_t* f;
	face_t* next;

	if (node->planenum == PLANENUM_LEAF) {
		return;
	}

	newlist = nullptr;

	for (f = node->faces; f; f = next) {
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
	double3_array maxs, mins;
	int i;

	Verbose("---- tjunc ----\n");

	if (g_notjunc) {
		return;
	}

	//
	// identify all points on common edges
	//

	// origin points won't allways be inside the map, so extend the hash
	// area
	for (i = 0; i < 3; i++) {
		if (fabs(headnode->maxs[i]) > fabs(headnode->mins[i])) {
			maxs[i] = fabs(headnode->maxs[i]);
		} else {
			maxs[i] = fabs(headnode->mins[i]);
		}
	}
	mins = negate_vector(maxs);

	InitHash(mins, maxs);

	numwedges = numwverts = 0;

	tjunc_find_r(headnode);

	Verbose("%i world edges  %i edge points\n", numwedges, numwverts);

	//
	// add extra vertexes on edges where needed
	//
	tjuncs = tjuncfaces = 0;

	tjunc_fix_r(headnode);

	Verbose("%i edges added by tjunctions\n", tjuncs);
	Verbose("%i faces added by tjunctions\n", tjuncfaces);
}
