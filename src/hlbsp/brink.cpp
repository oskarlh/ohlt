#include "brink.h"

#include "bsp5.h"

#include <cstring>
#include <list>
#include <map>
#include <vector>

// TODO: we should consider corners in addition to brinks.
// TODO: use bcircle_t structure only to find out all possible "movement"s,
// then send then down the bsp tree to determine which leafs may incorrectly
// block the movement.

// The image of a typical buggy brink (with type BrinkFloorBlocking):
//
//                brinkstoppoint
//        x------------x
//       /            / \.
//      /            /   \.
//     /   move >>>>xblocked
//    /            /       \.
//   /            /         x
//  x------------x         /
//  |      brinkstartpoint/
//  |            | \     /             Z
//  |            |  \   /              |  Y
//  |            |   \ /               | /
//  x------------x----x                |/
//                                     O-------X
//

static bbrink_t CopyBrink(bbrink_t const & other) {
	bbrink_t b{};
	b.direction = other.direction;
	b.start = other.start;
	b.stop = other.stop;
	b.numnodes = other.numnodes;
	b.nodes = other.nodes;

	// For some reason we're not copying the whole object including the edge
	// pointer???
	return b;
}

static bbrink_t
CreateBrink(double3_array const & start, double3_array const & stop) {
	bbrink_t b{};
	b.start = start;
	b.stop = stop;
	b.direction = vector_subtract(stop, start);

	b.numnodes = 1;
	bbrinknode_t newnode;
	newnode.isleaf = true;
	newnode.clipnode = nullptr;
	b.nodes.push_back(newnode);

	// CreateBrink must be followed by BrinkSplitClipnode
	return b;
}

void PrintBrink(bbrink_t const & b) {
	Log("direction %f %f %f start %f %f %f stop %f %f %f\n",
		b.direction[0],
		b.direction[1],
		b.direction[2],
		b.start[0],
		b.start[1],
		b.start[2],
		b.stop[0],
		b.stop[1],
		b.stop[2]);
	Log("numnodes %d\n", b.numnodes);
	for (int i = 0; i < b.numnodes; i++) {
		bbrinknode_t const * n = &b.nodes[i];
		if (n->isleaf) {
			Log("leaf[%d] content %d\n", i, std::to_underlying(n->content));
		} else {
			Log("node[%d]-[%d:%d] plane %f %f %f %f\n",
				i,
				n->children[0],
				n->children[1],
				n->plane->normal[0],
				n->plane->normal[1],
				n->plane->normal[2],
				n->plane->dist);
		}
	}
}

void BrinkSplitClipnode(
	bbrink_t* b,
	mapplane_t const * plane,
	int planenum,
	bclipnode_t* prev,
	bclipnode_t* n0,
	bclipnode_t* n1
) {
	int found{ 0 };
	int numfound{ 0 };
	for (int i = 0; i < b->numnodes; i++) {
		bbrinknode_t* node = &b->nodes[i];
		if (node->isleaf && node->clipnode == prev) {
			found = i;
			numfound++;
		}
	}
	if (numfound == 0) {
		PrintOnce(
			"BrinkSplitClipnode: internal error: couldn't find clipnode"
		);
		hlassume(false, assume_first);
	}
	if (numfound > 1) {
		PrintOnce(
			"BrinkSplitClipnode: internal error: found more than one clipnode"
		);
		hlassume(false, assume_first);
	}
	if (n0 == n1) {
		PrintOnce("BrinkSplitClipnode: internal error: n0 == n1");
		hlassume(false, assume_first);
	}
	b->nodes.resize(b->numnodes + 2);
	bbrinknode_t* node = &b->nodes[found];
	bbrinknode_t* front = &b->nodes[b->numnodes];
	bbrinknode_t* back = &b->nodes[b->numnodes + 1];

	node->clipnode = nullptr;
	node->isleaf = false;
	node->plane = plane;
	node->planenum = planenum;
	node->children[0] = b->numnodes;
	node->children[1] = b->numnodes + 1;

	front->isleaf = true;
	front->content = n0->content;
	front->clipnode = n0;

	back->isleaf = true;
	back->content = n1->content;
	back->clipnode = n1;

	b->numnodes += 2;
}

void BrinkReplaceClipnode(bbrink_t* b, bclipnode_t* prev, bclipnode_t* n) {
	int found{ 0 };
	int numfound{ 0 };
	for (int i = 0; i < b->numnodes; i++) {
		bbrinknode_t* node = &b->nodes[i];
		if (node->isleaf && node->clipnode == prev) {
			found = i;
			numfound++;
		}
	}
	if (numfound == 0) {
		PrintOnce(
			"BrinkSplitClipnode: internal error: couldn't find clipnode"
		);
		hlassume(false, assume_first);
	}
	if (numfound > 1) {
		PrintOnce(
			"BrinkSplitClipnode: internal error: found more than one clipnode"
		);
		hlassume(false, assume_first);
	}
	bbrinknode_t* node = &b->nodes[found];
	node->clipnode = n;
	node->content = n->content;
}

// compute the structure of the whole bsp tree

struct btreepoint_t final {
	btreeedge_l* edges; // this is a reversed reference
	double3_array v;
	double tmp_dist;
	face_side tmp_side;
	bool infinite;
	bool tmp_tested;
};

struct btreeedge_t final {
	btreeface_l* faces;		// this is a reversed reference
	bbrink_t* brink;		// not defined for infinite edges
	btreepoint_r points[2]; // pointing from points[1] to points[0]
	face_side tmp_side;
	bool infinite; // both points are infinite (i.e. this edge lies on the
				   // bounding box)
	bool tmp_tested;
	bool tmp_onleaf[2];
};

struct btreeface_t final {
	mapplane_t const * plane; // not defined for infinite face
	btreeedge_l* edges;	  // empty faces are allowed (in order to preserve
						  // topological correctness)
	btreeleaf_r leafs[2]; // pointing from leafs[0] to leafs[1] // this is a
						  // reversed reference

	int planenum;
	face_side tmp_side;
	bool infinite; // when the face is infinite, all its edges must also be
				   // infinite
	bool planeside; // if ture, this face is pointing at -plane->normal
	bool tmp_tested;
};

btreepoint_t* AllocTreepoint(int& numobjects, bool infinite) {
	numobjects++;
	btreepoint_t* tp = new btreepoint_t{};
	hlassume(tp != nullptr, assume_NoMemory);
	tp->edges = new btreeedge_l();
	tp->infinite = infinite;
	return tp;
}

btreeedge_t* AllocTreeedge(int& numobjects, bool infinite) {
	numobjects++;
	btreeedge_t* te = new btreeedge_t{};
	hlassume(te != nullptr, assume_NoMemory);
	te->points[0].p = nullptr;
	te->points[0].side = false;
	te->points[1].p = nullptr;
	te->points[1].side = true;
	te->faces = new btreeface_l();
	te->infinite = infinite;
	// should be followed by SetEdgePoints
	return te;
}

void AttachPointToEdge(btreeedge_t* te, btreepoint_t* tp, bool side) {
	if (te->points[side].p) {
		PrintOnce("AttachPointToEdge: internal error: point occupied.");
		hlassume(false, assume_first);
	}
	if (te->infinite && !tp->infinite) {
		PrintOnce(
			"AttachPointToEdge: internal error: attaching a finite object to an infinite object."
		);
		hlassume(false, assume_first);
	}
	te->points[side].p = tp;

	btreeedge_r er;
	er.e = te;
	er.side = side;
	tp->edges->push_back(er);
}

void SetEdgePoints(btreeedge_t* te, btreepoint_t* tp0, btreepoint_t* tp1) {
	AttachPointToEdge(te, tp0, false);
	AttachPointToEdge(te, tp1, true);
}

btreeface_t* AllocTreeface(int& numobjects, bool infinite) {
	numobjects++;
	btreeface_t* tf = new btreeface_t{};
	hlassume(tf != nullptr, assume_NoMemory);
	tf->edges = new btreeedge_l{};
	tf->leafs[0].l = nullptr;
	tf->leafs[0].side = false;
	tf->leafs[1].l = nullptr;
	tf->leafs[1].side = true;
	tf->infinite = infinite;
	return tf;
}

void AttachEdgeToFace(btreeface_t* tf, btreeedge_t* te, int side) {
	if (tf->infinite && !te->infinite) {
		PrintOnce(
			"AttachEdgeToFace: internal error: attaching a finite object to an infinite object."
		);
		hlassume(false, assume_first);
	}
	btreeedge_r er;
	er.e = te;
	er.side = side;
	tf->edges->push_back(er);

	btreeface_r fr;
	fr.f = tf;
	fr.side = side;
	te->faces->push_back(fr);
}

void AttachFaceToLeaf(btreeleaf_t* tl, btreeface_t* tf, int side) {
	if (tl->infinite && !tf->infinite) {
		PrintOnce(
			"AttachFaceToLeaf: internal error: attaching a finite object to an infinite object."
		);
		hlassume(false, assume_first);
	}
	btreeface_r fr;
	fr.f = tf;
	fr.side = side;
	tl->faces->push_back(fr);

	if (tf->leafs[side].l) {
		PrintOnce("AttachFaceToLeaf: internal error: leaf occupied.");
		hlassume(false, assume_first);
	}
	tf->leafs[side].l = tl;
}

void SetFaceLeafs(btreeface_t* tf, btreeleaf_t* tl0, btreeleaf_t* tl1) {
	AttachFaceToLeaf(tl0, tf, false);
	AttachFaceToLeaf(tl1, tf, true);
}

btreeleaf_t* AllocTreeleaf(int& numobjects, bool infinite) {
	numobjects++;
	btreeleaf_t* tl = new btreeleaf_t{};
	hlassume(tl != nullptr, assume_NoMemory);
	tl->faces = new btreeface_l{};
	tl->infinite = infinite;
	return tl;
}

btreeleaf_t* BuildOutside(int& numobjects) {
	btreeleaf_t* leaf_outside;
	leaf_outside = AllocTreeleaf(numobjects, true);
	leaf_outside->clipnode = nullptr;
	return leaf_outside;
}

btreeleaf_t* BuildBaseCell(
	int& numobjects,
	bclipnode_t* clipnode,
	double range,
	btreeleaf_t* leaf_outside
) {
	btreepoint_t* tp[8];
	for (int i = 0; i < 8; i++) {
		tp[i] = AllocTreepoint(numobjects, true);
		if (i & 1) {
			tp[i]->v[0] = range;
		} else {
			tp[i]->v[0] = -range;
		}
		if (i & 2) {
			tp[i]->v[1] = range;
		} else {
			tp[i]->v[1] = -range;
		}
		if (i & 4) {
			tp[i]->v[2] = range;
		} else {
			tp[i]->v[2] = -range;
		}
	}
	btreeedge_t* te[12];
	for (int i = 0; i < 12; i++) {
		te[i] = AllocTreeedge(numobjects, true);
	}
	SetEdgePoints(te[0], tp[1], tp[0]);
	SetEdgePoints(te[1], tp[3], tp[2]);
	SetEdgePoints(te[2], tp[5], tp[4]);
	SetEdgePoints(te[3], tp[7], tp[6]);
	SetEdgePoints(te[4], tp[2], tp[0]);
	SetEdgePoints(te[5], tp[3], tp[1]);
	SetEdgePoints(te[6], tp[6], tp[4]);
	SetEdgePoints(te[7], tp[7], tp[5]);
	SetEdgePoints(te[8], tp[4], tp[0]);
	SetEdgePoints(te[9], tp[5], tp[1]);
	SetEdgePoints(te[10], tp[6], tp[2]);
	SetEdgePoints(te[11], tp[7], tp[3]);
	btreeface_t* tf[6];
	for (int i = 0; i < 6; i++) {
		tf[i] = AllocTreeface(numobjects, true);
	}
	AttachEdgeToFace(tf[0], te[4], true);
	AttachEdgeToFace(tf[0], te[6], false);
	AttachEdgeToFace(tf[0], te[8], false);
	AttachEdgeToFace(tf[0], te[10], true);
	AttachEdgeToFace(tf[1], te[5], false);
	AttachEdgeToFace(tf[1], te[7], true);
	AttachEdgeToFace(tf[1], te[9], true);
	AttachEdgeToFace(tf[1], te[11], false);
	AttachEdgeToFace(tf[2], te[0], false);
	AttachEdgeToFace(tf[2], te[2], true);
	AttachEdgeToFace(tf[2], te[8], true);
	AttachEdgeToFace(tf[2], te[9], false);
	AttachEdgeToFace(tf[3], te[1], true);
	AttachEdgeToFace(tf[3], te[3], false);
	AttachEdgeToFace(tf[3], te[10], false);
	AttachEdgeToFace(tf[3], te[11], true);
	AttachEdgeToFace(tf[4], te[0], true);
	AttachEdgeToFace(tf[4], te[1], false);
	AttachEdgeToFace(tf[4], te[4], false);
	AttachEdgeToFace(tf[4], te[5], true);
	AttachEdgeToFace(tf[5], te[2], false);
	AttachEdgeToFace(tf[5], te[3], true);
	AttachEdgeToFace(tf[5], te[6], true);
	AttachEdgeToFace(tf[5], te[7], false);
	btreeleaf_t* tl;
	tl = AllocTreeleaf(numobjects, false);
	for (int i = 0; i < 6; i++) {
		SetFaceLeafs(tf[i], tl, leaf_outside);
	}
	tl->clipnode = clipnode;
	return tl;
}

btreepoint_t* GetPointFromEdge(btreeedge_t* te, bool side) {
	if (!te->points[side].p) {
		PrintOnce("GetPointFromEdge: internal error: point not set.");
		hlassume(false, assume_first);
	}
	return te->points[side].p;
}

void RemoveEdgeFromList(btreeedge_l* el, btreeedge_t* te, bool side) {
	btreeedge_l::iterator ei;
	for (ei = el->begin(); ei != el->end(); ei++) {
		if (ei->e == te && ei->side == side) {
			el->erase(ei);
			return; // only remove one copy if there are many (in order to
					// preserve topological correctness)
		}
	}
	PrintOnce("RemoveEdgeFromList: internal error: edge not found.");
	hlassume(false, assume_first);
}

void RemovePointFromEdge(
	btreeedge_t* te, btreepoint_t* tp, bool side
) // warning: the point will not be freed
{
	if (te->points[side].p != tp) {
		PrintOnce("RemovePointFromEdge: internal error: point not found.");
		hlassume(false, assume_first);
	}
	te->points[side].p = nullptr;

	RemoveEdgeFromList(tp->edges, te, side);
}

void DeletePoint(int& numobjects, btreepoint_t* tp) {
	if (!tp->edges->empty()) {
		PrintOnce("DeletePoint: internal error: point used by edge.");
		hlassume(false, assume_first);
	}
	delete tp->edges;
	delete tp;
	numobjects--;
}

void RemoveFaceFromList(btreeface_l* fl, btreeface_t* tf, bool side) {
	btreeface_l::iterator fi;
	for (fi = fl->begin(); fi != fl->end(); fi++) {
		if (fi->f == tf && fi->side == side) {
			fl->erase(fi);
			return;
		}
	}
	PrintOnce("RemoveFaceFromList: internal error: face not found.");
	hlassume(false, assume_first);
}

void RemoveEdgeFromFace(btreeface_t* tf, btreeedge_t* te, bool side) {
	RemoveEdgeFromList(tf->edges, te, side);
	RemoveFaceFromList(te->faces, tf, side);
}

void DeleteEdge(
	int& numobjects, btreeedge_t* te
) // warning: points in this edge could be freed if not reference by any
  // other edges
{
	if (!te->faces->empty()) {
		PrintOnce("DeleteEdge: internal error: edge used by face.");
		hlassume(false, assume_first);
	}
	if (!te->infinite) {
		delete te->brink;
	}
	for (int side = 0; side < 2; side++) {
		btreepoint_t* tp;
		tp = GetPointFromEdge(te, side);
		RemovePointFromEdge(te, tp, side);
		if (tp->edges->empty()) {
			DeletePoint(numobjects, tp);
		}
	}
	delete te->faces;
	delete te;
	numobjects--;
}

btreeleaf_t* GetLeafFromFace(btreeface_t* tf, bool side) {
	if (!tf->leafs[side].l) {
		PrintOnce("GetLeafFromFace: internal error: Leaf not set.");
		hlassume(false, assume_first);
	}
	return tf->leafs[side].l;
}

void RemoveFaceFromLeaf(btreeleaf_t* tl, btreeface_t* tf, bool side) {
	if (tf->leafs[side].l != tl) {
		PrintOnce("RemoveFaceFromLeaf: internal error: leaf not found.");
		hlassume(false, assume_first);
	}
	tf->leafs[side].l = nullptr;

	RemoveFaceFromList(tl->faces, tf, side);
}

void DeleteFace(
	int& numobjects, btreeface_t* tf
) // warning: edges in this face could be freed if not reference by any
  // other faces
{
	btreeedge_l::iterator ei;
	while ((ei = tf->edges->begin()) != tf->edges->end()) {
		btreeedge_t* te = ei->e;
		RemoveFaceFromList(te->faces, tf, ei->side);
		tf->edges->erase(ei);
		if (te->faces->empty()) {
			DeleteEdge(numobjects, te);
		}
	}
	for (int side = 0; side < 2; side++) {
		if (tf->leafs[side].l) {
			PrintOnce("DeleteFace: internal error: face used by leaf.");
			hlassume(false, assume_first);
		}
	}
	delete tf->edges;
	delete tf;
	numobjects--;
}

void DeleteLeaf(int& numobjects, btreeleaf_t* tl) {
	btreeface_l::iterator fi;
	while ((fi = tl->faces->begin()) != tl->faces->end()) {
		btreeface_t* tf = fi->f;
		RemoveFaceFromLeaf(tl, tf, fi->side);
		if (!tf->leafs[false].l && !tf->leafs[true].l) {
			DeleteFace(numobjects, tf);
		}
	}
	delete tl->faces;
	delete tl;
	numobjects--;
}

void SplitTreeLeaf(
	int& numobjects,
	btreeleaf_t* tl,
	mapplane_t const * plane,
	int planenum,
	double epsilon,
	btreeleaf_t*& front,
	btreeleaf_t*& back,
	bclipnode_t* c0,
	bclipnode_t* c1
) {
	btreeface_l::iterator fi;
	btreeedge_l::iterator ei;
	bool restart = false;

	// clear all the flags
	for (fi = tl->faces->begin(); fi != tl->faces->end(); fi++) {
		btreeface_t* tf = fi->f;
		tf->tmp_tested = false;
		for (ei = tf->edges->begin(); ei != tf->edges->end(); ei++) {
			btreeedge_t* te = ei->e;
			te->tmp_tested = false;
			for (int side = 0; side < 2; side++) {
				btreepoint_t* tp = GetPointFromEdge(te, side);
				tp->tmp_tested = false;
			}
		}
	}

	// split each point
	for (fi = tl->faces->begin(); fi != tl->faces->end(); fi++) {
		btreeface_t* tf = fi->f;
		for (ei = tf->edges->begin(); ei != tf->edges->end(); ei++) {
			btreeedge_t* te = ei->e;
			for (int side = 0; side < 2; side++) {
				btreepoint_t* tp = GetPointFromEdge(te, side);
				if (tp->tmp_tested) {
					continue;
				}
				tp->tmp_tested = true;
				double dist = dot_product(tp->v, plane->normal)
					- plane->dist;
				tp->tmp_dist = dist;
				if (dist > epsilon) {
					tp->tmp_side = face_side::front;
				} else if (dist < -epsilon) {
					tp->tmp_side = face_side::back;
				} else {
					tp->tmp_side = face_side::on;
				}
			}
		}
	}

	// split each edge
	for (fi = tl->faces->begin(); fi != tl->faces->end(); fi++) {
		btreeface_t* tf = fi->f;
		for (ei = tf->edges->begin(); ei != tf->edges->end();
			 restart ? restart = false, ei = tf->edges->begin() : ei++) {
			btreeedge_t* te = ei->e;
			if (te->tmp_tested) // splitted
			{
				continue;
			}
			te->tmp_tested = true;
			te->tmp_side = face_side::on;
			for (int side = 0; side < 2; side++) {
				btreepoint_t* tp = GetPointFromEdge(te, side);
				if (te->tmp_side == face_side::on) {
					te->tmp_side = tp->tmp_side;
				} else if (tp->tmp_side != face_side::on
						   && tp->tmp_side != te->tmp_side) {
					te->tmp_side = face_side::cross;
				}
			}
			// The plane does not necessarily split the leaf into two,
			// because of epsilon problem etc., and this will cause "Error:
			// CollectBrinks_r: not leaf" on some maps. In addition, by the
			// time of this step (split edges), the plane has not splitted
			// the leaf yet, so splitting the brink leafs now will break the
			// integrety of the entire geometry. (We want the four steps to
			// be as independent on each other as possible, that is, the
			// entire geometry remains valid after each step.)
			if (te->tmp_side == face_side::cross) {
				btreepoint_t* tp0 = GetPointFromEdge(te, false);
				btreepoint_t* tp1 = GetPointFromEdge(te, true);
				btreepoint_t* tpmid = AllocTreepoint(
					numobjects, te->infinite
				);
				tpmid->tmp_tested = true;
				tpmid->tmp_dist = 0;
				tpmid->tmp_side = face_side::on;
				double frac = tp0->tmp_dist
					/ (tp0->tmp_dist - tp1->tmp_dist);
				for (int k = 0; k < 3; k++) {
					tpmid->v[k] = tp0->v[k]
						+ frac * (tp1->v[k] - tp0->v[k]);
				}
				btreeedge_t* te0 = AllocTreeedge(numobjects, te->infinite);
				SetEdgePoints(te0, tp0, tpmid);
				te0->tmp_tested = true;
				te0->tmp_side = tp0->tmp_side;
				if (!te0->infinite) {
					te0->brink = new bbrink_t{ CopyBrink(*te->brink) };
					te0->brink->start = tpmid->v;
					te0->brink->stop = tp0->v;
				}
				btreeedge_t* te1 = AllocTreeedge(numobjects, te->infinite);
				SetEdgePoints(te1, tpmid, tp1);
				te1->tmp_tested = true;
				te1->tmp_side = tp1->tmp_side;
				if (!te1->infinite) {
					te1->brink = new bbrink_t(CopyBrink(*te->brink));
					te1->brink->start = tp1->v;
					te1->brink->stop = tpmid->v;
				}
				btreeface_l::iterator fj;
				while ((fj = te->faces->begin()) != te->faces->end()) {
					AttachEdgeToFace(fj->f, te0, fj->side);
					AttachEdgeToFace(fj->f, te1, fj->side);
					RemoveEdgeFromFace(fj->f, te, fj->side);
				}
				DeleteEdge(numobjects, te);
				restart = true;
			}
		}
	}

	// split each face
	for (fi = tl->faces->begin(); fi != tl->faces->end();
		 restart ? restart = false, fi = tl->faces->begin() : fi++) {
		btreeface_t* tf = fi->f;
		if (tf->tmp_tested) {
			continue;
		}
		tf->tmp_tested = true;
		tf->tmp_side = face_side::on;
		for (ei = tf->edges->begin(); ei != tf->edges->end(); ei++) {
			if (tf->tmp_side == face_side::on) {
				tf->tmp_side = ei->e->tmp_side;
			} else if (ei->e->tmp_side != face_side::on
					   && ei->e->tmp_side != tf->tmp_side) {
				tf->tmp_side = face_side::cross;
			}
		}
		if (tf->tmp_side == face_side::cross) {
			btreeface_t *frontface, *backface;
			frontface = AllocTreeface(numobjects, tf->infinite);
			if (!tf->infinite) {
				frontface->plane = tf->plane;
				frontface->planenum = tf->planenum;
				frontface->planeside = tf->planeside;
			}
			SetFaceLeafs(
				frontface,
				GetLeafFromFace(tf, false),
				GetLeafFromFace(tf, true)
			);
			frontface->tmp_tested = true;
			frontface->tmp_side = face_side::front;
			backface = AllocTreeface(numobjects, tf->infinite);
			if (!tf->infinite) {
				backface->plane = tf->plane;
				backface->planenum = tf->planenum;
				backface->planeside = tf->planeside;
			}
			SetFaceLeafs(
				backface,
				GetLeafFromFace(tf, false),
				GetLeafFromFace(tf, true)
			);
			backface->tmp_tested = true;
			backface->tmp_side = face_side::back;

			std::map<btreepoint_t*, int> vertexes;
			std::map<btreepoint_t*, int>::iterator vertex, vertex2;
			for (ei = tf->edges->begin(); ei != tf->edges->end(); ei++) {
				if (ei->e->tmp_side != face_side::back) {
					AttachEdgeToFace(frontface, ei->e, ei->side);
				} else {
					AttachEdgeToFace(backface, ei->e, ei->side);

					btreeedge_t* e = ei->e;
					for (int side = 0; side < 2; side++) {
						btreepoint_t* p = GetPointFromEdge(e, side);
						vertexes[p]
							+= ((bool) side == ei->side ? 1 : -1
							); // the default value is 0 if vertexes[p] does
							   // not exist
						vertex = vertexes.find(p);
						if (vertex->second == 0) {
							vertexes.erase(vertex);
						}
					}
				}
			}
			if (vertexes.size() != 2) {
				Developer(
					developer_level::warning,
					"SplitTreeLeaf: got invalid edge from split\n"
				);
			}

			while (1) {
				for (vertex = vertexes.begin(); vertex != vertexes.end();
					 vertex++) {
					if (vertex->second > 0) {
						break;
					}
				}
				for (vertex2 = vertexes.begin(); vertex2 != vertexes.end();
					 vertex2++) {
					if (vertex2->second < 0) {
						break;
					}
				}
				if (vertex == vertexes.end() && vertex2 == vertexes.end()) {
					break;
				}
				if (vertex == vertexes.end() || vertex2 == vertexes.end()) {
					PrintOnce(
						"SplitTreeLeaf: internal error: couldn't link edges"
					);
					hlassume(false, assume_first);
				}
				if (vertex->second != 1 || vertex2->second != -1) {
					Developer(
						developer_level::warning,
						"SplitTreeLeaf: got deformed edge from split\n"
					);
				}
				if (vertex->first->tmp_side != face_side::on
					|| vertex2->first->tmp_side != face_side::on) {
					PrintOnce(
						"SplitTreeLeaf: internal error: tmp_side != face_side::on"
					);
					hlassume(false, assume_first);
				}

				btreeedge_t* te;
				te = AllocTreeedge(numobjects, tf->infinite);
				SetEdgePoints(te, vertex->first, vertex2->first);
				if (!te->infinite) {
					te->brink = new bbrink_t{
						CreateBrink(vertex2->first->v, vertex->first->v)
					};
					if (GetLeafFromFace(tf, tf->planeside)->infinite
						|| GetLeafFromFace(tf, !tf->planeside)->infinite) {
						PrintOnce(
							"SplitTreeLeaf: internal error: an infinite object contains a finite object"
						);
						hlassume(false, assume_first);
					}
					BrinkSplitClipnode(
						te->brink,
						tf->plane,
						tf->planenum,
						nullptr,
						GetLeafFromFace(tf, tf->planeside)->clipnode,
						GetLeafFromFace(tf, !tf->planeside)->clipnode
					);
				}
				te->tmp_tested = true;
				te->tmp_side = face_side::on;
				AttachEdgeToFace(frontface, te, false);
				AttachEdgeToFace(backface, te, true);

				vertex->second--;
				vertex2->second++;
			}

			for (int side = 0; side < 2; side++) {
				RemoveFaceFromLeaf(GetLeafFromFace(tf, side), tf, side);
			}
			DeleteFace(numobjects, tf);
			restart = true;
		}
	}

	// split the leaf
	{
		if (tl->infinite) {
			PrintOnce(
				"SplitTreeLeaf: internal error: splitting the infinite leaf"
			);
			hlassume(false, assume_first);
		}
		front = AllocTreeleaf(numobjects, tl->infinite);
		back = AllocTreeleaf(numobjects, tl->infinite);
		front->clipnode = c0;
		back->clipnode = c1;

		face_side tmp_side = face_side::on;
		for (fi = tl->faces->begin(); fi != tl->faces->end(); fi++) {
			if (tmp_side == face_side::on) {
				tmp_side = fi->f->tmp_side;
			} else if (fi->f->tmp_side != face_side::on
					   && fi->f->tmp_side != tmp_side) {
				tmp_side = face_side::cross;
			}
		}

		std::map<btreeedge_t*, int> edges;
		std::map<btreeedge_t*, int>::iterator edge;

		while ((fi = tl->faces->begin()) != tl->faces->end()) {
			btreeface_t* tf = fi->f;
			int side = fi->side;
			RemoveFaceFromLeaf(
				tl, tf, side
			); // because we can only store 2 leafs for a face

			// fi is unusable now
			if (tf->tmp_side == face_side::front
				|| (tf->tmp_side == face_side::on
					&& tmp_side != face_side::back)) {
				AttachFaceToLeaf(front, tf, side);
			} else if (tf->tmp_side == face_side::back
					   || (tf->tmp_side == face_side::on
						   && tmp_side == face_side::back)) {
				AttachFaceToLeaf(back, tf, side);

				if (tmp_side == face_side::cross) {
					for (ei = tf->edges->begin(); ei != tf->edges->end();
						 ei++) {
						edges[ei->e] += (ei->side == (bool) side ? 1 : -1);
						edge = edges.find(ei->e);
						if (edge->second == 0) {
							edges.erase(edge);
						}
					}
				}
			}
		}

		if (tmp_side == face_side::cross) {
			btreeface_t* tf;
			tf = AllocTreeface(numobjects, tl->infinite);
			if (!tf->infinite) {
				tf->plane = plane;
				tf->planenum = planenum;
				tf->planeside = false;
			}
			tf->tmp_tested = true;
			tf->tmp_side = face_side::on;
			SetFaceLeafs(tf, front, back);
			for (edge = edges.begin(); edge != edges.end(); edge++) {
				if (edge->first->tmp_side != face_side::on) {
					PrintOnce("SplitTreeLeaf: internal error");
					hlassume(false, assume_first);
				}
				while (edge->second > 0) {
					AttachEdgeToFace(tf, edge->first, false);
					edge->second--;
				}
				while (edge->second < 0) {
					AttachEdgeToFace(tf, edge->first, true);
					edge->second++;
				}
			}
		}

		btreeleaf_t* frontback[2]{ front, back };
		for (std::size_t side = 0; side < 2; ++side) {
			for (fi = frontback[side]->faces->begin();
				 fi != frontback[side]->faces->end();
				 fi++) {
				for (ei = fi->f->edges->begin(); ei != fi->f->edges->end();
					 ei++) {
					ei->e->tmp_onleaf[0] = ei->e->tmp_onleaf[1] = false;
					ei->e->tmp_tested = false;
				}
			}
		}
		for (std::size_t side = 0; side < 2; ++side) {
			for (fi = frontback[side]->faces->begin();
				 fi != frontback[side]->faces->end();
				 fi++) {
				for (ei = fi->f->edges->begin(); ei != fi->f->edges->end();
					 ei++) {
					ei->e->tmp_onleaf[side] = true;
				}
			}
		}
		for (std::size_t side = 0; side < 2; ++side) {
			for (fi = frontback[side]->faces->begin();
				 fi != frontback[side]->faces->end();
				 fi++) {
				for (ei = fi->f->edges->begin(); ei != fi->f->edges->end();
					 ei++) {
					if (ei->e->tmp_tested) {
						continue;
					}
					ei->e->tmp_tested = true;
					if (!ei->e->infinite) {
						if (ei->e->tmp_onleaf[0] && ei->e->tmp_onleaf[1]) {
							if (ei->e->tmp_side != face_side::on) {
								PrintOnce("SplitTreeLeaf: internal error");
								hlassume(false, assume_first);
							}
							BrinkSplitClipnode(
								ei->e->brink,
								plane,
								planenum,
								tl->clipnode,
								c0,
								c1
							);
						} else if (ei->e->tmp_onleaf[0]) {
							if (ei->e->tmp_side == face_side::back) {
								PrintOnce("SplitTreeLeaf: internal error");
								hlassume(false, assume_first);
							}
							BrinkReplaceClipnode(
								ei->e->brink, tl->clipnode, c0
							);
						} else if (ei->e->tmp_onleaf[1]) {
							if (ei->e->tmp_side == face_side::front) {
								PrintOnce("SplitTreeLeaf: internal error");
								hlassume(false, assume_first);
							}
							BrinkReplaceClipnode(
								ei->e->brink, tl->clipnode, c1
							);
						}
					}
				}
			}
		}
		DeleteLeaf(numobjects, tl);
	}
}

void BuildTreeCells_r(int& numobjects, bclipnode_t* c) {
	if (c->isleaf) {
		return;
	}
	btreeleaf_t *tl, *front, *back;
	tl = c->treeleaf;
	SplitTreeLeaf(
		numobjects,
		tl,
		c->plane,
		c->planenum,
		ON_EPSILON,
		front,
		back,
		c->children[0],
		c->children[1]
	);
	c->treeleaf = nullptr;
	c->children[0]->treeleaf = front;
	c->children[1]->treeleaf = back;
	BuildTreeCells_r(numobjects, c->children[0]);
	BuildTreeCells_r(numobjects, c->children[1]);
}

#define MAXCLIPNODES (MAX_MAP_CLIPNODES * 8)

bclipnode_t* ExpandClipnodes_r(
	bclipnode_t* bclipnodes,
	int& numbclipnodes,
	dclipnode_t const * clipnodes,
	int headnode
) {
	if (numbclipnodes >= MAXCLIPNODES) {
		Error("ExpandClipnodes_r: exceeded MAXCLIPNODES");
	}
	bclipnode_t* c = &bclipnodes[numbclipnodes];
	numbclipnodes++;
	if (headnode < 0) {
		c->isleaf = true;
		c->content = contents_t{ headnode };
		c->partitions = nullptr;
	} else {
		c->isleaf = false;
		c->planenum = clipnodes[headnode].planenum;
		c->plane = &g_mapplanes[c->planenum];
		for (int k = 0; k < 2; k++) {
			c->children[k] = ExpandClipnodes_r(
				bclipnodes,
				numbclipnodes,
				clipnodes,
				clipnodes[headnode].children[k]
			);
		}
	}
	return c;
}

void ExpandClipnodes(
	bbrinkinfo_t* info, dclipnode_t const * clipnodes, int headnode
) {
	bclipnode_t* bclipnodes
		= new bclipnode_t[MAXCLIPNODES]; // 262144 * 30byte = 7.5MB
	hlassume(bclipnodes != nullptr, assume_NoMemory);
	info->numclipnodes = 0;
	ExpandClipnodes_r(bclipnodes, info->numclipnodes, clipnodes, headnode);
	info->clipnodes = new bclipnode_t[info->numclipnodes];
	hlassume(info->clipnodes != nullptr, assume_NoMemory);
	std::memcpy(
		info->clipnodes,
		bclipnodes,
		info->numclipnodes * sizeof(bclipnode_t)
	);
	for (int i = 0; i < info->numclipnodes; i++) {
		for (int k = 0; k < 2; k++) {
			info->clipnodes[i].children[k] = info->clipnodes
				+ (bclipnodes[i].children[k] - bclipnodes);
		}
	}
	delete[] bclipnodes;
}

void BuildTreeCells(bbrinkinfo_t* info) {
	info->numobjects = 0;
	info->leaf_outside = BuildOutside(info->numobjects);
	info->clipnodes[0].treeleaf = BuildBaseCell(
		info->numobjects,
		&info->clipnodes[0],
		hlbsp_bogus_range,
		info->leaf_outside
	);
	BuildTreeCells_r(info->numobjects, &info->clipnodes[0]);
}

void DeleteTreeCells_r(int& numobjects, bclipnode_t* node) {
	if (node->treeleaf) {
		DeleteLeaf(numobjects, node->treeleaf);
		node->treeleaf = nullptr;
	}
	if (!node->isleaf) {
		DeleteTreeCells_r(numobjects, node->children[0]);
		DeleteTreeCells_r(numobjects, node->children[1]);
	}
}

void DeleteTreeCells(bbrinkinfo_t* info) {
	DeleteLeaf(info->numobjects, info->leaf_outside);
	info->leaf_outside = nullptr;
	DeleteTreeCells_r(info->numobjects, &info->clipnodes[0]);
	if (info->numobjects != 0) {
		PrintOnce("DeleteTreeCells: internal error: numobjects != 0");
		hlassume(false, assume_first);
	}
}

void ClearMarks_r(bclipnode_t* node) {
	if (node->isleaf) {
		btreeface_l::iterator fi;
		btreeedge_l::iterator ei;
		for (fi = node->treeleaf->faces->begin();
			 fi != node->treeleaf->faces->end();
			 fi++) {
			for (ei = fi->f->edges->begin(); ei != fi->f->edges->end();
				 ei++) {
				ei->e->tmp_tested = false;
			}
		}
	} else {
		ClearMarks_r(node->children[0]);
		ClearMarks_r(node->children[1]);
	}
}

void CollectBrinks_r(bclipnode_t* node, int& numbrinks, bbrink_t** brinks) {
	if (node->isleaf) {
		btreeface_l::iterator fi;
		btreeedge_l::iterator ei;
		for (fi = node->treeleaf->faces->begin();
			 fi != node->treeleaf->faces->end();
			 fi++) {
			for (ei = fi->f->edges->begin(); ei != fi->f->edges->end();
				 ei++) {
				if (ei->e->tmp_tested) {
					continue;
				}
				ei->e->tmp_tested = true;
				if (!ei->e->infinite) {
					if (brinks != nullptr) {
						brinks[numbrinks] = ei->e->brink;
						brinks[numbrinks]->edge = ei->e;
						for (int i = 0; i < brinks[numbrinks]->numnodes;
							 i++) {
							bbrinknode_t* node
								= &brinks[numbrinks]->nodes[i];
							if (node->isleaf && !node->clipnode->isleaf) {
								PrintOnce(
									"CollectBrinks_r: internal error: not leaf"
								);
								hlassume(false, assume_first);
							}
						}
					}
					numbrinks++;
				}
			}
		}
	} else {
		CollectBrinks_r(node->children[0], numbrinks, brinks);
		CollectBrinks_r(node->children[1], numbrinks, brinks);
	}
}

void CollectBrinks(bbrinkinfo_t* info) {
	info->numbrinks = 0;
	ClearMarks_r(&info->clipnodes[0]);
	CollectBrinks_r(&info->clipnodes[0], info->numbrinks, nullptr);
	info->brinks = new bbrink_t* [info->numbrinks] {};
	info->numbrinks = 0;
	ClearMarks_r(&info->clipnodes[0]);
	CollectBrinks_r(&info->clipnodes[0], info->numbrinks, info->brinks);
}

void FreeBrinks(bbrinkinfo_t* info) {
	delete[] info->brinks;
}

struct bsurface_t;

struct bwedge_t final {
	contents_t content;
	int nodenum;
	bsurface_t* prev;
	bsurface_t* next;
};

struct bsurface_t final {
	double3_array normal; // pointing clockwise
	int nodenum;
	bool nodeside;
	bwedge_t* prev;
	bwedge_t* next;
};

#define MAXBRINKWEDGES 64

struct bcircle_t final {
	double3_array axis;
	double3_array basenormal;
	int numwedges[2]; // the front and back side of nodes[0]
	bwedge_t wedges[2][MAXBRINKWEDGES];		// in counterclosewise order
	bsurface_t surfaces[2][MAXBRINKWEDGES]; // the surface between two
											// adjacent wedges
};

bool CalculateCircle(bbrink_t* b, bcircle_t* c) {
	c->axis = b->direction;
	if (!normalize_vector(c->axis)) {
		return false;
	}
	c->basenormal = b->nodes[0].plane->normal;

	int side, i;
	for (side = 0; side < 2; side++) {
		double3_array facing = cross_product(c->basenormal, c->axis);
		facing = vector_scale(facing, side ? -1.0 : 1.0);
		if (normalize_vector(facing) < 1 - 0.01) {
			return false;
		}

		// sort the wedges
		c->numwedges[side] = 1;
		c->wedges[side][0].nodenum = b->nodes[0].children[side];
		c->surfaces[side][0].nodenum = 0;
		c->surfaces[side][0].nodeside = !side;
		while (1) {
			for (i = 0; i < c->numwedges[side]; i++) {
				int nodenum = c->wedges[side][i].nodenum;
				bbrinknode_t* node = &b->nodes[nodenum];
				if (!node->isleaf) {
					std::memmove(
						&c->wedges[side][i + 1],
						&c->wedges[side][i],
						(c->numwedges[side] - i) * sizeof(bwedge_t)
					);
					std::memmove(
						&c->surfaces[side][i + 2],
						&c->surfaces[side][i + 1],
						(c->numwedges[side] - 1 - i) * sizeof(bsurface_t)
					);
					c->numwedges[side]++;
					bool flipnode
						= (dot_product(node->plane->normal, facing) < 0);
					c->wedges[side][i].nodenum = node->children[flipnode];
					c->wedges[side][i + 1].nodenum
						= node->children[!flipnode];
					c->surfaces[side][i + 1].nodenum = nodenum;
					c->surfaces[side][i + 1].nodeside = flipnode;
					break;
				}
			}
			if (i == c->numwedges[side]) {
				break;
			}
		}
	}
	if ((c->numwedges[0] + c->numwedges[1]) * 2 - 1 != b->numnodes) {
		PrintOnce("CalculateCircle: internal error 1");
		hlassume(false, assume_first);
	}

	// fill in other information
	for (side = 0; side < 2; side++) {
		for (i = 0; i < c->numwedges[side]; i++) {
			bwedge_t* w = &c->wedges[side][i];
			bbrinknode_t* node = &b->nodes[w->nodenum];
			if (!node->clipnode->isleaf) {
				PrintOnce("CalculateCircle: internal error: not leaf");
				hlassume(false, assume_first);
			}
			w->content = node->content;
			w->prev = &c->surfaces[side][i];
			w->next = (i == c->numwedges[side] - 1)
				? &c->surfaces[!side][0]
				: &c->surfaces[side][i + 1];
			w->prev->next = w;
			w->next->prev = w;
		}
		for (i = 0; i < c->numwedges[side]; i++) {
			bsurface_t* s = &c->surfaces[side][i];
			bbrinknode_t* node = &b->nodes[s->nodenum];
			s->normal = vector_scale(
				node->plane->normal, s->nodeside ? -1.0 : 1.0
			);
		}
	}

	// check the normals
	for (side = 0; side < 2; side++) {
		for (i = 0; i < c->numwedges[side]; i++) {
			bwedge_t* w = &c->wedges[side][i];
			if (i == 0 && i == c->numwedges[side] - 1) // 180 degrees
			{
				continue;
			}
			double3_array v = cross_product(
				w->prev->normal, w->next->normal
			);
			if (!normalize_vector(v)
				|| dot_product(v, c->axis) < 1 - 0.01) {
				return false;
			}
		}
	}
	return true;
}

void PrintCircle(bcircle_t const * c) {
	Log("axis %f %f %f\n", c->axis[0], c->axis[1], c->axis[2]);
	Log("basenormal %f %f %f\n",
		c->basenormal[0],
		c->basenormal[1],
		c->basenormal[2]);
	Log("numwedges %d %d\n", c->numwedges[0], c->numwedges[1]);
	for (int side = 0; side < 2; side++) {
		for (int i = 0; i < c->numwedges[side]; i++) {
			bwedge_t const * w = &c->wedges[side][i];
			bsurface_t const * s = &c->surfaces[side][i];
			Log("surface[%d][%d] nodenum %d nodeside %d normal %f %f %f\n",
				side,
				i,
				s->nodenum,
				s->nodeside,
				s->normal[0],
				s->normal[1],
				s->normal[2]);
			Log("wedge[%d][%d] nodenum %d content %d\n",
				side,
				i,
				w->nodenum,
				std::to_underlying(w->content));
		}
	}
}

bool AddPartition(
	bclipnode_t* clipnode,
	int planenum,
	bool planeside,
	contents_t content,
	bbrinklevel_e brinktype
) {
	// make sure we won't do any harm
	btreeface_l::iterator fi;
	btreeedge_l::iterator ei;
	int side;
	if (!clipnode->isleaf) {
		return false;
	}
	bool onback = false;
	for (fi = clipnode->treeleaf->faces->begin();
		 fi != clipnode->treeleaf->faces->end();
		 fi++) {
		for (ei = fi->f->edges->begin(); ei != fi->f->edges->end(); ei++) {
			for (side = 0; side < 2; side++) {
				btreepoint_t* tp = GetPointFromEdge(ei->e, side);
				mapplane_t const * plane = &g_mapplanes[planenum];
				double dist = dot_product(tp->v, plane->normal)
					- plane->dist;
				if (planeside ? dist < -ON_EPSILON : dist > ON_EPSILON) {
					return false;
				}
				if (planeside ? dist > ON_EPSILON : dist < -ON_EPSILON) {
					onback = true;
				}
			}
		}
	}
	if (!onback) {
		return false; // the whole leaf is on the plane, or the leaf doesn't
					  // consist of any vertex
	}
	bpartition_t* p = new bpartition_t{};
	hlassume(p != nullptr, assume_NoMemory);
	p->next = clipnode->partitions;
	p->planenum = planenum;
	p->planeside = planeside;
	p->content = content;
	p->type = brinktype;
	clipnode->partitions = p;
	return true;
}

void AnalyzeBrinks(bbrinkinfo_t* info) {
	int countgood = 0;
	int countinvalid = 0;
	int countskipped = 0;
	int countfixed = 0;
	int i, j, side;
	for (i = 0; i < info->numbrinks; i++) {
		bbrink_t& b = *info->brinks[i];
		if (b.numnodes <= 5) // quickly reject the most trivial brinks
		{
			if (b.numnodes != 3 && b.numnodes != 5) {
				PrintOnce("AnalyzeBrinks: internal error 1");
				hlassume(false, assume_first);
			}
			// because a brink won't necessarily be split twice after its
			// creation
			if (b.numnodes == 3) {
				if (g_developer >= developer_level::fluff) {
					Developer(
						developer_level::fluff,
						"Brink wasn't split by the second plane:\n"
					);
					PrintBrink(b);
				}
				countinvalid++;
			} else {
				countgood++;
			}
			continue;
		}

		if (b.numnodes > 2 * MAXBRINKWEDGES - 1) {
			if (g_developer >= developer_level::megaspam) {
				Developer(
					developer_level::megaspam,
					"Skipping complicated brink:\n"
				);
				PrintBrink(b);
			}
			countskipped++;
			continue;
		}
		bcircle_t c;
		// build the circle to find out the planes a player may move along
		if (!CalculateCircle(&b, &c)) {
			if (g_developer >= developer_level::fluff) {
				Developer(
					developer_level::fluff,
					"CalculateCircle failed for brink:\n"
				);
				PrintBrink(b);
			}
			countinvalid++;
			continue;
		}

		int transitionfound[2];
		bsurface_t* transitionpos[2];
		bool transitionside[2];
		for (side = 0; side < 2; side++) {
			transitionfound[side] = 0;
			for (j = 1; j < c.numwedges[side];
				 j++) // we will later consider the surfaces on the first
					  // split
			{
				bsurface_t* s = &c.surfaces[side][j];
				if ((s->prev->content == contents_t::SOLID)
					!= (s->next->content == contents_t::SOLID)) {
					transitionfound[side]++;
					transitionpos[side] = s;
					transitionside[side]
						= (s->prev->content == contents_t::SOLID);
				}
			}
		}

		if (transitionfound[0] == 0 || transitionfound[1] == 0) {
			// at least one side of the first split is completely SOLID or
			// EMPTY. no bugs in this case
			countgood++;
			continue;
		}

		if (transitionfound[0] > 1 || transitionfound[1] > 1
			|| (c.surfaces[0][0].prev->content == contents_t::SOLID)
				!= (c.surfaces[0][0].next->content == contents_t::SOLID)
			|| (c.surfaces[1][0].prev->content == contents_t::SOLID)
				!= (c.surfaces[1][0].next->content == contents_t::SOLID)) {
			// there must at least 3 transition surfaces now, which is too
			// complicated. just leave it unfixed
			if (g_developer >= developer_level::megaspam) {
				Developer(
					developer_level::megaspam,
					"Skipping complicated brink:\n"
				);
				PrintBrink(b);
				PrintCircle(&c);
			}
			countskipped++;
			continue;
		}

		if (transitionside[1] != !transitionside[0]) {
			PrintOnce("AnalyzeBrinks: internal error 2");
			hlassume(false, assume_first);
		}
		bool bfix = false;
		bool berror = false;
		double3_array vup = { 0, 0, 1 };
		bool isfloor;
		bool onfloor;
		bool blocking;
		{
			isfloor = false;
			for (int side2 = 0; side2 < 2; side2++) {
				double3_array normal = vector_scale(
					transitionpos[side2]->normal,
					transitionside[side2] ? -1.0 : 1.0
				); // pointing from SOLID to EMPTY
				if (dot_product(normal, vup) > BRINK_FLOOR_THRESHOLD) {
					isfloor = true;
				}
			}
		}
		{
			onfloor = false;
			for (int side2 = 0; side2 < 2; side2++) {
				btreepoint_t* tp = GetPointFromEdge(b.edge, side2);
				if (tp->infinite) {
					continue;
				}
				for (btreeedge_l::iterator ei = tp->edges->begin();
					 ei != tp->edges->end();
					 ei++) {
					for (btreeface_l::iterator fi = ei->e->faces->begin();
						 fi != ei->e->faces->end();
						 fi++) {
						if (fi->f->infinite
							|| GetLeafFromFace(fi->f, false)->infinite
							|| GetLeafFromFace(fi->f, true)->infinite) {
							PrintOnce(
								"AnalyzeBrinks: internal error: an infinite object contains a finite object"
							);
							hlassume(false, assume_first);
						}
						for (int side3 = 0; side3 < 2; side3++) {
							double3_array normal = vector_scale(
								fi->f->plane->normal,
								(fi->f->planeside != (bool) side3) ? -1.0
																   : 1.0
							);
							if (dot_product(normal, vup)
									> BRINK_FLOOR_THRESHOLD
								&& GetLeafFromFace(fi->f, side3)
										->clipnode->content
									== contents_t::SOLID
								&& GetLeafFromFace(fi->f, !side3)
										->clipnode->content
									!= contents_t::SOLID) {
								onfloor = true;
							}
						}
					}
				}
			}
		}
		// this code does not fix all the bugs, it only aims to fix most of
		// the bugs
		for (side = 0; side < 2; side++) {
			bsurface_t* smovement = transitionpos[side];
			bsurface_t* s;
			for (s = transitionside[!side] ? &c.surfaces[!side][0]
										   : &c.surfaces[side][0];
				 ;
				 s = transitionside[!side] ? s->next->next
										   : s->prev->prev) {
				bwedge_t* w = transitionside[!side] ? s->next : s->prev;
				bsurface_t* snext = transitionside[!side] ? w->next
														  : w->prev;
				double dot = dot_product(
					cross_product(smovement->normal, snext->normal), c.axis
				);
				if (transitionside[!side] ? dot < 0.01 : dot > -0.01) {
					break;
				}
				if (w->content != contents_t::SOLID) {
					break;
				}
				if (snext
					== (transitionside[!side] ? &c.surfaces[side][0]
											  : &c.surfaces[!side][0])) {
					Developer(
						developer_level::error,
						"AnalyzeBrinks: surface past 0\n"
					);
					break;
				}
				bfix = true;
				{
					if (dot_product(smovement->normal, s->normal) > 0.01) {
						blocking = false;
					} else {
						blocking = true;
					}
				}
				bclipnode_t* clipnode = b.nodes[w->nodenum].clipnode;
				int planenum = b.nodes[smovement->nodenum].planenum;
				bool planeside = transitionside[!side]
					? smovement->nodeside
					: !smovement->nodeside;
				bbrinklevel_e brinktype;
				brinktype = isfloor
					? (blocking ? BrinkFloorBlocking : BrinkFloor)
					: onfloor ? (blocking ? BrinkWallBlocking : BrinkWall)
							  : BrinkAny;
				if (!AddPartition(
						clipnode,
						planenum,
						planeside,
						contents_t::EMPTY,
						brinktype
					)) {
					berror = true;
				}
			}
		}
		if (berror) {
			if (g_developer >= developer_level::fluff) {
				Developer(
					developer_level::fluff,
					"AddPartition failed for brink:\n"
				);
				PrintBrink(b);
			}
			countinvalid++;
		} else if (!bfix) {
			countgood++;
		} else {
			countfixed++;
		}
	}
	Developer(
		developer_level::message,
		"brinks: good = %d skipped = %d fixed = %d invalid = %d\n",
		countgood,
		countskipped,
		countfixed,
		countinvalid
	);
}

void DeleteClipnodes(bbrinkinfo_t* info) {
	for (int i = 0; i < info->numclipnodes; i++) {
		if (!info->clipnodes[i].isleaf) {
			continue;
		}
		bpartition_t* p;
		while ((p = info->clipnodes[i].partitions) != nullptr) {
			info->clipnodes[i].partitions = p->next;
			delete p;
		}
	}
	delete[] info->clipnodes;
}

void SortPartitions(bbrinkinfo_t* info
) // to merge same partition planes and compress clipnodes better if using
  // HLBSP_MERGECLIPNODE
{
	int countfloorblocking = 0;
	int countfloor = 0;
	int countwallblocking = 0;
	int countwall = 0;
	int countany = 0;
	for (int i = 0; i < info->numclipnodes; i++) {
		bclipnode_t* clipnode = &info->clipnodes[i];
		if (!clipnode->isleaf) {
			continue;
		}
		bpartition_t *current, **pp, *partitions;
		partitions = clipnode->partitions;
		clipnode->partitions = nullptr;
		while ((current = partitions) != nullptr) {
			partitions = current->next;
			if (current->content != contents_t::EMPTY) {
				PrintOnce(
					"SortPartitions: content of partition was not empty."
				);
				hlassume(false, assume_first);
			}
			for (pp = &clipnode->partitions; *pp; pp = &(*pp)->next) {
				if ((*pp)->planenum > current->planenum
					|| ((*pp)->planenum == current->planenum
						&& (*pp)->planeside >= current->planeside
					)) // normally the planeside should be identical
				{
					break;
				}
			}
			if (*pp && (*pp)->planenum == current->planenum
				&& (*pp)->planeside == current->planeside) {
				(*pp)->type = std::min(
					(*pp)->type, current->type
				); // pick the lowest (most important) level from the
				   // existing partition and the current partition
				delete current;
				continue;
			}
			switch (current->type) {
				case BrinkFloorBlocking:
					countfloorblocking++;
					break;
				case BrinkFloor:
					countfloor++;
					break;
				case BrinkWallBlocking:
					countwallblocking++;
					break;
				case BrinkWall:
					countwall++;
					break;
				case BrinkAny:
					countany++;
					break;
				default:
					PrintOnce("SortPartitions: internal error");
					hlassume(false, assume_first);
					break;
			}
			current->next = *pp;
			*pp = current;
		}
	}
	Developer(
		developer_level::message,
		"partitions: floorblocking = %d floor = %d wallblocking = %d wall = %d any = %d\n",
		countfloorblocking,
		countfloor,
		countwallblocking,
		countwall,
		countany
	);
}

bbrinkinfo_t* CreateBrinkinfo(dclipnode_t const * clipnodes, int headnode) {
	bbrinkinfo_t* info{};
	try {
		info = new bbrinkinfo_t{};
		ExpandClipnodes(info, clipnodes, headnode);
		BuildTreeCells(info);
		CollectBrinks(info);
		AnalyzeBrinks(info);
		FreeBrinks(info);
		DeleteTreeCells(info);
		SortPartitions(info);
	} catch (std::bad_alloc const &) {
		hlassume(false, assume_NoMemory);
	}
	return info;
}

extern int count_mergedclipnodes;
using clipnodemap_t = std::map<std::pair<int, std::pair<int, int>>, int>;

inline clipnodemap_t::key_type MakeKey(dclipnode_t const & c) {
	return std::make_pair(
		c.planenum, std::make_pair(c.children[0], c.children[1])
	);
}

bool FixBrinks_r_r(
	bclipnode_t const * clipnode,
	bpartition_t const * p,
	bbrinklevel_e level,
	int& headnode_out,
	dclipnode_t* begin,
	dclipnode_t* end,
	dclipnode_t*& current,
	clipnodemap_t* outputmap
) {
	while (p && p->type > level) {
		p = p->next;
	}
	if (p == nullptr) {
		headnode_out = (int) clipnode->content;
		return true;
	}
	dclipnode_t* cn;
	dclipnode_t tmpclipnode;
	cn = &tmpclipnode;
	dclipnode_t* c = current;
	current++;
	cn->planenum = p->planenum;
	cn->children[p->planeside] = (std::int16_t) p->content;
	int r;
	if (!FixBrinks_r_r(
			clipnode, p->next, level, r, begin, end, current, outputmap
		)) {
		return false;
	}
	cn->children[!p->planeside] = r;
	clipnodemap_t::iterator output;
	output = outputmap->find(MakeKey(*cn));
	if (g_noclipnodemerge || output == outputmap->end()) {
		if (c >= end) {
			return false;
		}
		*c = *cn;
		(*outputmap)[MakeKey(*cn)] = c - begin;
		headnode_out = c - begin;
	} else {
		count_mergedclipnodes++;
		if (current != c + 1) {
			Error("Merge clipnodes: internal error");
		}
		current = c;
		headnode_out = output->second; // use the existing clipnode
	}
	return true;
}

bool FixBrinks_r(
	bclipnode_t const * clipnode,
	bbrinklevel_e level,
	int& headnode_out,
	dclipnode_t* begin,
	dclipnode_t* end,
	dclipnode_t*& current,
	clipnodemap_t* outputmap
) {
	if (clipnode->isleaf) {
		return FixBrinks_r_r(
			clipnode,
			clipnode->partitions,
			level,
			headnode_out,
			begin,
			end,
			current,
			outputmap
		);
	} else {
		dclipnode_t* cn;
		dclipnode_t tmpclipnode;
		cn = &tmpclipnode;
		dclipnode_t* c = current;
		current++;
		cn->planenum = clipnode->planenum;
		for (int k = 0; k < 2; k++) {
			int r;
			if (!FixBrinks_r(
					clipnode->children[k],
					level,
					r,
					begin,
					end,
					current,
					outputmap
				)) {
				return false;
			}
			cn->children[k] = r;
		}
		clipnodemap_t::iterator output;
		output = outputmap->find(MakeKey(*cn));
		if (g_noclipnodemerge || output == outputmap->end()) {
			if (c >= end) {
				return false;
			}
			*c = *cn;
			(*outputmap)[MakeKey(*cn)] = c - begin;
			headnode_out = c - begin;
		} else {
			count_mergedclipnodes++;
			if (current != c + 1) {
				Error("Merge clipnodes: internal error");
			}
			current = c;
			headnode_out = output->second; // use existing clipnode
		}
		return true;
	}
}

bool FixBrinks(
	bbrinkinfo_t const * brinkinfo,
	bbrinklevel_e level,
	int& headnode_out,
	dclipnode_t* clipnodes_out,
	int maxsize,
	int size,
	int& size_out
) {
	bbrinkinfo_t const * info = (bbrinkinfo_t const *) brinkinfo;
	dclipnode_t* begin = clipnodes_out;
	dclipnode_t* end = &clipnodes_out[maxsize];
	dclipnode_t* current = &clipnodes_out[size];
	clipnodemap_t outputmap;
	int r;
	if (!FixBrinks_r(
			&info->clipnodes[0], level, r, begin, end, current, &outputmap
		)) {
		return false;
	}
	headnode_out = r;
	size_out = current - begin;
	return true;
}

void DeleteBrinkinfo(bbrinkinfo_t* brinkinfo) {
	DeleteClipnodes(brinkinfo);
	delete brinkinfo;
}
