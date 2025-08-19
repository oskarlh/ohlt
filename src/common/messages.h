#pragma once
#include <cstdint>
#include <string_view>

struct MessageTable_t final {
	std::u8string_view title;
	std::u8string_view text;
	std::u8string_view howto;
};

enum class assume_msg : std::uint8_t {
	first = 0,

	// Generic
	NoMemory,
	ValidPointer,

	// HLCSG
	PLANE_WITH_NO_NORMAL,
	BRUSH_WITH_COPLANAR_FACES,
	BRUSH_OUTSIDE_WORLD,
	MIXED_FACE_CONTENTS,
	BRUSH_NOT_ALLOWED_IN_WORLD,
	BRUSH_NOT_ALLOWED_IN_ENTITY,
	COULD_NOT_FIND_WAD,
	exceeded_MAX_SWITCHED_LIGHTS,
	exceeded_MAX_TEXFILES,

	// HLBSP
	exceeded_MAX_LEAF_FACES,
	exceeded_MAX_SUPERFACEEDGES,
	EmptySolid,

	// HLVIS
	exceeded_MAX_PORTALS_ON_LEAF,

	// HLRAD
	exceeded_MAX_PATCHES,
	MalformedTextureFace,
	BadLightType,
	exceeded_MAX_SINGLEMAP,

	// Common
	exceeded_MAX_MAP_PLANES,
	exceeded_MAX_MAP_TEXTURES,
	exceeded_MAX_MAP_MIPTEX,
	exceeded_FINAL_MAX_MAP_TEXINFO,
	exceeded_INITIAL_MAX_MAP_TEXINFO,
	exceeded_MAX_MAP_SIDES,
	exceeded_MAX_MAP_BRUSHES,
	exceeded_MAX_ENGINE_ENTITIES,
	exceeded_MAX_MAP_MODELS,
	exceeded_MAX_MAP_VERTS,
	exceeded_MAX_MAP_EDGES,
	exceeded_MAX_MAP_CLIPNODES,
	exceeded_MAX_MAP_MARKSURFACES,
	exceeded_MAX_MAP_FACES,
	exceeded_MAX_MAP_SURFEDGES,
	exceeded_MAX_MAP_NODES,
	COMPRESSVIS_OVERFLOW,
	DECOMPRESSVIS_OVERFLOW,
	exceeded_MAX_MAP_LEAFS,
	TOOL_CANCEL,
	exceeded_MAX_MAP_LIGHTING,
	exceeded_MAX_INTERNAL_MAP_PLANES,
	COULD_NOT_LOCATE_WAD,

	last
};

MessageTable_t const & get_assume(assume_msg id) noexcept;
