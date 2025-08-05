#pragma once

#include "entity_key_value.h"
#include "external_types/external_types.h"
#include "internal_types/internal_types.h"
#include "mathtypes.h"
#include "planes.h"
#include "wad_texture_name.h"

#include <cstddef>
#include <filesystem>
#include <vector>

// upper design bounds

constexpr std::ptrdiff_t MAX_MAP_MODELS = 512; // 400 //vluzacn
// variable, but 400 brush entities is very stressful on the engine and
// network code as it is

constexpr std::ptrdiff_t MAX_ENGINE_ENTITIES = 16384; // 1024 //vluzacn
constexpr std::ptrdiff_t MAX_MAP_ENTITIES = 16384;	  // 2048 //vluzacn
// hard limit, in actuallity it is too much, as temporary entities in the
// game plus static map entities can overflow

constexpr std::ptrdiff_t MAX_MAP_ENTSTRING = 2048 * 1024; //(512*1024)
// abitrary, 512Kb of string data should be plenty even with TFC FGD's

constexpr std::ptrdiff_t MAX_MAP_NODES = 32767;
// hard limit (negative short's are used as contents values)
constexpr std::ptrdiff_t MAX_MAP_CLIPNODES = 32767;
// hard limit (negative short's are used as contents values)

constexpr std::ptrdiff_t MAX_MAP_LEAFS = 32760;
constexpr std::ptrdiff_t MAX_MAP_LEAFS_ENGINE = 8192;
// No problem has been observed in testmap or reported, except when viewing
// the map from outside (some leafs missing, no crash). This problem
// indicates that engine's MAX_MAP_LEAFS is 8192 (for reason, see: Quake -
// gl_model.c - Mod_Init). I don't know if visleafs > 8192 will cause
// Mod_DecompressVis overflow.

constexpr std::ptrdiff_t MAX_MAP_VERTS = 65535;
constexpr std::ptrdiff_t MAX_MAP_FACES
	= 65535; // This ought to be 32768, otherwise faces(in world) can become
			 // invisible. --vluzacn
constexpr std::ptrdiff_t MAX_MAP_WORLDFACES = 32768;
constexpr std::ptrdiff_t MAX_MAP_MARKSURFACES = 65535;
// hard limit (data structures store them as unsigned shorts)

constexpr std::ptrdiff_t MAX_MAP_TEXTURES = 4096;
// hard limit (halflife limitation) // I used 2048 different textures in a
// test map and everything looks fine in both opengl and d3d mode.

constexpr std::ptrdiff_t MAX_MAP_EDGES = 256'000;
constexpr std::ptrdiff_t MAX_MAP_SURFEDGES = 512'000;
// arbtirary

constexpr std::ptrdiff_t MAX_MAP_VISIBILITY = 0x80'0000;
// arbitrary

// these are for entity key:value pairs
constexpr std::ptrdiff_t MAX_KEY = 128; // 32 //vluzacn
constexpr std::ptrdiff_t MAX_VAL
	= 4096; // the name used to be MAX_VALUE //vluzacn
// quote from yahn: 'probably can raise these values if needed'

// texture size limit

constexpr std::ptrdiff_t MAX_TEXTURE_SIZE
	= 348'972; // Bytes in a 512x512 image((256 * 256 * sizeof(short) * 3) /
			   // 2) //stop compiler from warning 512*512 texture. --vluzacn
// this is arbitrary, and needs space for the largest realistic texture plus
// room for its mipmaps.'  This value is primarily used to catch damanged or
// invalid textures in a wad file

constexpr std::ptrdiff_t TEXTURE_STEP
	= 16; // this constant was previously defined in lightmap.cpp. --vluzacn
constexpr std::ptrdiff_t MAX_SURFACE_EXTENT
	= 16; // if lightmap extent exceeds 16, the map will not be able to load
		  // in 'Software' renderer and HLDS. //--vluzacn

constexpr double ENGINE_ENTITY_RANGE = 4096.0;
//=============================================================================

constexpr std::uint32_t BSPVERSION = 30;

//
// BSP File Structures
//

struct lump_t final {
	std::uint32_t fileofs;
	std::uint32_t filelen;
};

struct dmodel_t final {
	float3_array mins;
	float3_array maxs;
	float3_array origin;
	std::int32_t headnode[NUM_HULLS];
	std::int32_t visleafs; // not including the solid leaf 0
	std::int32_t firstface, numfaces;
};

struct dmiptexlump_t final {
	// TODO: Should these be unsigned? I think hlbsp's FinishBSPFile writes
	// -1 in some cases? Look into it
	std::int32_t nummiptex;
	std::int32_t dataofs[1]; // Important: It's actually
							 // dataofs[nummiptex]
};

constexpr std::size_t MIPLEVELS = 4; // Four mip maps stored

struct miptex_t final {
	wad_texture_name name;
	std::uint32_t width, height;
	std::uint32_t offsets[MIPLEVELS];
};

// https://developer.valvesoftware.com/wiki/Miptex
// LOOKS LIKE I *MIGHT* BE ABLE TO OMIT ALL BUT THE FIRST MIP LEVEL?????
// IF OPENGL HALF-LIFE ISN'T USING THEM.
// STILL NEED THE PALETTE THOUGH
class miptex_header_and_data final {
  private:
	std::unique_ptr<std::byte[]> headerAndData;

  public:
	miptex_header_and_data() = delete;
	miptex_header_and_data(miptex_header_and_data const &) = delete;
	constexpr miptex_header_and_data(miptex_header_and_data&&) noexcept
		= default;

	constexpr miptex_header_and_data(std::size_t dataSize) {
		std::size_t totalSize = sizeof(miptex_t) + dataSize;
		headerAndData = std::make_unique_for_overwrite<std::byte[]>(
			totalSize
		);
		[[maybe_unused]] miptex_t& header = *new (headerAndData.get())
												miptex_t{};
	}

	constexpr ~miptex_header_and_data() {
		header().~miptex_t();
	}

	miptex_header_and_data& operator=(miptex_header_and_data const &)
		= delete;
	constexpr miptex_header_and_data&
	operator=(miptex_header_and_data&&) noexcept
		= default;

	constexpr miptex_t& header() noexcept {
		return *(miptex_t*) headerAndData.get();
	}

	constexpr miptex_t const & header() const noexcept {
		return *(miptex_t const *) headerAndData.get();
	}

	constexpr std::byte* data() noexcept {
		return headerAndData.get() + sizeof(miptex_t);
	}

	constexpr std::byte const * data() const noexcept {
		return headerAndData.get() + sizeof(miptex_t);
	}
};

class miptex_header_and_data_view final {
  private:
	std::byte* headerAndData;

  public:
	miptex_header_and_data_view() = delete;

	constexpr miptex_header_and_data_view(std::byte* ptr) noexcept :
		headerAndData(ptr) { }

	constexpr miptex_header_and_data_view(miptex_header_and_data_view const &)
		noexcept
		= default;

	constexpr miptex_header_and_data_view&
	operator=(miptex_header_and_data_view const &) noexcept
		= default;

	constexpr miptex_t& header() noexcept {
		return *(miptex_t*) headerAndData;
	}

	constexpr miptex_t const & header() const noexcept {
		return *(miptex_t const *) headerAndData;
	}

	constexpr std::byte* data() noexcept {
		return headerAndData + sizeof(miptex_t);
	}

	constexpr std::byte const * data() const noexcept {
		return headerAndData + sizeof(miptex_t);
	}
};

struct dvertex_t final {
	std::array<float, 3> point;
};

enum class contents_t : std::int32_t {
	// DECISION_NODE is not really a content type and it's only used in
	// HLBSP's node_t::contents
	DECISION_NODE = 0,

	// Non-solid and doesn't affect VIS
	EMPTY = -1,

	// Default
	SOLID = -2,

	// Textures with the ! prefix are usually water when part of
	// worldspawn, unless they are slime, lava, or a current.
	WATER = -3,

	// Like water but deals damage to the player and some
	// monsters when touched
	SLIME = -4,

	// Like slime but deals double the damage
	LAVA = -5,
	SKY = -6,

	// Replaces an entity's origin KV, then is removed by HLCSG
	// If a brush were to have this content type at runtime, it would be
	// treated like water
	ORIGIN = -7,

	// Acts like water combined with a trigger_push with a speed of 2048 in
	// the given direction
	CURRENT_0 = -9,
	CURRENT_90 = -10,
	CURRENT_180 = -11,
	CURRENT_270 = -12,
	CURRENT_UP = -13,
	CURRENT_DOWN = -14,

	// Like water, but never solid. Despite this, it still affects VIS
	TRANSLUCENT = -15,

	// Removed in CSG and BSP, so VIS and RAD shouldn't
	// have to deal with this, only clip planes!
	CONTENTS_NULL = -20,

	// Filters down to EMPTY by BSP,
	// ENGINE SHOULD NEVER SEE THIS.
	// The game engine also uses this
	HINT = -21,

	// Similar to ORIGIN
	BOUNDINGBOX = -22,

	TOEMPTY = -32,
};

std::u8string_view ContentsToString(contents_t contents) noexcept;

struct dnode_t final {
	std::int32_t planenum;
	std::array<std::int16_t, 2>
		children; // Negative numbers are -(leafs+1), not nodes
	std::array<std::int16_t, 3> mins; // For sphere culling
	std::array<std::int16_t, 3> maxs;
	std::uint16_t firstface;
	std::uint16_t numfaces; // Counting both sides
};

struct dclipnode_t final {
	std::int32_t planenum;
	std::int16_t children[2]; // Negative numbers are contents
};

// note that edge 0 is never used, because negative edge nums are used for
// counterclockwise use of the edge in a face
struct dedge_t final {
	std::array<std::uint16_t, 2> v; // vertex numbers
};

constexpr std::size_t MAXLIGHTMAPS = 4;

struct dface_t final {
	std::uint16_t planenum;
	std::uint16_t side;

	std::uint32_t firstedge; // We must support > 64k edges
	std::uint16_t numedges;
	texinfo_count texinfo; // Can be no_texinfo (-1)

	// lighting info
	std::array<std::uint8_t, MAXLIGHTMAPS> styles;
	std::int32_t lightofs; // Start of [numstyles*surfsize] samples
};

// leaf 0 is the generic contents_t::SOLID leaf, used for all solid areas
// all other leafs need visibility info
struct dleaf_t final {
	contents_t contents;
	std::int32_t visofs; // -1 = no visibility info

	// For frustum culling
	std::array<std::int16_t, 3> mins;
	std::array<std::int16_t, 3> maxs;

	std::uint16_t firstmarksurface;
	std::uint16_t nummarksurfaces;

	std::array<std::uint8_t, 4>
		unused_ambient_level; // Used by Quake, not by Half-Life
};

// TODO: Swap the ENTITIES and PLANES lump headers
// when compiling Blue Shift maps. Swapped ENTITIES and PLANES are
// the ONLY difference between Blue Shift BSPs and Half-Life BSPs
constexpr std::size_t LUMP_ENTITIES = 0;
constexpr std::size_t LUMP_PLANES = 1;
constexpr std::size_t LUMP_TEXTURES = 2;
constexpr std::size_t LUMP_VERTEXES = 3;
constexpr std::size_t LUMP_VISIBILITY = 4;
constexpr std::size_t LUMP_NODES = 5;
constexpr std::size_t LUMP_TEXINFO = 6;
constexpr std::size_t LUMP_FACES = 7;
constexpr std::size_t LUMP_LIGHTING = 8;
constexpr std::size_t LUMP_CLIPNODES = 9;
constexpr std::size_t LUMP_LEAFS = 10;
constexpr std::size_t LUMP_MARKSURFACES = 11;
constexpr std::size_t LUMP_EDGES = 12;
constexpr std::size_t LUMP_SURFEDGES = 13;
constexpr std::size_t LUMP_MODELS = 14;

enum class lump_id : std::size_t {
	entities = 0,
	planes,
	textures,
	vertexes,
	visibility,
	nodes,
	texinfo,
	faces,
	lighting,
	clipnodes,
	leafs,
	marksurfaces,
	edges,
	surfedges,
	models
};
constexpr std::size_t num_lump_types = std::size_t(lump_id::models) + 1;

template <lump_id Id>
struct lump_element_type_map final { };

template <>
struct lump_element_type_map<lump_id::entities> final {
	using type = char8_t;
};

template <>
struct lump_element_type_map<lump_id::planes> final {
	using type = dplane_t;
};

template <>
struct lump_element_type_map<lump_id::textures> final {
	using type = std::byte;
};

template <>
struct lump_element_type_map<lump_id::vertexes> final {
	using type = dvertex_t;
};

template <>
struct lump_element_type_map<lump_id::visibility> final {
	using type = std::byte;
};

template <>
struct lump_element_type_map<lump_id::nodes> final {
	using type = dnode_t;
};

template <>
struct lump_element_type_map<lump_id::texinfo> final {
	using type = texinfo_t;
};

template <>
struct lump_element_type_map<lump_id::faces> final {
	using type = dface_t;
};

template <>
struct lump_element_type_map<lump_id::lighting> final {
	using type = std::byte;
};

template <>
struct lump_element_type_map<lump_id::clipnodes> final {
	using type = dclipnode_t;
};

template <>
struct lump_element_type_map<lump_id::leafs> final {
	using type = dleaf_t;
};

template <>
struct lump_element_type_map<lump_id::marksurfaces> final {
	using type = std::uint16_t;
};

template <>
struct lump_element_type_map<lump_id::edges> final {
	using type = dedge_t;
};

template <>
struct lump_element_type_map<lump_id::surfedges> final {
	using type = std::int32_t;
};

template <>
struct lump_element_type_map<lump_id::models> final {
	using type = dmodel_t;
};

template <lump_id Id>
using lump_element_type = lump_element_type_map<Id>::type;

struct dheader_t final {
	std::uint32_t version;
	lump_t lumps[num_lump_types];
};

//============================================================================

constexpr float ANGLE_UP{ -1.0 };
constexpr float ANGLE_DOWN{ -2.0 };

//
// Entity Related Stuff
//

// Called by every stage except hlcsg
extern void parse_entities_from_bsp_file();

extern void DeleteKey(entity_t* ent, std::u8string_view key);
extern void set_key_value(entity_t* ent, entity_key_value&& newKeyValue);
extern void set_key_value(
	entity_t* ent, std::u8string_view key, std::u8string_view value
);
extern char8_t const *
ValueForKey(entity_t const * const ent, std::u8string_view key);
std::u8string_view
value_for_key(entity_t const * const ent, std::u8string_view key);
bool has_key_value(entity_t const * const ent, std::u8string_view key);
bool key_value_is_empty(entity_t const * const ent, std::u8string_view key);
bool key_value_is(
	entity_t const * const ent,
	std::u8string_view key,
	std::u8string_view value
);
bool key_value_starts_with(
	entity_t const * const ent,
	std::u8string_view key,
	std::u8string_view prefix
);
bool classname_is(entity_t const * const ent, std::u8string_view classname);
std::u8string_view get_classname(entity_t const & ent);

std::int32_t
IntForKey(entity_t const * const ent, std::u8string_view key) noexcept;
float float_for_key(entity_t const & ent, std::u8string_view key) noexcept;

bool bool_key_value(entity_t const & ent, std::u8string_view key) noexcept;

// Returns `clamp((kv as a double), min, max)`.
// If the key-value does not contain a number, returns std::nullopt
std::optional<double> clamp_double_key_value(
	entity_t const & ent, std::u8string_view key, double min, double max
) noexcept;

// Returns `clamp((kv as an unsigned integer), min, max)`.
// If the key-value does not contain a number, returns std::nullopt
std::optional<std::uint64_t> clamp_unsigned_integer_from_string_key_value(
	entity_t const & ent,
	std::u8string_view key,
	std::uint64_t min,
	std::uint64_t max
) noexcept;

// Returns `clamp((kv as a signed integer), min, max)`.
// If the key-value does not contain a number, returns std::nullopt
std::optional<std::int64_t> clamp_signed_integer_from_string_key_value(
	entity_t const & ent,
	std::u8string_view key,
	std::int64_t min,
	std::int64_t max
) noexcept;

// Returns `clamp((kv as a floating-point number), min, max)`.
// If the key-value does not contain a number, returns std::nullopt
template <std::floating_point NumberType>
std::optional<NumberType> numeric_key_value(
	entity_t const & ent,
	std::u8string_view key,
	NumberType min = std::numeric_limits<NumberType>::min(),
	NumberType max = std::numeric_limits<NumberType>::max()
) noexcept {
	return clamp_double_key_value(ent, key, min, max)
		.transform([](double result) { return NumberType(result); });
}

// Returns `clamp((kv as an unsigned integer), min, max)`.
// If the key-value does not contain a number, returns std::nullopt
template <std::unsigned_integral NumberType>
std::optional<NumberType> numeric_key_value(
	entity_t const & ent,
	std::u8string_view key,
	NumberType min = std::numeric_limits<NumberType>::min(),
	NumberType max = std::numeric_limits<NumberType>::max()
) noexcept {
	return clamp_unsigned_integer_from_string_key_value(ent, key, min, max)
		.transform([](std::uint64_t result) { return NumberType(result); });
}

// Returns `clamp((kv as a signed integer), min, max)`.
// If the key-value does not contain a number, returns std::nullopt
template <std::signed_integral NumberType>
std::optional<NumberType> numeric_key_value(
	entity_t const & ent,
	std::u8string_view key,
	NumberType min = std::numeric_limits<NumberType>::min(),
	NumberType max = std::numeric_limits<NumberType>::max()
) noexcept {
	return clamp_signed_integer_from_string_key_value(ent, key, min, max)
		.transform([](std::int64_t result) { return NumberType(result); });
}

float3_array
get_float3_for_key(entity_t const & ent, std::u8string_view key);
double3_array
get_double3_for_key(entity_t const & ent, std::u8string_view key);

template <any_vec3 Vec3>
inline float3_array
get_vector_for_key(entity_t const & ent, std::u8string_view key)
	requires(std::is_same_v<Vec3, float3_array>) {
	return get_float3_for_key(ent, key);
}

template <any_vec3 Vec3>
inline double3_array
get_vector_for_key(entity_t const & ent, std::u8string_view key)
	requires(std::is_same_v<Vec3, double3_array>) {
	return get_double3_for_key(ent, key);
}

std::optional<std::reference_wrapper<entity_t>>
find_target_entity(std::u8string_view target);
entity_t* EntityForModel(int modnum);

//
// Texture Related Stuff
//

extern std::ptrdiff_t g_max_map_miptex;
constexpr std::ptrdiff_t g_max_map_lightdata
	= std::numeric_limits<std::int32_t>::max();
extern void dtexdata_init();

extern wad_texture_name get_texture_by_number(texinfo_count texturenumber);

//
// BSP File Data
//

struct bsp_data final {
	std::array<dmodel_t, MAX_MAP_MODELS> mapModels{};
	std::uint32_t mapModelsLength{ 0 };

	std::array<std::byte, MAX_MAP_VISIBILITY> visData{};
	std::uint32_t visDataByteSize{ 0 };

	// This one can be resized and reallocated
	std::vector<std::byte> lightData{};

	// This one can perhaps not be resized and reallocated
	// Now it's always initialized with g_max_map_miptex 0s
	// TODO: See if that can be changed
	std::vector<std::byte> textureData{}; // (dmiptexlump_t)
	int textureDataByteSize{ 0 };

	std::array<char8_t, MAX_MAP_ENTSTRING> entityData{};
	std::uint32_t entityDataLength{ 0 };

	std::array<dleaf_t, MAX_MAP_LEAFS> leafs{};
	int leafsLength{ 0 };

	std::array<dplane_t, MAX_INTERNAL_MAP_PLANES> planes{};
	int planesLength{ 0 };

	std::array<dvertex_t, MAX_MAP_VERTS> vertexes{};
	int vertexesLength{ 0 };

	std::array<dnode_t, MAX_MAP_NODES> nodes{};
	int nodesLength{ 0 };

	std::array<texinfo_t, INITIAL_MAX_MAP_TEXINFO> texInfos{};
	texinfo_count texInfosLength{ 0 };

	std::array<dface_t, MAX_MAP_FACES> faces{};
	int facesLength{ 0 };

	// Doesn't belong here - it's not something that's written to the BSP
	int worldExtent{ 65536 }; // ENGINE_ENTITY_RANGE; // -worldextent

	std::array<dclipnode_t, MAX_MAP_CLIPNODES> clipNodes{};
	int clipNodesLength{ 0 };

	std::array<dedge_t, MAX_MAP_EDGES> edges{};
	int edgesLength{ 0 };

	std::array<std::uint16_t, MAX_MAP_MARKSURFACES> markSurfaces{};
	int markSurfacesLength{ 0 };

	std::array<std::int32_t, MAX_MAP_SURFEDGES> surfEdges{};
	int surfEdgesLength{ 0 };

	// Doesn't belong here - it's not something that's written directly to
	// the BSP
	std::array<entity_t, MAX_MAP_ENTITIES> entities{};
	entity_count entitiesLength;
};

extern bsp_data bspGlobals;

extern std::uint32_t& g_nummodels;
extern std::array<dmodel_t, MAX_MAP_MODELS>& g_dmodels;

extern std::uint32_t& g_visdatasize;
extern std::array<std::byte, MAX_MAP_VISIBILITY>& g_dvisdata;

extern std::vector<std::byte>& g_dlightdata;

extern int& g_texdatasize;
extern std::vector<std::byte>& g_dtexdata; // (dmiptexlump_t)

extern std::uint32_t& g_entdatasize;
extern std::array<char8_t, MAX_MAP_ENTSTRING>& g_dentdata;

extern int& g_numleafs;
extern std::array<dleaf_t, MAX_MAP_LEAFS>& g_dleafs;

extern int& g_numplanes;
extern std::array<dplane_t, MAX_INTERNAL_MAP_PLANES>& g_dplanes;

extern int& g_numvertexes;
extern std::array<dvertex_t, MAX_MAP_VERTS>& g_dvertexes;

extern int& g_numnodes;
extern std::array<dnode_t, MAX_MAP_NODES>& g_dnodes;

extern texinfo_count& g_numtexinfo;
extern std::array<texinfo_t, INITIAL_MAX_MAP_TEXINFO>& g_texinfo;

extern int& g_numfaces;
extern std::array<dface_t, MAX_MAP_FACES>& g_dfaces;

extern int& g_iWorldExtent;

extern int& g_numclipnodes;
extern std::array<dclipnode_t, MAX_MAP_CLIPNODES>& g_dclipnodes;

extern int& g_numedges;
extern std::array<dedge_t, MAX_MAP_EDGES>& g_dedges;

extern int& g_nummarksurfaces;
extern std::array<std::uint16_t, MAX_MAP_MARKSURFACES>& g_dmarksurfaces;

extern int& g_numsurfedges;
extern std::array<std::int32_t, MAX_MAP_SURFEDGES>& g_dsurfedges;

extern entity_count& g_numentities;
extern std::array<entity_t, MAX_MAP_ENTITIES>& g_entities;

extern void DecompressVis(
	byte const * src, byte* const dest, unsigned int const dest_length
);
extern int CompressVis(
	byte const * const src,
	unsigned int const src_length,
	byte* dest,
	unsigned int dest_length
);

extern void LoadBSPImage(dheader_t* header);
extern void LoadBSPFile(std::filesystem::path const & filename);
extern void WriteBSPFile(std::filesystem::path const & filename);
extern void WriteExtentFile(std::filesystem::path const & filename);
extern bool CalcFaceExtents_test();

struct face_extents {
	std::array<std::int32_t, 2> mins;
	std::array<std::int32_t, 2> maxs;
};

extern face_extents get_face_extents(int facenum) noexcept;
extern texinfo_count ParseTexinfoForFace(dface_t const & f);
extern void DeleteEmbeddedLightmaps();

std::size_t hash_data();
