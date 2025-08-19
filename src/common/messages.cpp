#include "messages.h"

#include <utility>

// Common descriptions
std::u8string_view const internallimit
	= u8"The compiler tool hit an internal limit";
std::u8string_view const internalerror
	= u8"The compiler tool had an internal error";
std::u8string_view const maperror
	= u8"The map has a problem which must be fixed";

// Common explanations
std::u8string_view const selfexplanitory = u8"self explanitory";
std::u8string_view const reference
	= u8"Check the file http://www.zhlt.info/common-mapping-problems.html for a detailed explanation of this problem";
std::u8string_view const simplify
	= u8"The map is too complex for the game engine/compile tools to handle.  Simplify";
std::u8string_view const contact
	= u8"If you think this error is not caused by mapper error, you can file an issue at " PROJECT_ISSUE_TRACKER;

static MessageTable_t const assumes[std::to_underlying(assume_msg::last
)] = {
	{ u8"invalid assume message",
	  u8"This is a message should never be printed.",
	  contact },

	// Generic
	{ u8"Memory allocation failure",
	  u8"The program failled to allocate a block of memory.",
	  u8"Likely causes are (in order of likeliness) : the partition holding the swapfile is full; swapfile size is smaller than required; memory fragmentation; heap corruption" },
	{ u8"NULL Pointer", internalerror, contact },
	{ u8"Bad Thread Workcount", internalerror, contact },

	// HLCSG
	{ u8"Missing '[' in texturedef (U)", maperror, reference },
	{ u8"plane with no normal", maperror, reference },
	{ u8"brush with coplanar faces", maperror, reference },
	{ u8"brush outside world", maperror, reference },
	{ u8"mixed face contents", maperror, reference },
	{ u8"Brush type not allowed in world", maperror, reference },
	{ u8"Brush type not allowed in entity", maperror, reference },
	{ u8"No visibile brushes",
	  u8"All brushes are CLIP or ORIGIN (at least one must be normal/visible)",
	  selfexplanitory },
	{ u8"Entity with ONLY an ORIGIN brush",
	  u8"All entities need at least one visible brush to function properly.  CLIP, HINT, ORIGIN, do not count as visible brushes.",
	  selfexplanitory },
	{ u8"Could not find wad file",
	  u8"The compile tools could not locate a wad file that the map was referencing.",
	  u8"Make sure the wad's listed in the level editor actually all exist" },
	{ u8"Exceeded MAX_TRIANGLES", internallimit, contact },
	{ u8"Exceeded MAX_SWITCHED_LIGHTS",
	  u8"The maximum number of switchable light entities has been reached",
	  selfexplanitory },
	{ u8"Exceeded MAX_TEXFILES", internallimit, contact },

	// HLBSP
	{ u8"LEAK in the map", maperror, reference },
	{ u8"Exceeded MAX_LEAF_FACES",
	  u8"This error is almost always caused by an invalid brush, by having huge rooms, or scaling a texture down to extremely small values (between -1 and 1)",
	  u8"Find the invalid brush.  Any imported prefabs, carved brushes, or vertex manipulated brushes should be suspect" },
	{ u8"Exceeded MAX_SUPERFACEEDGES", internallimit, contact },
	{ u8"Empty Solid Entity",
	  u8"A solid entity in the map (func_wall for example) has no brushes.",
	  u8"If using Worldcraft, do a check for problems and fix any occurences of 'Empty solid'" },

	// HLVIS
	{ u8"Leaf portal saw into leaf", maperror, reference },
	{ u8"Exceeded MAX_PORTALS_ON_LEAF", maperror, reference },

	// HLRAD
	{ u8"Exceeded MAX_PATCHES", maperror, reference },
	{ u8"Transfer < 0", internalerror, contact },
	{ u8"Bad Surface Extents", maperror, reference },
	{ u8"Malformed face normal",
	  u8"The texture alignment of a visible face is unusable",
	  u8"If using Worldcraft, do a check for problems and fix any occurences of 'Texture axis perpindicular to face'" },
	{ u8"No Lights!",
	  u8"lighting of map halted (I assume you do not want a pitch black map!)",
	  u8"Put some lights in the map." },
	{ u8"Bad Light Type", internalerror, contact },
	{ u8"Exceeded MAX_SINGLEMAP", internallimit, contact },

	// Common
	{ u8"Unable to create thread", internalerror, contact },
	{ u8"Exceeded MAX_MAP_PLANES",
	  u8"The maximum number of plane definitions has been reached",
	  u8"The map has grown too complex" },
	{ u8"Exceeded MAX_MAP_TEXTURES",
	  u8"The maximum number of textures for a map has been reached",
	  selfexplanitory },

	{ u8"Exceeded MAX_MAP_MIPTEX",
	  u8"Texture memory usage on the map has exceeded the limit",
	  u8"Merge similar textures, remove unused textures from the map" },
	{ u8"Exceeded FINAL_MAX_MAP_TEXINFO", maperror, contact },
	{ u8"Exceeded INITIAL_MAX_MAP_TEXINFO", internalerror, contact },
	{ u8"Exceeded MAX_MAP_SIDES", internallimit, contact },
	{ u8"Exceeded MAX_MAP_BRUSHES",
	  u8"The maximum number of brushes for a map has been reached",
	  selfexplanitory },
	{ u8"Exceeded MAX_MAP_ENTITIES",
	  u8"The maximum number of entities for the compile tools has been reached",
	  selfexplanitory },
	{ u8"Exceeded MAX_ENGINE_ENTITIES",
	  u8"The maximum number of entities for the half-life engine has been reached",
	  selfexplanitory },

	{ u8"Exceeded MAX_MAP_MODELS",
	  u8"The maximum number of brush based entities has been reached",
	  u8"Remove unnecessary brush entities, consolidate similar entities into a single entity" },
	{ u8"Exceeded MAX_MAP_VERTS",
	  u8"The maximum number of vertices for a map has been reached",
	  simplify }, // internallimit, contact //--vluzacn
	{ u8"Exceeded MAX_MAP_EDGES", internallimit, contact },

	{ u8"Exceeded MAX_MAP_CLIPNODES", maperror, reference },
	{ u8"Exceeded MAX_MAP_MARKSURFACES", internallimit, contact },
	{ u8"Exceeded MAX_MAP_FACES",
	  u8"The maximum number of faces for a map has been reached",
	  u8"This error is typically caused by having a large face with a small texture scale on it, or overly complex maps." },
	{ u8"Exceeded MAX_MAP_SURFEDGES", internallimit, contact },
	{ u8"Exceeded MAX_MAP_NODES",
	  u8"The maximum number of nodes for a map has been reached",
	  simplify },
	{ u8"CompressVis Overflow", internalerror, contact },
	{ u8"DecompressVis Overflow", internalerror, contact },
	{ u8"Exceeded MAX_MAP_LEAFS",
	  u8"The maximum number of leaves for a map has been reached",
	  simplify },
	{ u8"Execution Cancelled",
	  u8"Tool execution was cancelled either by the user or due to a fatal compile setting",
	  selfexplanitory },
	{ u8"Internal Error", internalerror, contact },
	{ u8"Exceeded MAX_MAP_LIGHTING",
	  u8"You have run out of light data memory",
	  u8"It might be caused by a compiler bug." },
	{ u8"Exceeded MAX_INTERNAL_MAP_PLANES",
	  u8"The maximum number of plane definitions has been reached",
	  u8"The map has grown too complex" },
	{ u8"Could not locate WAD file",
	  u8"The compile tools could not locate a wad file that the map was referencing.",
	  u8"Make sure the file '<mapname>.wa_' exists. This is a file generated by hlcsg and you should not delete it. If you have to run hlrad without this file, use '-waddir' to specify folders where hlrad can find all the wad files." },
};

MessageTable_t const & get_assume(assume_msg id) noexcept {
	return assumes[std::to_underlying(id)];
}
