#pragma once
// Cached mesh for tracing custom objects

#include "list.h" // simple container
#include "planes.h"
#include "studio.h"

#include <memory>

#define AREA_NODES 32
#define AREA_DEPTH 4

#define MAX_FACET_PLANES 32
#define MAX_PLANES		 524'288 // unsigned short limit
#define PLANE_HASHES	 (MAX_PLANES >> 2)

// Compute methods
enum class trace_method : std::uint8_t {
	shadow_fast,
	shadow_normal,
	shadow_slow
};

using word = unsigned short;
using uint = unsigned int;
using vec4_t = std::array<float, 4>; // x,y,z,w
using matrix3x4 = std::array<std::array<float, 4>, 3>;

#define Q_rint(x) ((x) < 0 ? ((int) ((x) - 0.5f)) : ((int) ((x) + 0.5f)))

struct mplane_t final {
	float3_array normal;
	float dist;
	byte type;	   // for fast side tests
	byte signbits; // signx + (signy<<1) + (signz<<1)
	byte pad[2];
};

struct hashplane_t final {
	mplane_t pl;
	std::uint32_t planePoolIndex;
};

struct link_t final {
	link_t* prev;
	link_t* next;
};

struct areanode_t final {
	int axis; // -1 = leaf node
	float dist;
	areanode_t* children[2];
	link_t facets;
};

struct mvert_t final {
	float3_array point;
	float st[2]; // for alpha-texture test
};

struct mfacet_t final {
	link_t area;			   // linked to a division node or leaf
	mstudiotexture_t* texture; // valid for alpha-testing surfaces
	mvert_t triangle[3];	   // store triangle points
	float3_array mins, maxs;   // an individual size of each facet
	float3_array edge1, edge2; // new trace stuff
	byte numplanes; // because numplanes for each facet can't exceeds
					// MAX_FACET_PLANES!
	uint* indices; // a indexes into mesh plane pool
};

struct mmesh_t final {
	mfacet_t* facets;
	mplane_t* planes; // Shared plane pool
	float3_array mins;
	float3_array maxs;
	std::uint32_t numfacets;
	std::uint32_t numplanes;
	trace_method trace_mode; // Trace method
};

class triset final {
  public:
	int v[3]; // indices to vertex list
};

struct model_t; // Forward declaration

class CMeshDesc final {
  private:
	mmesh_t m_mesh{};
	char const * m_debugName{ nullptr }; // just for debug purpoces
	std::array<areanode_t, AREA_NODES>
		areanodes; // AABB tree for speedup trace test
	int numareanodes;
	bool has_tree;		 // build AABB tree
	int m_iTotalPlanes;	 // just for stats
	int m_iNumTris{ 0 }; // if > 0 we are in build mode
	size_t mesh_size;	 // mesh total size

	// used only while mesh is constructed
	std::unique_ptr<mfacet_t[]> facets;
	std::unique_ptr<std::uint32_t[]> planehash;
	std::unique_ptr<hashplane_t[]> planepool;

  public:
	CMeshDesc();
	CMeshDesc(CMeshDesc&&) = default;
	~CMeshDesc();

	void clear();

	// mesh construction
	bool InitMeshBuild(char const * debug_name, int numTriangles);
	bool AddMeshTriangle(
		mvert_t const triangle[3], mstudiotexture_t* tex = nullptr
	);
	bool FinishMeshBuild();
	void FreeMeshBuild();
	void FreeMesh();

	// local mathlib
	void AngleMatrix(
		float3_array const & angles,
		float3_array const & origin,
		float3_array const & scale,
		matrix3x4& matrix
	);
	void ConcatTransforms(
		matrix3x4 const & in1, matrix3x4 const & in2, matrix3x4& out
	);
	void QuaternionMatrix(
		vec4_t const & quat, float3_array const & origin, matrix3x4& matrix
	);
	void VectorTransform(
		float3_array const & in1, matrix3x4 const & in2, float3_array& out
	);
	void AngleQuaternion(float3_array const & angles, vec4_t& quat);

	// studio models processing
	void StudioCalcBoneQuaterion(
		mstudiobone_t* pbone, mstudioanim_t* panim, vec4_t& q
	);
	void StudioCalcBonePosition(
		mstudiobone_t* pbone, mstudioanim_t* panim, float3_array& pos
	);
	bool StudioConstructMesh(model_t* pModel);

	// linked list operations
	void InsertLinkBefore(link_t* l, link_t* before);
	void RemoveLink(link_t* l);
	void ClearLink(link_t* l);

	// AABB tree contsruction
	areanode_t* CreateAreaNode(
		int depth, float3_array const & mins, float3_array const & maxs
	);
	void RelinkFacet(mfacet_t* facet);

	inline areanode_t* GetHeadNode(void) {
		return (has_tree) ? &areanodes[0] : nullptr;
	}

	// plane cache
	uint AddPlaneToPool(mplane_t const * pl);
	bool PlaneFromPoints(mvert_t const triangle[3], mplane_t* plane);
	bool ComparePlanes(
		mplane_t const * plane, float3_array const & normal, float dist
	);
	bool PlaneEqual(mplane_t const * p0, mplane_t const * p1);
	void CategorizePlane(mplane_t* plane);
	void SnapPlaneToGrid(mplane_t* plane);
	void SnapVectorToGrid(float3_array& normal);

	// check for cache
	inline mmesh_t* GetMesh() {
		return &m_mesh;
	}

	void ClearBounds(float3_array& mins, float3_array& maxs) {
		// make bogus range
		mins[0] = mins[1] = mins[2] = 999999.0f;
		maxs[0] = maxs[1] = maxs[2] = -999999.0f;
	}

	void AddPointToBounds(
		float3_array const & v, float3_array& mins, float3_array& maxs
	) {
		for (int i = 0; i < 3; i++) {
			if (v[i] < mins[i]) {
				mins[i] = v[i];
			}
			if (v[i] > maxs[i]) {
				maxs[i] = v[i];
			}
		}
	}

	bool Intersect(
		float3_array const & trace_mins, float3_array const & trace_maxs
	) {
		if (m_mesh.mins[0] > trace_maxs[0] || m_mesh.mins[1] > trace_maxs[1]
			|| m_mesh.mins[2] > trace_maxs[2]) {
			return false;
		}
		if (m_mesh.maxs[0] < trace_mins[0] || m_mesh.maxs[1] < trace_mins[1]
			|| m_mesh.maxs[2] < trace_mins[2]) {
			return false;
		}
		return true;
	}
};

// simplification
void ProgressiveMesh(
	List<float3_array>& vert,
	List<triset>& tri,
	List<int>& map,
	List<int>& permutation
);
void PermuteVertices(
	List<int>& permutation, List<float3_array>& vert, List<triset>& tris
);
int MapVertex(int a, int mx, List<int>& map);

// collision description
struct model_t final {
	char name[64]; // model name
	float3_array origin;
	float3_array angles;
	float3_array scale; // scale X-Form
	int body;			// sets by level-designer
	int skin;			// e.g. various alpha-textures
	trace_method trace_mode;

	std::byte* extradata; // model
	void* anims;		  // studio animations

	CMeshDesc mesh; // cform
};
