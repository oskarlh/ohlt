#pragma once

struct MessageTable_t final {
	char const * title;
	char const * text;
	char const * howto;
};

enum assume_msgs {
	assume_first = 0,

	// Generic
	assume_NoMemory,
	assume_ValidPointer,
	assume_BadWorkcount,

	// HLCSG
	assume_MISSING_BRACKET_IN_TEXTUREDEF,
	assume_PLANE_WITH_NO_NORMAL,
	assume_BRUSH_WITH_COPLANAR_FACES,
	assume_BRUSH_OUTSIDE_WORLD,
	assume_MIXED_FACE_CONTENTS,
	assume_BRUSH_NOT_ALLOWED_IN_WORLD,
	assume_BRUSH_NOT_ALLOWED_IN_ENTITY,
	assume_NO_VISIBILE_BRUSHES,
	assume_ONLY_ORIGIN,
	assume_COULD_NOT_FIND_WAD,
	assume_MAX_TRIANGLES,
	assume_MAX_SWITCHED_LIGHTS,
	assume_MAX_TEXFILES,

	// HLBSP
	assume_LEAK,
	assume_MAX_LEAF_FACES,
	assume_MAX_SUPERFACEEDGES,
	assume_EmptySolid,

	// HLVIS
	assume_LEAF_PORTAL_SAW_INTO_LEAF,
	assume_MAX_PORTALS_ON_LEAF,
	assume_VALID_NETVIS_STATE,

	// HLRAD
	assume_MAX_TEXLIGHTS,
	assume_MAX_PATCHES,
	assume_TransferError,
	assume_BadSurfaceExtents,
	assume_MalformedTextureFace,
	assume_NoLights,
	assume_BadLightType,
	assume_MAX_SINGLEMAP,

	// Common
	assume_THREAD_ERROR,
	assume_MAX_MAP_PLANES,
	assume_MAX_MAP_TEXTURES,
	assume_MAX_MAP_MIPTEX,
	assume_FINAL_MAX_MAP_TEXINFO,
	assume_INITIAL_MAX_MAP_TEXINFO,
	assume_MAX_MAP_SIDES,
	assume_MAX_MAP_BRUSHES,
	assume_MAX_MAP_ENTITIES,
	assume_MAX_ENGINE_ENTITIES,
	assume_MAX_MAP_MODELS,
	assume_MAX_MAP_VERTS,
	assume_MAX_MAP_EDGES,
	assume_MAX_MAP_CLIPNODES,
	assume_MAX_MAP_MARKSURFACES,
	assume_MAX_MAP_FACES,
	assume_MAX_MAP_SURFEDGES,
	assume_MAX_MAP_NODES,
	assume_COMPRESSVIS_OVERFLOW,
	assume_DECOMPRESSVIS_OVERFLOW,
	assume_MAX_MAP_LEAFS,
	assume_TOOL_CANCEL,
	assume_GENERIC,
	assume_MAX_MAP_LIGHTING,
	assume_MAX_INTERNAL_MAP_PLANES,
	assume_COULD_NOT_LOCATE_WAD,

	assume_last
};

extern MessageTable_t const * GetAssume(assume_msgs id);
