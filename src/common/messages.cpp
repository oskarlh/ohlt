#include "messages.h"

// Common descriptions
char const * const internallimit
	= "The compiler tool hit an internal limit";
char const * const internalerror
	= "The compiler tool had an internal error";
char const * const maperror = "The map has a problem which must be fixed";

// Common explanations
char const * const selfexplanitory = "self explanitory";
char const * const reference
	= "Check the file http://www.zhlt.info/common-mapping-problems.html for a detailed explanation of this problem";
char const * const simplify
	= "The map is too complex for the game engine/compile tools to handle.  Simplify";
char const * const contact
	= "If you think this error is not caused by mapper error, you can file an issue at " PROJECT_ISSUE_TRACKER;

static MessageTable_t const assumes[assume_last] = {
	{ "invalid assume message",
	  "This is a message should never be printed.",
	  contact },

	// Generic
	{ "Memory allocation failure",
	  "The program failled to allocate a block of memory.",
	  "Likely causes are (in order of likeliness) : the partition holding the swapfile is full; swapfile size is smaller than required; memory fragmentation; heap corruption" },
	{ "NULL Pointer", internalerror, contact },
	{ "Bad Thread Workcount", internalerror, contact },

	// HLCSG
	{ "Missing '[' in texturedef (U)", maperror, reference },
	{ "plane with no normal", maperror, reference },
	{ "brush with coplanar faces", maperror, reference },
	{ "brush outside world", maperror, reference },
	{ "mixed face contents", maperror, reference },
	{ "Brush type not allowed in world", maperror, reference },
	{ "Brush type not allowed in entity", maperror, reference },
	{ "No visibile brushes",
	  "All brushes are CLIP or ORIGIN (at least one must be normal/visible)",
	  selfexplanitory },
	{ "Entity with ONLY an ORIGIN brush",
	  "All entities need at least one visible brush to function properly.  CLIP, HINT, ORIGIN, do not count as visible brushes.",
	  selfexplanitory },
	{ "Could not find wad file",
	  "The compile tools could not locate a wad file that the map was referencing.",
	  "Make sure the wad's listed in the level editor actually all exist" },
	{ "Exceeded MAX_TRIANGLES", internallimit, contact },
	{ "Exceeded MAX_SWITCHED_LIGHTS",
	  "The maximum number of switchable light entities has been reached",
	  selfexplanitory },
	{ "Exceeded MAX_TEXFILES", internallimit, contact },

	// HLBSP
	{ "LEAK in the map", maperror, reference },
	{ "Exceeded MAX_LEAF_FACES",
	  "This error is almost always caused by an invalid brush, by having huge rooms, or scaling a texture down to extremely small values (between -1 and 1)",
	  "Find the invalid brush.  Any imported prefabs, carved brushes, or vertex manipulated brushes should be suspect" },
	{ "Exceeded MAX_SUPERFACEEDGES", internallimit, contact },
	{ "Empty Solid Entity",
	  "A solid entity in the map (func_wall for example) has no brushes.",
	  "If using Worldcraft, do a check for problems and fix any occurences of 'Empty solid'" },

	// HLVIS
	{ "Leaf portal saw into leaf", maperror, reference },
	{ "Exceeded MAX_PORTALS_ON_LEAF", maperror, reference },
	{ "Invalid client/server state", internalerror, contact },

	// HLRAD
	{ "Exceeded MAX_TEXLIGHTS",
	  "The maximum number of texture lights in use by a single map has been reached",
	  "Use fewer texture lights." },
	{ "Exceeded MAX_PATCHES", maperror, reference },
	{ "Transfer < 0", internalerror, contact },
	{ "Bad Surface Extents", maperror, reference },
	{ "Malformed face normal",
	  "The texture alignment of a visible face is unusable",
	  "If using Worldcraft, do a check for problems and fix any occurences of 'Texture axis perpindicular to face'" },
	{ "No Lights!",
	  "lighting of map halted (I assume you do not want a pitch black map!)",
	  "Put some lights in the map." },
	{ "Bad Light Type", internalerror, contact },
	{ "Exceeded MAX_SINGLEMAP", internallimit, contact },

	// Common
	{ "Unable to create thread", internalerror, contact },
	{ "Exceeded MAX_MAP_PLANES",
	  "The maximum number of plane definitions has been reached",
	  "The map has grown too complex" },
	{ "Exceeded MAX_MAP_TEXTURES",
	  "The maximum number of textures for a map has been reached",
	  selfexplanitory },

	{ "Exceeded MAX_MAP_MIPTEX",
	  "Texture memory usage on the map has exceeded the limit",
	  "Merge similar textures, remove unused textures from the map" },
	{ "Exceeded FINAL_MAX_MAP_TEXINFO", maperror, contact },
	{ "Exceeded INITIAL_MAX_MAP_TEXINFO", internalerror, contact },
	{ "Exceeded MAX_MAP_SIDES", internallimit, contact },
	{ "Exceeded MAX_MAP_BRUSHES",
	  "The maximum number of brushes for a map has been reached",
	  selfexplanitory },
	{ "Exceeded MAX_MAP_ENTITIES",
	  "The maximum number of entities for the compile tools has been reached",
	  selfexplanitory },
	{ "Exceeded MAX_ENGINE_ENTITIES",
	  "The maximum number of entities for the half-life engine has been reached",
	  selfexplanitory },

	{ "Exceeded MAX_MAP_MODELS",
	  "The maximum number of brush based entities has been reached",
	  "Remove unnecessary brush entities, consolidate similar entities into a single entity" },
	{ "Exceeded MAX_MAP_VERTS",
	  "The maximum number of vertices for a map has been reached",
	  simplify }, // internallimit, contact //--vluzacn
	{ "Exceeded MAX_MAP_EDGES", internallimit, contact },

	{ "Exceeded MAX_MAP_CLIPNODES", maperror, reference },
	{ "Exceeded MAX_MAP_MARKSURFACES", internallimit, contact },
	{ "Exceeded MAX_MAP_FACES",
	  "The maximum number of faces for a map has been reached",
	  "This error is typically caused by having a large face with a small texture scale on it, or overly complex maps." },
	{ "Exceeded MAX_MAP_SURFEDGES", internallimit, contact },
	{ "Exceeded MAX_MAP_NODES",
	  "The maximum number of nodes for a map has been reached",
	  simplify },
	{ "CompressVis Overflow", internalerror, contact },
	{ "DecompressVis Overflow", internalerror, contact },
	{ "Exceeded MAX_MAP_LEAFS",
	  "The maximum number of leaves for a map has been reached",
	  simplify },
	{ "Execution Cancelled",
	  "Tool execution was cancelled either by the user or due to a fatal compile setting",
	  selfexplanitory },
	{ "Internal Error", internalerror, contact },
	{ "Exceeded MAX_MAP_LIGHTING",
	  "You have run out of light data memory",
	  "It might be caused by a compiler bug." },
	{ "Exceeded MAX_INTERNAL_MAP_PLANES",
	  "The maximum number of plane definitions has been reached",
	  "The map has grown too complex" },
	{ "Could not locate WAD file",
	  "The compile tools could not locate a wad file that the map was referencing.",
	  "Make sure the file '<mapname>.wa_' exists. This is a file generated by hlcsg and you should not delete it. If you have to run hlrad without this file, use '-waddir' to specify folders where hlrad can find all the wad files." },
};

MessageTable_t const * GetAssume(assume_msgs id) {
	if (!(id > assume_first && id < assume_last
		)) //(!(id > assume_first) && (id < assume_last)) --vluzacn
	{
		id = assume_first;
	}
	return &assumes[id];
}
