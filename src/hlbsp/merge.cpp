#include "bsp5.h"

//  TryMerge
//  MergeFaceToList
//  FreeMergeListScraps
//  MergePlaneFaces
//  MergeAll

#define CONTINUOUS_EPSILON ON_EPSILON

// =====================================================================================
//  TryMerge
//      If two polygons share a common edge and the edges that meet at the
//      common points are both inside the other polygons, merge them
//      Returns NULL if the faces couldn't be merged, or the new face.
//      The originals will NOT be freed.
// =====================================================================================
static face_t* TryMerge(face_t* f1, face_t* f2) {
	face_t* newf;
	int i;
	int j;
	int k;
	double3_array normal;
	double3_array delta;
	double3_array planenormal;
	double dot;
	mapplane_t* plane;
	bool keep1;
	bool keep2;

	if (f1->numpoints == -1 || f2->numpoints == -1) {
		return nullptr;
	}
	if (f1->texturenum != f2->texturenum) {
		return nullptr;
	}
	if (f1->contents != f2->contents) {
		return nullptr;
	}
	if (f1->planenum != f2->planenum) {
		return nullptr;
	}
	if (f1->facestyle != f2->facestyle) {
		return nullptr;
	}
	if (f1->detailLevel != f2->detailLevel) {
		return nullptr;
	}

	//
	// find a common edge
	//

	double3_array p1{};
	double3_array p2{};
	j = 0;

	for (i = 0; i < f1->numpoints; i++) {
		p1 = f1->pts[i];
		p2 = f1->pts[(i + 1) % f1->numpoints];
		for (j = 0; j < f2->numpoints; j++) {
			double3_array const & p3{ f2->pts[j] };
			double3_array const & p4{ f2->pts[(j + 1) % f2->numpoints] };
			for (k = 0; k < 3; k++) {
				if (fabs(p1[k] - p4[k]) > ON_EPSILON) {
					break;
				}
				if (fabs(p2[k] - p3[k]) > ON_EPSILON) {
					break;
				}
			}
			if (k == 3) {
				break;
			}
		}
		if (j < f2->numpoints) {
			break;
		}
	}

	if (i == f1->numpoints) {
		return nullptr; // no matching edges
	}

	//
	// check slope of connected lines
	// if the slopes are colinear, the point can be removed
	//
	plane = &g_mapplanes[f1->planenum];
	planenormal = plane->normal;

	double3_array back{ f1->pts[(i + f1->numpoints - 1) % f1->numpoints] };
	delta = vector_subtract(p1, back);
	normal = cross_product(planenormal, delta);
	normalize_vector(normal);

	back = f2->pts[(j + 2) % f2->numpoints];
	delta = vector_subtract(back, p1);
	dot = dot_product(delta, normal);
	if (dot > CONTINUOUS_EPSILON) {
		return nullptr; // not a convex polygon
	}
	keep1 = dot < -CONTINUOUS_EPSILON;

	back = f1->pts[(i + 2) % f1->numpoints];
	delta = vector_subtract(back, p2);
	normal = cross_product(planenormal, delta);
	normalize_vector(normal);

	back = f2->pts[(j + f2->numpoints - 1) % f2->numpoints];
	delta = vector_subtract(back, p2);
	dot = dot_product(delta, normal);
	if (dot > CONTINUOUS_EPSILON) {
		return nullptr; // not a convex polygon
	}
	keep2 = dot < -CONTINUOUS_EPSILON;

	//
	// build the new polygon
	//
	if (f1->numpoints + f2->numpoints > MAXEDGES) {
		//              Error ("TryMerge: too many edges!");
		return nullptr;
	}

	newf = NewFaceFromFace(f1);

	// copy first polygon
	for (k = (i + 1) % f1->numpoints; k != i; k = (k + 1) % f1->numpoints) {
		if (k == (i + 1) % f1->numpoints && !keep2) {
			continue;
		}

		newf->pts[newf->numpoints] = f1->pts[k];
		newf->numpoints++;
	}

	// copy second polygon
	for (int l = (j + 1) % f2->numpoints; l != j;
		 l = (l + 1) % f2->numpoints) {
		if (l == (j + 1) % f2->numpoints && !keep1) {
			continue;
		}
		newf->pts[newf->numpoints] = f2->pts[l];
		newf->numpoints++;
	}

	return newf;
}

// =====================================================================================
//  MergeFaceToList
// =====================================================================================
static face_t* MergeFaceToList(face_t* face, face_t* list) {
	face_t* newf;
	face_t* f;

	for (f = list; f; f = f->next) {
		// CheckColinear (f);
		newf = TryMerge(face, f);
		if (!newf) {
			continue;
		}
		FreeFace(face);
		f->numpoints = -1; // merged out
		return MergeFaceToList(newf, list);
	}

	// didn't merge, so add at start
	face->next = list;
	return face;
}

// =====================================================================================
//  FreeMergeListScraps
// =====================================================================================
static face_t* FreeMergeListScraps(face_t* merged) {
	face_t* head;
	face_t* next;

	head = nullptr;
	for (; merged; merged = next) {
		next = merged->next;
		if (merged->numpoints == -1) {
			FreeFace(merged);
		} else {
			merged->next = head;
			head = merged;
		}
	}

	return head;
}

// =====================================================================================
//  MergePlaneFaces
// =====================================================================================
void MergePlaneFaces(surface_t* plane) {
	face_t* f1;
	face_t* next;
	face_t* merged;

	merged = nullptr;

	for (f1 = plane->faces; f1; f1 = next) {
		next = f1->next;
		merged = MergeFaceToList(f1, merged);
	}

	// chain all of the non-empty faces to the plane
	plane->faces = FreeMergeListScraps(merged);
}

// =====================================================================================
//  MergeAll
// =====================================================================================
void MergeAll(surface_t* surfhead) {
	surface_t* surf;
	int mergefaces;
	face_t* f;

	Verbose("---- MergeAll ----\n");

	mergefaces = 0;
	for (surf = surfhead; surf; surf = surf->next) {
		MergePlaneFaces(surf);
		for (f = surf->faces; f; f = f->next) {
			mergefaces++;
		}
	}

	Verbose("%i mergefaces\n", mergefaces);
}
