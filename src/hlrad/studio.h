#pragma once

#include "mathtypes.h"

#include <cstdint>

// Studio models are position independent, so the cache manager can move
// them.

// Studio limits
constexpr std::int32_t MAXSTUDIOSKINS = 128; // Total textures
constexpr std::size_t MAXSTUDIOBONES = 128;	 // Total bones actually used

// lighting & rendermode options
#define STUDIO_NF_CHROME	  0x0002
#define STUDIO_NF_ADDITIVE	  0x0020 // rendering with additive mode
#define STUDIO_NF_TRANSPARENT 0x0040 // use texture with alpha channel
#define STUDIO_NF_UV_COORDS \
	(1 << 31) // using half-float coords instead of ST

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
struct studioseqhdr_t {
	std::int32_t id;
	std::int32_t version;

	std::array<char, 64> name;
	std::int32_t length;
};

// bones
struct mstudiobone_t {
	std::array<char, 32> name; // bone name for symbolic links
	std::int32_t parent;	   // parent bone
	std::int32_t flags;		   // ??
	std::array<std::int32_t, 6>
		bonecontroller;			// bone controller index, -1 == none
	std::array<float, 6> value; // Default DoF values
	std::array<float, 6> scale; // Scale for delta DoF values
};

// demand loaded sequence groups
struct mstudioseqgroup_t {
	std::array<char, 32> label; // Textual name
	std::array<char, 64> name;	// File name
	std::uint32_t unused;		// Was the "cache" index pointer
	std::int32_t data;			// Hack for group 0
};

// sequence descriptions
struct mstudioseqdesc_t {
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
};

// events
struct mstudioevent_t {
	std::int32_t frame;
	std::int32_t event;
	std::int32_t type;
	std::array<char, 64> options;
};

// pivots
struct mstudiopivot_t {
	float3_array org; // pivot point
	std::int32_t start;
	std::int32_t end;
};

// attachment
struct mstudioattachment_t {
	std::array<char, 32> name;
	std::int32_t type;
	std::int32_t bone;
	float3_array org; // attachment point
	std::array<float3_array, 3> vectors;
};

struct mstudioanim_t {
	std::array<std::uint16_t, 6> offset;
};

struct mstudioanimvalue_num_t {
	std::uint8_t valid;
	std::uint8_t total;
};

// Animation frames
union mstudioanimvalue_t {
	mstudioanimvalue_num_t num;
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
