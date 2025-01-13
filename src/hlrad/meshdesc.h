#pragma once

/*
meshdesc.h - cached mesh for tracing custom objects
Copyright (C) 2012 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "studio.h"
#include "list.h"		// simple container

#define AREA_NODES		32
#define AREA_DEPTH		4

#define MAX_FACET_PLANES	32
#define MAX_PLANES		524288		// unsigned short limit
#define PLANE_HASHES	(MAX_PLANES>>2)

#define PLANE_NORMAL_EPSILON	0.00001f
#define PLANE_DIST_EPSILON	0.04f

// Compute methods
enum class trace_method : std::uint8_t {
	shadow_fast,
	shadow_normal,
	shadow_slow
};

#ifndef M_PI
#define M_PI		3.14159265358979323846	// matches value in gcc v2 math.h
#endif

typedef unsigned short	word;
typedef unsigned int	uint;
using vec4_t = std::array<vec_t, 4>;	// x,y,z,w
typedef std::array<std::array<vec_t, 4>, 3> matrix3x4;

#define Q_rint( x )		((x) < 0 ? ((int)((x)-0.5f)) : ((int)((x)+0.5f)))


typedef struct mplane_s
{
	vec3_array		normal;
	float		dist;
	byte		type;		// for fast side tests
	byte		signbits;		// signx + (signy<<1) + (signz<<1)
	byte		pad[2];
} mplane_t;

typedef struct hashplane_s
{
	mplane_t		pl;
	struct hashplane_s	*hash;
} hashplane_t;

typedef struct link_s
{
	struct link_s	*prev, *next;
} link_t;

typedef struct areanode_s
{
	int		axis;		// -1 = leaf node
	float		dist;
	struct areanode_s	*children[2];
	link_t		facets;
} areanode_t;

typedef struct mvert_s
{
	vec3_array		point;
	float		st[2];		// for alpha-texture test
} mvert_t;

typedef struct
{
	link_t		area;		// linked to a division node or leaf
	mstudiotexture_t	*texture;		// valid for alpha-testing surfaces
	mvert_t		triangle[3];	// store triangle points
	vec3_array		mins, maxs;	// an individual size of each facet
	vec3_array		edge1, edge2;	// new trace stuff
	byte		numplanes;	// because numplanes for each facet can't exceeds MAX_FACET_PLANES!
	uint		*indices;		// a indexes into mesh plane pool
} mfacet_t;

struct mmesh_t {
	mfacet_t *facets;
	mplane_t *planes; // Shared plane pool
	vec3_array mins;
	vec3_array maxs;
	std::uint32_t numfacets;
	std::uint32_t numplanes;
	trace_method trace_mode; // Trace method
};

class triset
{
public:
	int v[3];		// indices to vertex list
};

struct model_t; // Forward declaration

class CMeshDesc
{
private:
	using hashplane_t_pointer = hashplane_t*;

	mmesh_t		m_mesh;
	const char	*m_debugName;		// just for debug purpoces
	std::array<areanode_t, AREA_NODES> areanodes;	// AABB tree for speedup trace test
	int		numareanodes;
	bool		has_tree;			// build AABB tree
	int		m_iTotalPlanes;		// just for stats
	int		m_iNumTris;		// if > 0 we are in build mode
	size_t		mesh_size;		// mesh total size

	// used only while mesh is contsructed
	std::unique_ptr<mfacet_t[]> facets;
	std::unique_ptr<hashplane_t_pointer[]> planehash;
	std::unique_ptr<hashplane_t[]>	planepool;
public:
	CMeshDesc();
	~CMeshDesc();

	// mesh construction
	bool InitMeshBuild( const char *debug_name, int numTrinagles ); 
	bool AddMeshTrinagle( const mvert_t triangle[3], mstudiotexture_t *tex = nullptr );
	bool FinishMeshBuild();
	void FreeMeshBuild();
	void FreeMesh( );

	// local mathlib
	void AngleMatrix(const vec3_array& angles, const vec3_array& origin, const vec3_array& scale, matrix3x4& matrix);
	void ConcatTransforms(const matrix3x4& in1, const matrix3x4& in2, matrix3x4& out);
	void QuaternionMatrix(const vec4_t& quat, const vec3_array& origin, matrix3x4& matrix);
	void VectorTransform(const vec3_array& in1, const matrix3x4& in2, vec3_array& out);
	void AngleQuaternion(const vec3_array& angles, vec4_t& quat);

	// studio models processing
	void StudioCalcBoneQuaterion( mstudiobone_t *pbone, mstudioanim_t *panim, vec4_t& q );
	void StudioCalcBonePosition( mstudiobone_t *pbone, mstudioanim_t *panim, vec3_t pos );
	bool StudioConstructMesh( model_t *pModel );

	// linked list operations
	void InsertLinkBefore( link_t *l, link_t *before );
	void RemoveLink( link_t *l );
	void ClearLink( link_t *l );

	// AABB tree contsruction
	areanode_t *CreateAreaNode(int depth, const vec3_array& mins, const vec3_array& maxs);
	void RelinkFacet( mfacet_t *facet );
	inline areanode_t *GetHeadNode( void ) { return (has_tree) ? &areanodes[0] : nullptr; }

	// plane cache
	uint AddPlaneToPool( const mplane_t *pl );
	bool PlaneFromPoints( const mvert_t triangle[3], mplane_t *plane );
	bool ComparePlanes( const mplane_t *plane, const vec3_t normal, float dist );
	bool PlaneEqual( const mplane_t *p0, const mplane_t *p1 );
	void CategorizePlane( mplane_t *plane );
	void SnapPlaneToGrid( mplane_t *plane );
	void SnapVectorToGrid( vec3_t normal );

	// check for cache
	inline mmesh_t *GetMesh() { return &m_mesh; } 

	void ClearBounds(vec3_array& mins, vec3_array& maxs ) {
		// make bogus range
		mins[0] = mins[1] = mins[2] =  999999.0f;
		maxs[0] = maxs[1] = maxs[2] = -999999.0f;
	}

	void AddPointToBounds( const vec3_array& v, vec3_array& mins, vec3_array& maxs )
	{
		for( int i = 0; i < 3; i++ )
		{
			if( v[i] < mins[i] ) mins[i] = v[i];
			if( v[i] > maxs[i] ) maxs[i] = v[i];
		}
	}

	bool Intersect( const vec3_t trace_mins, const vec3_t trace_maxs )
	{
		if( m_mesh.mins[0] > trace_maxs[0] || m_mesh.mins[1] > trace_maxs[1] || m_mesh.mins[2] > trace_maxs[2] )
			return false;
		if( m_mesh.maxs[0] < trace_mins[0] || m_mesh.maxs[1] < trace_mins[1] || m_mesh.maxs[2] < trace_mins[2] )
			return false;
		return true;
	}
};

// simplification
void ProgressiveMesh( List<float3_array> &vert, List<triset> &tri, List<int> &map, List<int> &permutation );
void PermuteVertices( List<int> &permutation, List<float3_array> &vert, List<triset> &tris );
int MapVertex( int a, int mx, List<int> &map );

// collision description
struct model_t {
	char name[64];		// model name
	vec3_array origin;
	vec3_array angles;
	vec3_array scale;		// scale X-Form
	int body;		// sets by level-designer
	int skin;		// e.g. various alpha-textures
	trace_method trace_mode;
    
	void *extradata;	// model
	void *anims;		// studio animations

	CMeshDesc mesh;		// cform
};
