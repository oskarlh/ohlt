#pragma once

#include "cmdlib.h"
#include "mathlib.h"
#include "wad_texture_name.h"

#include <cstddef>
#include <filesystem>
#include <vector>


// upper design bounds

constexpr std::ptrdiff_t MAX_MAP_HULLS = 4;
// hard limit

constexpr std::ptrdiff_t MAX_MAP_MODELS = 512; //400 //vluzacn
// variable, but 400 brush entities is very stressful on the engine and network code as it is

constexpr std::ptrdiff_t MAX_MAP_BRUSHES = 32768;
// arbitrary, but large numbers of brushes generally require more lightmap's than the compiler can handle

constexpr std::ptrdiff_t MAX_ENGINE_ENTITIES = 16384; //1024 //vluzacn
constexpr std::ptrdiff_t MAX_MAP_ENTITIES = 16384; //2048 //vluzacn
// hard limit, in actuallity it is too much, as temporary entities in the game plus static map entities can overflow

constexpr std::ptrdiff_t MAX_MAP_ENTSTRING = 2048 * 1024; //(512*1024) //vluzacn
// abitrary, 512Kb of string data should be plenty even with TFC FGD's

constexpr std::ptrdiff_t MAX_MAP_PLANES = 32768; // TODO: This can be larger, because although faces can only use plane 0~32767, clipnodes can use plane 0-65535. --vluzacn
constexpr std::ptrdiff_t MAX_INTERNAL_MAP_PLANES = 256 * 1024;
// (from email): I have been building a rather complicated map, and using your latest 
// tools (1.61) it seemed to compile fine.  However, in game, the engine was dropping
// a lot of faces from almost every FUNC_WALL, and also caused a strange texture 
// phenomenon in software mode (see attached screen shot).  When I compiled with v1.41,
// I noticed that it hit the MAX_MAP_PLANES limit of 32k.  After deleting some brushes
// I was able to bring the map under the limit, and all of the previous errors went away.

constexpr std::ptrdiff_t MAX_MAP_NODES = 32767;
// hard limit (negative short's are used as contents values)
constexpr std::ptrdiff_t MAX_MAP_CLIPNODES = 32767;
// hard limit (negative short's are used as contents values)

constexpr std::ptrdiff_t MAX_MAP_LEAFS = 32760;
constexpr std::ptrdiff_t MAX_MAP_LEAFS_ENGINE = 8192;
// No problem has been observed in testmap or reported, except when viewing the map from outside (some leafs missing, no crash).
// This problem indicates that engine's MAX_MAP_LEAFS is 8192 (for reason, see: Quake - gl_model.c - Mod_Init).
// I don't know if visleafs > 8192 will cause Mod_DecompressVis overflow.

constexpr std::ptrdiff_t MAX_MAP_VERTS = 65535;
constexpr std::ptrdiff_t MAX_MAP_FACES = 65535; // This ought to be 32768, otherwise faces(in world) can become invisible. --vluzacn
constexpr std::ptrdiff_t MAX_MAP_WORLDFACES = 32768;
constexpr std::ptrdiff_t MAX_MAP_MARKSURFACES = 65535;
// hard limit (data structures store them as unsigned shorts)

constexpr std::ptrdiff_t MAX_MAP_TEXTURES = 4096; //512 //vluzacn
// hard limit (halflife limitation) // I used 2048 different textures in a test map and everything looks fine in both opengl and d3d mode.

constexpr std::ptrdiff_t MAX_MAP_TEXINFO = 32767;
// hard limit (face.texinfo is signed short)
constexpr std::ptrdiff_t MAX_INTERNAL_MAP_TEXINFOS = 262144;


constexpr std::ptrdiff_t MAX_MAP_EDGES = 256000;
constexpr std::ptrdiff_t MAX_MAP_SURFEDGES = 512000;
// arbtirary

constexpr std::ptrdiff_t MAX_MAP_VISIBILITY = 0x800000;
// arbitrary

// these are for entity key:value pairs
constexpr std::ptrdiff_t MAX_KEY = 128; //32 //vluzacn
constexpr std::ptrdiff_t MAX_VAL = 4096; // the name used to be MAX_VALUE //vluzacn
// quote from yahn: 'probably can raise these values if needed'

// texture size limit

constexpr std::ptrdiff_t MAX_TEXTURE_SIZE = 348972; //Bytes in a 512x512 image((256 * 256 * sizeof(short) * 3) / 2) //stop compiler from warning 512*512 texture. --vluzacn
// this is arbitrary, and needs space for the largest realistic texture plus
// room for its mipmaps.'  This value is primarily used to catch damanged or invalid textures
// in a wad file

constexpr std::ptrdiff_t TEXTURE_STEP = 16; // this constant was previously defined in lightmap.cpp. --vluzacn
constexpr std::ptrdiff_t MAX_SURFACE_EXTENT = 16; // if lightmap extent exceeds 16, the map will not be able to load in 'Software' renderer and HLDS. //--vluzacn

constexpr double ENGINE_ENTITY_RANGE = 4096.0;
//=============================================================================

constexpr std::uint32_t BSPVERSION = 30;


//
// BSP File Structures
//


struct lump_t {
    std::uint32_t fileofs, filelen;
};

struct dmodel_t {
    float mins[3], maxs[3];
    float origin[3];
    std::int32_t headnode[MAX_MAP_HULLS];
    std::int32_t visleafs; // not including the solid leaf 0
    std::int32_t firstface, numfaces;
};


struct dmiptexlump_t {
    std::int32_t nummiptex;
    std::int32_t dataofs[4]; // [nummiptex]
};

constexpr std::size_t MIPLEVELS = 4; // Four mip maps stored
struct miptex_t {
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
		miptex_header_and_data(const miptex_header_and_data&) = delete;
		constexpr miptex_header_and_data(miptex_header_and_data&&) noexcept = default;

		constexpr miptex_header_and_data(std::size_t dataSize) {
			std::size_t totalSize = sizeof(miptex_t) + dataSize;
			headerAndData = std::make_unique_for_overwrite<std::byte[]>(totalSize);
			miptex_t& header = *new(headerAndData.get()) miptex_t{};
		}
		constexpr ~miptex_header_and_data() {
			header().~miptex_t();
		}

		miptex_header_and_data& operator=(const miptex_header_and_data&) = delete;
		constexpr miptex_header_and_data& operator=(miptex_header_and_data&&) noexcept = default;

		constexpr miptex_t& header() noexcept {
			return *(miptex_t*) headerAndData.get();
		}
		constexpr const miptex_t& header() const noexcept {
			return *(const miptex_t*) headerAndData.get();
		}
		constexpr std::byte* data() noexcept {
			return headerAndData.get() + sizeof(miptex_t);
		}
		constexpr const std::byte* data() const noexcept {
			return headerAndData.get() + sizeof(miptex_t);
		}
};

class miptex_header_and_data_view final {
	private:
		std::byte* headerAndData;
	public:
		miptex_header_and_data_view() = delete;
		constexpr miptex_header_and_data_view(std::byte* ptr) noexcept : headerAndData(ptr) {}
		constexpr miptex_header_and_data_view(const miptex_header_and_data_view&) noexcept = default;

		constexpr miptex_header_and_data_view& operator=(const miptex_header_and_data_view&) noexcept = default;

		constexpr miptex_t& header() noexcept {
			return *(miptex_t*) headerAndData;
		}
		constexpr const miptex_t& header() const noexcept {
			return *(const miptex_t*) headerAndData;
		}
		constexpr std::byte* data() noexcept {
			return headerAndData + sizeof(miptex_t);
		}
		constexpr const std::byte* data() const noexcept {
			return headerAndData + sizeof(miptex_t);
		}
};

struct dvertex_t {
    std::array<float, 3> point;
};

struct dplane_t {
    std::array<float, 3> normal;
    float dist;
    planetypes type;                                  // PLANE_X - PLANE_ANYZ ?remove? trivial to regenerate
};

enum contents_t {
    CONTENTS_EMPTY = -1,
    CONTENTS_SOLID = -2,
    CONTENTS_WATER = -3,
    CONTENTS_SLIME = -4,
    CONTENTS_LAVA = -5,
    CONTENTS_SKY = -6,
    CONTENTS_ORIGIN = -7,                                  // removed at csg time

    CONTENTS_CURRENT_0 = -9,
    CONTENTS_CURRENT_90 = -10,
    CONTENTS_CURRENT_180 = -11,
    CONTENTS_CURRENT_270 = -12,
    CONTENTS_CURRENT_UP = -13,
    CONTENTS_CURRENT_DOWN = -14,

    CONTENTS_TRANSLUCENT = -15,
    CONTENTS_HINT = -16,     // Filters down to CONTENTS_EMPTY by bsp, ENGINE SHOULD NEVER SEE THIS

    CONTENTS_NULL = -17,     // AJM  // removed in csg and bsp, VIS or RAD shouldnt have to deal with this, only clip planes!


	CONTENTS_BOUNDINGBOX = -19, // similar to CONTENTS_ORIGIN

	CONTENTS_TOEMPTY = -32,
};

struct dnode_t {
    std::int32_t planenum;
    std::int16_t children[2]; // Negative numbers are -(leafs+1), not nodes
    std::int16_t mins[3]; // For sphere culling
    std::int16_t maxs[3];
    std::uint16_t firstface;
    std::uint16_t numfaces; // Counting both sides
};

struct dclipnode_t {
    std::int32_t planenum;
    std::int16_t children[2]; // Negative numbers are contents
};

struct texinfo_t {
    float vecs[2][4]; // [s/t][xyz offset]
    std::int32_t miptex;
    std::uint32_t flags;
};
// texinfo.flags only has one flag supported by the engine:
constexpr std::uint32_t TEX_SPECIAL = 1; // Sky or slime or null, no lightmap or 256 subdivision

// note that edge 0 is never used, because negative edge nums are used for
// counterclockwise use of the edge in a face
struct dedge_t {
    std::uint16_t  v[2];                                  // vertex numbers
};

#define MAXLIGHTMAPS    4
struct dface_t {
    std::uint16_t	planenum;
    std::int16_t           side;

    std::int32_t             firstedge;                             // we must support > 64k edges
    std::int16_t           numedges;
    std::int16_t           texinfo;

    // lighting info
    std::uint8_t            styles[MAXLIGHTMAPS];
    std::int32_t             lightofs;                              // start of [numstyles*surfsize] samples
};

#define AMBIENT_WATER   0
#define AMBIENT_SKY     1
#define AMBIENT_SLIME   2
#define AMBIENT_LAVA    3


// leaf 0 is the generic CONTENTS_SOLID leaf, used for all solid areas
// all other leafs need visibility info
struct dleaf_t {
    std::int32_t contents;
    std::int32_t visofs; // -1 = no visibility info

    // For frustum culling
    std::array<std::int16_t, 3> mins;
    std::array<std::int16_t, 3> maxs;

    std::uint16_t firstmarksurface;
    std::uint16_t nummarksurfaces;

    std::array<std::uint8_t, 4> unused_ambient_level{}; // Used by Quake, not by Half-Life
};


// TODO: Automatically swap the ENTITIES and PLANES lump headers
// when compiling Blue Shift maps. Swapped ENTITIES and PLANES are
// the ONLY difference between Blue Shift BSPs and Half-Life BSPs
constexpr std::size_t LUMP_ENTITIES     = 0;
constexpr std::size_t LUMP_PLANES       = 1;
constexpr std::size_t LUMP_TEXTURES     = 2;
constexpr std::size_t LUMP_VERTEXES     = 3;
constexpr std::size_t LUMP_VISIBILITY   = 4;
constexpr std::size_t LUMP_NODES        = 5;
constexpr std::size_t LUMP_TEXINFO      = 6;
constexpr std::size_t LUMP_FACES        = 7;
constexpr std::size_t LUMP_LIGHTING     = 8;
constexpr std::size_t LUMP_CLIPNODES    = 9;
constexpr std::size_t LUMP_LEAFS        = 10;
constexpr std::size_t LUMP_MARKSURFACES = 11;
constexpr std::size_t LUMP_EDGES        = 12;
constexpr std::size_t LUMP_SURFEDGES    = 13;
constexpr std::size_t LUMP_MODELS       = 14;


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

template<lump_id Id> struct lump_element_type_map {};
template<> struct lump_element_type_map<lump_id::entities> { using type = char8_t; };
template<> struct lump_element_type_map<lump_id::planes> { using type = dplane_t; };
template<> struct lump_element_type_map<lump_id::textures> { using type = std::byte; };
template<> struct lump_element_type_map<lump_id::vertexes> { using type = dvertex_t; };
template<> struct lump_element_type_map<lump_id::visibility> { using type = std::byte; };
template<> struct lump_element_type_map<lump_id::nodes> { using type = dnode_t; };
template<> struct lump_element_type_map<lump_id::texinfo> { using type = texinfo_t; };
template<> struct lump_element_type_map<lump_id::faces> { using type = dface_t; };
template<> struct lump_element_type_map<lump_id::lighting> { using type = std::byte; };
template<> struct lump_element_type_map<lump_id::clipnodes> { using type = dclipnode_t; };
template<> struct lump_element_type_map<lump_id::leafs> { using type = dleaf_t; };
template<> struct lump_element_type_map<lump_id::marksurfaces> { using type = std::uint16_t; };
template<> struct lump_element_type_map<lump_id::edges> { using type = dedge_t; };
template<> struct lump_element_type_map<lump_id::surfedges> { using type = std::int32_t; };
template<> struct lump_element_type_map<lump_id::models> { using type = dmodel_t; };

template <lump_id Id> using lump_element_type = lump_element_type_map<Id>::type;


struct dheader_t {
    std::uint32_t version;
    lump_t lumps[num_lump_types];
};

//============================================================================

#define ANGLE_UP    -1.0 //#define ANGLE_UP    -1 //--vluzacn
#define ANGLE_DOWN  -2.0 //#define ANGLE_DOWN  -2 //--vluzacn

//
// Entity Related Stuff
//

struct epair_t {
    epair_t* next = nullptr;
    std::u8string key;
    std::u8string value;
};

struct entity_t {
    vec3_array          origin;
    int             firstbrush;
    int             numbrushes;
    epair_t*        epairs;
};

extern void            ParseEntities();
extern void            UnparseEntities();

extern void DeleteAllKeys(entity_t* ent);
extern void            DeleteKey(entity_t* ent, std::u8string_view key);
extern void            SetKeyValue(entity_t* ent, std::u8string_view key, std::u8string_view value);
extern const char8_t* ValueForKey(const entity_t* const ent, std::u8string_view key);
std::u8string_view value_for_key(const entity_t* const ent, std::u8string_view key);
bool key_value_is_not_empty(const entity_t* const ent, std::u8string_view key);
bool key_value_is_empty(const entity_t* const ent, std::u8string_view key);
bool key_value_is(const entity_t* const ent, std::u8string_view key, std::u8string_view value);
bool key_value_starts_with(const entity_t* const ent, std::u8string_view key, std::u8string_view prefix);
bool classname_is(const entity_t* const ent, std::u8string_view classname);

extern int             IntForKey(const entity_t* const ent, std::u8string_view key);
extern vec_t           FloatForKey(const entity_t* const ent, std::u8string_view key);
extern void            GetVectorForKey(const entity_t* const ent, std::u8string_view key, vec3_array& vec);

extern entity_t* FindTargetEntity(std::u8string_view target);
extern std::unique_ptr<epair_t> ParseEpair();
extern entity_t* EntityForModel(int modnum);

//
// Texture Related Stuff
//

extern std::ptrdiff_t g_max_map_miptex;
constexpr std::ptrdiff_t g_max_map_lightdata = std::numeric_limits<std::int32_t>::max();
extern void dtexdata_init();
extern void dtexdata_free();

extern wad_texture_name get_texture_by_number(int texturenumber);


//
// BSP File Data
//

struct bsp_data {
	std::array<dmodel_t, MAX_MAP_MODELS> mapModels{};
	std::uint32_t mapModelsChecksum{0};
	int mapModelsLength{0};

	std::array<std::byte, MAX_MAP_VISIBILITY> visData{};
	int visDataByteSize{0};
	std::uint32_t visDataChecksum{0};

    // This one can be resized and reallocated
	std::vector<std::byte> lightData{};
	std::uint32_t lightDataChecksum{0};

    // This one can perhaps not be resized and reallocated
    // Now it's always initialized with g_max_map_miptex 0s
    // TODO: See if that can be changed
	std::vector<std::byte> textureData{}; // (dmiptexlump_t)
	int textureDataByteSize{0};
	std::uint32_t textureDataChecksum{0};

	std::array<char8_t, MAX_MAP_ENTSTRING> entityData{};
	std::uint32_t entityDataChecksum{0};
	int entityDataLength{0};

	std::array<dleaf_t, MAX_MAP_LEAFS> leafs{};
	std::uint32_t leafsChecksum{0};
	int leafsLength{0};

	std::array<dplane_t, MAX_INTERNAL_MAP_PLANES> planes{};
	std::uint32_t planesChecksum{0};
	int planesLength{0};

	std::array<dvertex_t, MAX_MAP_VERTS> vertexes{};
	std::uint32_t vertexesChecksum{0};
	int vertexesLength{0};

	std::array<dnode_t, MAX_MAP_NODES> nodes{};
	std::uint32_t nodesChecksum{0};
	int nodesLength{0};

    std::array<texinfo_t, MAX_INTERNAL_MAP_TEXINFOS> texInfos{};
    std::uint32_t texInfosChecksum{0};
    int texInfosLength{0};

	std::array<dface_t, MAX_MAP_FACES> faces{};
	std::uint32_t facesChecksum{0};
	int facesLength{0};

	int worldExtent{65536}; // ENGINE_ENTITY_RANGE; // -worldextent // seedee

    std::array<dclipnode_t, MAX_MAP_CLIPNODES> clipNodes{};
    std::uint32_t clipNodesChecksum{0};
    int clipNodesLength{0};

	std::array<dedge_t, MAX_MAP_EDGES> edges{};
	std::uint32_t edgesChecksum{0};
	int edgesLength{0};

	std::array<std::uint16_t, MAX_MAP_MARKSURFACES> markSurfaces{};
	std::uint32_t markSurfacesChecksum{0};
	int markSurfacesLength{0};

    std::array<std::int32_t, MAX_MAP_SURFEDGES> surfEdges{};
    std::uint32_t surfEdgesChecksum{0};
    int surfEdgesLength{0};

	std::array<entity_t, MAX_MAP_ENTITIES> entities{};
	int entitiesLength;
};

extern bsp_data bspGlobals;

extern int& g_nummodels;
extern std::array<dmodel_t, MAX_MAP_MODELS>& g_dmodels;
extern std::uint32_t& g_dmodels_checksum;

extern int& g_visdatasize;
extern std::array<std::byte, MAX_MAP_VISIBILITY>& g_dvisdata;
extern std::uint32_t& g_dvisdata_checksum;

extern std::vector<std::byte>& g_dlightdata;
extern std::uint32_t& g_dlightdata_checksum;

extern int& g_texdatasize;
extern std::vector<std::byte>& g_dtexdata; // (dmiptexlump_t)
extern std::uint32_t& g_dtexdata_checksum;

extern int& g_entdatasize;
extern std::array<char8_t, MAX_MAP_ENTSTRING>& g_dentdata;
extern std::uint32_t& g_dentdata_checksum;

extern int& g_numleafs;
extern std::array<dleaf_t, MAX_MAP_LEAFS>& g_dleafs;
extern std::uint32_t& g_dleafs_checksum;

extern int& g_numplanes;
extern std::array<dplane_t, MAX_INTERNAL_MAP_PLANES>& g_dplanes;
extern std::uint32_t& g_dplanes_checksum;

extern int& g_numvertexes;
extern std::array<dvertex_t, MAX_MAP_VERTS>& g_dvertexes;
extern std::uint32_t& g_dvertexes_checksum;

extern int& g_numnodes;
extern std::array<dnode_t, MAX_MAP_NODES>& g_dnodes;
extern std::uint32_t& g_dnodes_checksum;

extern int& g_numtexinfo;
extern std::array<texinfo_t, MAX_INTERNAL_MAP_TEXINFOS>& g_texinfo;
extern std::uint32_t& g_texinfo_checksum;

extern int& g_numfaces;
extern std::array<dface_t, MAX_MAP_FACES>& g_dfaces;
extern std::uint32_t& g_dfaces_checksum;

extern int& g_iWorldExtent;

extern int& g_numclipnodes;
extern std::array<dclipnode_t, MAX_MAP_CLIPNODES>& g_dclipnodes;
extern std::uint32_t& g_dclipnodes_checksum;

extern int& g_numedges;
extern std::array<dedge_t, MAX_MAP_EDGES>& g_dedges;
extern std::uint32_t& g_dedges_checksum;

extern int& g_nummarksurfaces;
extern std::array<std::uint16_t, MAX_MAP_MARKSURFACES>& g_dmarksurfaces;
extern std::uint32_t& g_dmarksurfaces_checksum;

extern int& g_numsurfedges;
extern std::array<std::int32_t, MAX_MAP_SURFEDGES>& g_dsurfedges;
extern std::uint32_t& g_dsurfedges_checksum;


extern int& g_numentities;
extern std::array<entity_t, MAX_MAP_ENTITIES>& g_entities;

extern void     DecompressVis(const byte* src, byte* const dest, const unsigned int dest_length);
extern int      CompressVis(const byte* const src, const unsigned int src_length, byte* dest, unsigned int dest_length);

extern void     LoadBSPImage(dheader_t* header);
extern void     LoadBSPFile(const std::filesystem::path& filename);
extern void     WriteBSPFile(const std::filesystem::path& filename);
extern void		WriteExtentFile (const std::filesystem::path& filename);
extern bool		CalcFaceExtents_test ();
extern void		GetFaceExtents (int facenum, int mins_out[2], int maxs_out[2]);
extern int		ParseImplicitTexinfoFromTexture (int miptex);
extern int		ParseTexinfoForFace (const dface_t *f);
extern void		DeleteEmbeddedLightmaps ();
