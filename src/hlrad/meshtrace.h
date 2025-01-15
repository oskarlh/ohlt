#pragma once

/*
trace.h - trace triangle meshes
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

#include "meshdesc.h"

#define FRAC_EPSILON		(1.0f / 32.0f)
#define BARY_EPSILON		0.01f
#define ASLF_EPSILON		0.0001f
#define COPLANAR_EPSILON		0.25f
#define NEAR_SHADOW_EPSILON		1.5f
#define SELF_SHADOW_EPSILON		0.5f

#define STRUCT_FROM_LINK( l, t, m )	((t *)((byte *)l - (int)(long int)&(((t *)0)->m)))
#define FACET_FROM_AREA( l )		STRUCT_FROM_LINK( l, mfacet_t, area )
#define bound( min, num, max )	((num) >= (min) ? ((num) < (max) ? (num) : (max)) : (min))

class TraceMesh
{
private:
	float3_array		m_vecStart, m_vecEnd;
	float3_array		m_vecStartMins, m_vecEndMins;
	float3_array		m_vecStartMaxs, m_vecEndMaxs;
	float3_array		m_vecAbsMins, m_vecAbsMaxs;
	float3_array		m_vecTraceDirection;// ray direction
	float		m_flTraceDistance;
	bool		m_bHitTriangle;	// now we hit triangle
	areanode_t	*areanodes;	// AABB for static meshes
	mmesh_t		*mesh;		// mesh to trace
	int		checkcount;	// debug
	void		*m_extradata;	// pointer to model extradata

	inline void ClearBounds( float3_array& mins, float3_array& maxs )
	{
		// make bogus range
		mins[0] = mins[1] = mins[2] =  999999.0f;
		maxs[0] = maxs[1] = maxs[2] = -999999.0f;
	}

	inline void AddPointToBounds( const float3_array& v, float3_array& mins, float3_array& maxs )
	{
		for( int i = 0; i < 3; i++ )
		{
			if( v[i] < mins[i] ) mins[i] = v[i];
			if( v[i] > maxs[i] ) maxs[i] = v[i];
		}
	}

	inline bool BoundsIntersect( const float3_array& mins1, const float3_array& maxs1, const float3_array& mins2, const float3_array& maxs2 )
	{
		if( mins1[0] > maxs2[0] || mins1[1] > maxs2[1] || mins1[2] > maxs2[2] )
			return false;
		if( maxs1[0] < mins2[0] || maxs1[1] < mins2[1] || maxs1[2] < mins2[2] )
			return false;
		return true;
	}
public:
	TraceMesh() { mesh = nullptr; }
	~TraceMesh() {}

	// trace stuff
	void SetTraceMesh( mmesh_t *cached_mesh, areanode_t *tree ) { mesh = cached_mesh; areanodes = tree; }
	void SetupTrace( const float3_array& start, const float3_array& mins, const float3_array& maxs, const float3_array& end ); 
	void SetTraceModExtradata( void *data ) { m_extradata = data; }
	bool ClipRayToBox( const float3_array& mins, const float3_array& maxs );
	bool ClipRayToTriangle( const mfacet_t *facet );	// obsolete
	bool ClipRayToFacet( const mfacet_t *facet );
	bool ClipRayToFace( const mfacet_t *facet ); // ripped out from q3map2
	void ClipToLinks( areanode_t *node );
	bool DoTrace( void );
};
