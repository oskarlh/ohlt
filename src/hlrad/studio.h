#pragma once

#include "mathtypes.h"

#include <cstdint>

/***
 *
 *	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
 *
 *	This product contains software technology licensed from Id
 *	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software,
 *Inc. All Rights Reserved.
 *
 *   Use, distribution, and modification of this source code and/or
 *resulting object code is restricted to non-commercial enhancements to
 *products from Valve LLC.  All other use, distribution, or modification is
 *prohibited without written permission from Valve LLC.
 *
 ****/

/*
==============================================================================

STUDIO MODELS

Studio models are position independent, so the cache manager can move them.
==============================================================================
*/

// header
#define STUDIO_VERSION 10
#define IDSTUDIOHEADER \
	(('T' << 24) + ('S' << 16) + ('D' << 8) + 'I') // little-endian "IDST"
#define IDSEQGRPHEADER \
	(('Q' << 24) + ('S' << 16) + ('D' << 8) + 'I') // little-endian "IDSQ"

// studio limits
#define MAXSTUDIOTRIANGLES 65535 // max triangles per model
#define MAXSTUDIOVERTS	   32768 // max vertices per submodel
#define MAXSTUDIOSEQUENCES 256	 // total animation sequences
#define MAXSTUDIOSKINS	   128	 // total textures
#define MAXSTUDIOSRCBONES  512	 // bones allowed at source movement
#define MAXSTUDIOBONES	   128	 // total bones actually used
#define MAXSTUDIOMODELS	   32	 // sub-models per model
#define MAXSTUDIOBODYPARTS 32	 // body parts per submodel
#define MAXSTUDIOGROUPS \
	16 // sequence groups (e.g. barney01.mdl, barney02.mdl, e.t.c)
#define MAXSTUDIOANIMATIONS	 512  // max frames per sequence
#define MAXSTUDIOMESHES		 256  // max textures per model
#define MAXSTUDIOEVENTS		 1024 // events per model
#define MAXSTUDIOPIVOTS		 256  // pivot points
#define MAXSTUDIOBLENDS		 16	  // max anim blends
#define MAXSTUDIOCONTROLLERS 8	  // max controllers per model
#define MAXSTUDIOATTACHMENTS 4	  // max attachments per model

// client-side model flags
#define STUDIO_ROCKET  0x0001 // leave a trail
#define STUDIO_GRENADE 0x0002 // leave a trail
#define STUDIO_GIB	   0x0004 // leave a trail
#define STUDIO_ROTATE  0x0008 // rotate (bonus items)
#define STUDIO_TRACER  0x0010 // green split trail
#define STUDIO_ZOMGIB  0x0020 // small blood trail
#define STUDIO_TRACER2 0x0040 // orange split trail + rotate
#define STUDIO_TRACER3 0x0080 // purple trail
#define STUDIO_DYNAMIC_LIGHT \
	0x0100 // dynamically get lighting from floor or ceil (flying monsters)
#define STUDIO_TRACE_HITBOX \
	0x0200 // always use hitbox trace instead of bbox

#define STUDIO_HAS_BUMP (1 << 16) // loadtime set

// lighting & rendermode options
#define STUDIO_NF_FLATSHADE	  0x0001
#define STUDIO_NF_CHROME	  0x0002
#define STUDIO_NF_FULLBRIGHT  0x0004
#define STUDIO_NF_COLORMAP	  0x0008 // can changed by colormap command
#define STUDIO_NF_NOSMOOTH	  0x0010 // rendering as semiblended
#define STUDIO_NF_ADDITIVE	  0x0020 // rendering with additive mode
#define STUDIO_NF_TRANSPARENT 0x0040 // use texture with alpha channel
#define STUDIO_NF_NORMALMAP	  0x0080 // indexed normalmap
#define STUDIO_NF_GLOSSMAP	  0x0100 // glossmap
#define STUDIO_NF_GLOSSPOWER  0x0200
#define STUDIO_NF_LUMA		  0x0400 // self-illuminate parts
#define STUDIO_NF_ALPHASOLID \
	0x0800 // use with STUDIO_NF_TRANSPARENT to have solid alphatest
		   // surfaces for env_static
#define STUDIO_NF_TWOSIDE 0x1000 // render mesh as twosided

#define STUDIO_NF_NODRAW (1 << 16) // failed to create shader for this mesh
#define STUDIO_NF_NODLIGHT \
	(1 << 17) // failed to create dlight shader for this mesh
#define STUDIO_NF_NOSUNLIGHT \
	(1 << 18) // failed to create sun light shader for this mesh
#define STUDIO_DXT5_NORMALMAP (1 << 19) // it's DXT5nm texture
#define STUDIO_NF_HAS_ALPHA	  (1 << 20) // external texture has alpha-channel
#define STUDIO_NF_UV_COORDS \
	(1 << 31) // using half-float coords instead of ST

// motion flags
#define STUDIO_X	 0x0001
#define STUDIO_Y	 0x0002
#define STUDIO_Z	 0x0004
#define STUDIO_XR	 0x0008
#define STUDIO_YR	 0x0010
#define STUDIO_ZR	 0x0020
#define STUDIO_LX	 0x0040
#define STUDIO_LY	 0x0080
#define STUDIO_LZ	 0x0100
#define STUDIO_AX	 0x0200
#define STUDIO_AY	 0x0400
#define STUDIO_AZ	 0x0800
#define STUDIO_AXR	 0x1000
#define STUDIO_AYR	 0x2000
#define STUDIO_AZR	 0x4000
#define STUDIO_TYPES 0x7FFF
#define STUDIO_RLOOP 0x8000 // controller that wraps shortest distance

// bonecontroller types
#define STUDIO_MOUTH 4

// sequence flags
#define STUDIO_LOOPING 0x0001

// bone flags
#define BONE_HAS_NORMALS	   0x0001
#define BONE_HAS_VERTICES	   0x0002
#define BONE_HAS_BBOX		   0x0004
#define BONE_JIGGLE_PROCEDURAL 0x0008

// These should match
// https://github.com/ValveSoftware/halflife/blob/master/engine/studio.h
struct studiohdr_t {
	std::int32_t ident;
	std::int32_t version;

	std::array<char, 64> name;
	std::int32_t length;

	float3_array eyeposition; // ideal eye position
	float3_array min;		  // ideal movement hull size
	float3_array max;

	float3_array bbmin; // clipping bounding box
	float3_array bbmax;

	std::int32_t flags;

	std::int32_t numbones; // bones
	std::int32_t boneindex;

	std::int32_t numbonecontrollers; // bone controllers
	std::int32_t bonecontrollerindex;

	std::int32_t numhitboxes; // complex bounding boxes
	std::int32_t hitboxindex;

	std::int32_t numseq; // animation sequences
	std::int32_t seqindex;

	std::int32_t numseqgroups; // demand loaded sequences
	std::int32_t seqgroupindex;

	std::int32_t numtextures; // raw textures
	std::int32_t textureindex;
	std::int32_t texturedataindex;

	std::int32_t numskinref; // replaceable textures
	std::int32_t numskinfamilies;
	std::int32_t skinindex;

	std::int32_t numbodyparts;
	std::int32_t bodypartindex;

	std::int32_t numattachments; // queryable attachable points
	std::int32_t attachmentindex;

	std::int32_t soundtable;
	std::int32_t soundindex;
	std::int32_t soundgroups;
	std::int32_t soundgroupindex;

	std::int32_t
		numtransitions; // animation node to animation node transition graph
	std::int32_t transitionindex;
};

// header for demand loaded sequence group data
typedef struct {
	std::int32_t id;
	std::int32_t version;

	std::array<char, 64> name;
	std::int32_t length;
} studioseqhdr_t;

// bones
typedef struct {
	std::array<char, 32> name; // bone name for symbolic links
	std::int32_t parent;	   // parent bone
	std::int32_t flags;		   // ??
	std::array<std::int32_t, 6>
		bonecontroller;			// bone controller index, -1 == none
	std::array<float, 6> value; // Default DoF values
	std::array<float, 6> scale; // Scale for delta DoF values
} mstudiobone_t;

// demand loaded sequence groups
struct mstudioseqgroup_t {
	std::array<char, 32> label; // Textual name
	std::array<char, 64> name;	// File name
	std::uint32_t unused;		// Was the "cache" index pointer
	std::int32_t data;			// Hack for group 0
};

// sequence descriptions
typedef struct {
	std::array<char, 32> label; // sequence label

	float fps;			// frames per second
	std::int32_t flags; // looping/non-looping flags

	std::int32_t activity;
	std::int32_t actweight;

	std::int32_t numevents;
	std::int32_t eventindex;

	std::int32_t numframes; // number of frames per sequence

	std::int32_t numpivots; // number of foot pivots
	std::int32_t pivotindex;

	std::int32_t motiontype;
	std::int32_t motionbone;
	float3_array linearmovement;
	std::int32_t automoveposindex;
	std::int32_t automoveangleindex;

	float3_array bbmin; // per sequence bounding box
	float3_array bbmax;

	std::int32_t numblends;
	std::int32_t
		animindex; // mstudioanim_t pointer relative to start of sequence
				   // group data [blend][bone][X, Y, Z, XR, YR, ZR]

	std::array<std::int32_t, 2> blendtype; // X, Y, Z, XR, YR, ZR
	std::array<float, 2> blendstart;	   // starting value
	std::array<float, 2> blendend;		   // ending value
	std::int32_t blendparent;

	std::int32_t seqgroup; // sequence group for demand loading

	std::int32_t entrynode; // transition node at entry
	std::int32_t exitnode;	// transition node at exit
	std::int32_t nodeflags; // transition rules

	std::int32_t nextseq; // auto advancing sequences
} mstudioseqdesc_t;

// events
typedef struct mstudioevent_s {
	std::int32_t frame;
	std::int32_t event;
	std::int32_t type;
	std::array<char, 64> options;
} mstudioevent_t;

// pivots
typedef struct {
	float3_array org; // pivot point
	std::int32_t start;
	std::int32_t end;
} mstudiopivot_t;

// attachment
typedef struct {
	std::array<char, 32> name;
	std::int32_t type;
	std::int32_t bone;
	float3_array org; // attachment point
	std::array<float3_array, 3> vectors;
} mstudioattachment_t;

typedef struct {
	std::array<std::uint16_t, 6> offset;
} mstudioanim_t;

// animation frames
union mstudioanimvalue_t {
	struct {
		std::uint8_t valid;
		std::uint8_t total;
	} num;

	std::int16_t value;
};

// body part index
struct mstudiobodyparts_t {
	std::array<char, 64> name;
	std::int32_t nummodels;
	std::int32_t base;
	std::int32_t modelindex; // index into models array
};

// skin info
struct mstudiotexture_t {
	std::array<char, 64> name;
	std::int32_t flags;
	std::int32_t width;
	std::int32_t height;
	std::int32_t index;
};

// skin families
// short	index[skinfamilies][skinref]

// studio models
struct mstudiomodel_t {
	std::array<char, 64> name;

	std::int32_t type;
	float boundingradius;

	std::int32_t nummesh;
	std::int32_t meshindex;

	std::int32_t numverts;		// number of unique vertices
	std::int32_t vertinfoindex; // vertex bone info
	std::int32_t vertindex;		// vertex float3_array
	std::int32_t numnorms;		// number of unique surface normals
	std::int32_t norminfoindex; // normal bone info
	std::int32_t normindex;		// normal float3_array

	std::int32_t numgroups;	 // UNUSED
	std::int32_t groupindex; // UNUSED
};

// meshes
struct mstudiomesh_t {
	std::int32_t numtris;
	std::int32_t triindex;
	std::int32_t skinref;
	std::int32_t numnorms;	// per mesh normals
	std::int32_t normindex; // UNUSED!
};
