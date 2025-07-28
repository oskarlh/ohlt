/*
 *  Progressive Mesh type Polygon Reduction Algorithm
 *  by Stan Melax (c) 1998
 *  Permission to use any of this code wherever you want is granted..
 *  Although, please do acknowledge authorship if appropriate.
 *
 *  See the header file progmesh.h for a description of this module
 */

#include "hlrad.h"
#include "meshdesc.h"

#include <vector>

/*
 *  For the polygon reduction algorithm we use data structures
 *  that contain a little bit more information than the usual
 *  indexed face set type of data structure.
 *  From a vertex we wish to be able to quickly get the
 *  neighboring faces and vertices.
 */

class CTriangle;
class CVertex;

class CTriangle final {
  public:
	CVertex* vertex[3];	   // the 3 points that make this tri
	float3_array normal{}; // unit vector othogonal to this face

	CTriangle(CVertex* v0, CVertex* v1, CVertex* v2);
	~CTriangle();

	void ComputeNormal();
	void ReplaceVertex(CVertex* vold, CVertex* vnew);
	int HasVertex(CVertex* v);
};

class CVertex final {
  public:
	float3_array position{};	  // location of point in euclidean space
	int id;						  // place of vertex in original list
	List<CVertex*> neighbor;	  // adjacent vertices
	std::vector<CTriangle*> face; // adjacent triangles
	float objdist;				  // cached cost of collapsing edge
	CVertex* collapse;			  // candidate vertex for collapse

	CVertex(float3_array const & v, int _id);
	~CVertex();
	void RemoveIfNonNeighbor(CVertex* n);
};

List<CVertex*> vertices;
List<CTriangle*> triangles;

CTriangle ::CTriangle(CVertex* v0, CVertex* v1, CVertex* v2) {
	assert(v0 != v1 && v1 != v2 && v2 != v0);

	vertex[0] = v0;
	vertex[1] = v1;
	vertex[2] = v2;

	ComputeNormal();

	triangles.Add(this);

	for (int i = 0; i < 3; i++) {
		vertex[i]->face.emplace_back(this);

		for (int j = 0; j < 3; j++) {
			if (i == j) {
				continue;
			}

			vertex[i]->neighbor.AddUnique(vertex[j]);
		}
	}
}

CTriangle ::~CTriangle() {
	triangles.Remove(this);

	for (int i1 = 0; i1 < 3; i1++) {
		if (vertex[i1]) {
			vertex[i1]->face.erase(std::ranges::find(vertex[i1]->face, this)
			);
		}
	}

	for (int i1 = 0; i1 < 3; i1++) {
		int i2 = (i1 + 1) % 3;

		if (!vertex[i1] || !vertex[i2]) {
			continue;
		}

		vertex[i1]->RemoveIfNonNeighbor(vertex[i2]);
		vertex[i2]->RemoveIfNonNeighbor(vertex[i1]);
	}
}

int CTriangle ::HasVertex(CVertex* v) {
	return v == vertex[0] || v == vertex[1] || v == vertex[2];
}

void CTriangle ::ComputeNormal(void) {
	float3_array v0 = vertex[0]->position;
	float3_array v1 = vertex[1]->position;
	float3_array v2 = vertex[2]->position;

	float3_array a = vector_subtract(v1, v0);
	float3_array b = vector_subtract(v2, v1);
	normal = cross_product(a, b);

	if (vector_length(normal) == 0.0f) {
		return;
	}

	float3_array n{ normal[0], normal[1], normal[2] };
	normalize_vector(n);
	normal[0] = n[0];
	normal[1] = n[1];
	normal[2] = n[2];
}

void CTriangle ::ReplaceVertex(CVertex* vold, CVertex* vnew) {
	assert(vold && vnew);
	assert(vold == vertex[0] || vold == vertex[1] || vold == vertex[2]);
	assert(vnew != vertex[0] && vnew != vertex[1] && vnew != vertex[2]);

	if (vold == vertex[0]) {
		vertex[0] = vnew;
	} else if (vold == vertex[1]) {
		vertex[1] = vnew;
	} else {
		assert(vold == vertex[2]);
		vertex[2] = vnew;
	}

	vold->face.erase(std::ranges::find(vold->face, this));
	assert(!std::ranges::contains(vnew->face, this));
	vnew->face.emplace_back(this);

	for (int i = 0; i < 3; i++) {
		vold->RemoveIfNonNeighbor(vertex[i]);
		vertex[i]->RemoveIfNonNeighbor(vold);
	}

	for (int i = 0; i < 3; i++) {
		assert(std::ranges::count(vertex[i]->face, this) == 1);
		for (int j = 0; j < 3; j++) {
			if (i == j) {
				continue;
			}
			vertex[i]->neighbor.AddUnique(vertex[j]);
		}
	}

	ComputeNormal();
}

CVertex ::CVertex(float3_array const & v, int _id) {
	position = v;
	id = _id;

	vertices.Add(this);
}

CVertex ::~CVertex() {
	assert(face.empty());

	while (neighbor.Size()) {
		neighbor[0]->neighbor.Remove(this);
		neighbor.Remove(neighbor[0]);
	}

	vertices.Remove(this);
}

void CVertex ::RemoveIfNonNeighbor(CVertex* n) {
	// removes n from neighbor list if n isn't a neighbor.
	if (!neighbor.Contains(n)) {
		return;
	}

	if (std::ranges::none_of(face, [n](CTriangle* tri) {
			return tri->HasVertex(n);
		})) {
		neighbor.Remove(n);
	}
}

static float ComputeEdgeCollapseCost(CVertex* u, CVertex* v) {
	// if we collapse edge uv by moving u to v then how
	// much different will the model change, i.e. how much "error".
	// Texture, vertex normal, and border vertex code was removed
	// to keep this demo as simple as possible.
	// The method of determining cost was designed in order
	// to exploit small and coplanar regions for
	// effective polygon reduction.
	// Is is possible to add some checks here to see if "folds"
	// would be generated.  i.e. normal of a remaining face gets
	// flipped.  I never seemed to run into this problem and
	// therefore never added code to detect this case.

	float const edgelength = distance_between_points(
		v->position, u->position
	);

	int i;

	// find the "sides" triangles that are on the edge uv
	List<CTriangle*> sides;

	for (i = 0; i < u->face.size(); i++) {
		if (u->face[i]->HasVertex(v)) {
			sides.Add(u->face[i]);
		}
	}

	float curvature = 0.0f;
	// use the triangle facing most away from the sides
	// to determine our curvature term
	for (i = 0; i < u->face.size(); i++) {
		float mincurv = 1.0f; // curve for face i and closer side to it

		for (int j = 0; j < sides.Size(); j++) {
			float dotprod = dot_product(
				u->face[i]->normal, sides[j]->normal
			);
			mincurv = std::min(mincurv, (1.0f - dotprod) / 2.0f);
		}

		curvature = std::max(curvature, mincurv);
	}

	// the more coplanar the lower the curvature term
	return edgelength * curvature;
}

static void ComputeEdgeCostAtVertex(CVertex* v) {
	// compute the edge collapse cost for all edges that start
	// from vertex v.  Since we are only interested in reducing
	// the object by selecting the min cost edge at each step, we
	// only cache the cost of the least cost edge at this vertex
	// (in member variable collapse) as well as the value of the
	// cost (in member variable objdist).

	if (v->neighbor.Size() == 0) {
		// v doesn't have neighbors so it costs nothing to collapse
		v->collapse = nullptr;
		v->objdist = -0.01f;
		return;
	}

	v->objdist = 1000000.0f;
	v->collapse = nullptr;

	// search all neighboring edges for "least cost" edge
	for (int i = 0; i < v->neighbor.Size(); i++) {
		float dist;
		dist = ComputeEdgeCollapseCost(v, v->neighbor[i]);

		if (dist < v->objdist) {
			v->collapse = v->neighbor[i]; // candidate for edge collapse
			v->objdist = dist;			  // cost of the collapse
		}
	}
}

static void ComputeAllEdgeCollapseCosts(void) {
	// For all the edges, compute the difference it would make
	// to the model if it was collapsed.  The least of these
	// per vertex is cached in each vertex object.
	for (int i = 0; i < vertices.Size(); i++) {
		ComputeEdgeCostAtVertex(vertices[i]);
	}
}

static void Collapse(CVertex* u, CVertex* v) {
	// Collapse the edge uv by moving vertex u onto v
	// Actually remove tris on uv, then update tris that
	// have u to have v, and then remove u.
	if (!v) {
		// u is a vertex all by itself so just delete it
		delete u;
		return;
	}

	List<CVertex*> tmp;
	// make tmp a list of all the neighbors of u
	for (int i = 0; i < u->neighbor.Size(); i++) {
		tmp.Add(u->neighbor[i]);
	}

	// delete triangles on edge uv:
	for (int i = u->face.size() - 1; i >= 0; i--) {
		if (u->face[i]->HasVertex(v)) {
			delete (u->face[i]);
		}
	}

	// update remaining triangles to have v instead of u
	for (int i = u->face.size() - 1; i >= 0; i--) {
		u->face[i]->ReplaceVertex(u, v);
	}

	delete u;

	// recompute the edge collapse costs for neighboring vertices
	for (int i = 0; i < tmp.Size(); i++) {
		ComputeEdgeCostAtVertex(tmp[i]);
	}
}

static void AddVertex(std::span<float3_array const> vert) {
	for (int i = 0; i < vert.size(); i++) {
		CVertex* v = new CVertex(vert[i], i);
	}
}

static void AddFaces(std::span<triset const> tri) {
	for (int i = 0; i < tri.size(); i++) {
		CTriangle* t = new CTriangle(
			vertices[tri[i].v[0]],
			vertices[tri[i].v[1]],
			vertices[tri[i].v[2]]
		);
	}
}

static CVertex* MinimumCostEdge(void) {
	// Find the edge that when collapsed will affect model the least.
	// This funtion actually returns a CVertex, the second vertex
	// of the edge (collapse candidate) is stored in the vertex data.
	// Serious optimization opportunity here: this function currently
	// does a sequential search through an unsorted list :-(
	// Our algorithm could be O(n*lg(n)) instead of O(n*n)
	CVertex* mn = vertices[0];

	for (int i = 0; i < vertices.Size(); i++) {
		if (vertices[i]->objdist < mn->objdist) {
			mn = vertices[i];
		}
	}

	return mn;
}

void ProgressiveMesh(
	std::span<float3_array const> vert,
	std::span<triset const> tri,
	std::vector<int>& map,
	std::vector<std::size_t>& permutation
) {
	AddVertex(vert); // put input data into our data structures
	AddFaces(tri);

	ComputeAllEdgeCollapseCosts();		 // cache all edge collapse costs
	permutation.resize(vertices.Size()); // allocate space
	map.resize(vertices.Size());		 // allocate space

	// reduce the object down to nothing:
	while (vertices.Size() > 0) {
		// get the next vertex to collapse
		CVertex* mn = MinimumCostEdge();
		// keep track of this vertex, i.e. the collapse ordering
		permutation[mn->id] = vertices.Size() - 1;
		// keep track of vertex to which we collapse to
		map[vertices.Size() - 1] = (mn->collapse) ? mn->collapse->id : -1;
		// Collapse this edge
		Collapse(mn, mn->collapse);
	}

	// reorder the map list based on the collapse ordering
	for (int i = 0; i < map.size(); i++) {
		map[i] = (map[i] == -1) ? 0 : permutation[map[i]];
	}

	// The caller of this function should reorder their vertices
	// according to the returned "permutation".
}

void PermuteVertices(
	std::span<std::size_t const> permutation,
	std::span<float3_array> vert,
	std::span<triset> tris
) {
	assert(permutation.size() == vert.size());

	// rearrange the vertex list
	std::vector<float3_array> temp_list{ vert.begin(), vert.end() };

	for (std::size_t i = 0; i < vert.size(); i++) {
		vert[permutation[i]] = temp_list[i];
	}

	// update the changes in the entries in the triangle list
	for (std::size_t i = 0; i < tris.size(); i++) {
		for (int j = 0; j < 3; j++) {
			tris[i].v[j] = permutation[tris[i].v[j]];
		}
	}
}

int MapVertex(int a, int mx, std::span<int const> map) {
	if (mx <= 0) {
		return 0;
	}

	while (a >= mx) {
		a = map[a];
	}

	return a;
}
