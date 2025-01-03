#include <bit>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <numbers>
#include <ranges>
#include <span>
#include <type_traits>

#include "cmdlib.h"
#include "filelib.h"
#include "messages.h"
#include "hlassert.h"
#include "log.h"
#include "mathlib.h"
#include "bspfile.h"
#include "scriplib.h"
#include "blockmem.h"
#include "cli_option_defaults.h"


using namespace std::literals;

//=============================================================================

std::ptrdiff_t g_max_map_miptex = cli_option_defaults::max_map_miptex;

bsp_data bspGlobals;

int& g_nummodels{bspGlobals.mapModelsLength};
std::array<dmodel_t, MAX_MAP_MODELS>& g_dmodels{bspGlobals.mapModels};
std::uint32_t& g_dmodels_checksum{bspGlobals.mapModelsChecksum};

int& g_visdatasize{bspGlobals.visDataByteSize};
std::array<std::byte, MAX_MAP_VISIBILITY>& g_dvisdata{bspGlobals.visData};
std::uint32_t& g_dvisdata_checksum{bspGlobals.visDataChecksum};

std::vector<std::byte>& g_dlightdata{bspGlobals.lightData};
std::uint32_t& g_dlightdata_checksum{bspGlobals.lightDataChecksum};

int& g_texdatasize{bspGlobals.textureDataByteSize};
std::vector<std::byte>& g_dtexdata{bspGlobals.textureData};                                  // (dmiptexlump_t)
std::uint32_t& g_dtexdata_checksum{bspGlobals.textureDataChecksum};

int& g_entdatasize{bspGlobals.entityDataLength};
std::array<char8_t, MAX_MAP_ENTSTRING>& g_dentdata{bspGlobals.entityData};
std::uint32_t& g_dentdata_checksum{bspGlobals.entityDataChecksum};

int& g_numleafs{bspGlobals.leafsLength};
std::array<dleaf_t, MAX_MAP_LEAFS>& g_dleafs{bspGlobals.leafs};
std::uint32_t& g_dleafs_checksum{bspGlobals.leafsChecksum};

int& g_numplanes{bspGlobals.planesLength};
std::array<dplane_t, MAX_INTERNAL_MAP_PLANES>& g_dplanes{bspGlobals.planes};
std::uint32_t& g_dplanes_checksum{bspGlobals.planesChecksum};

int& g_numvertexes{bspGlobals.vertexesLength};
std::array<dvertex_t, MAX_MAP_VERTS>& g_dvertexes{bspGlobals.vertexes};
std::uint32_t& g_dvertexes_checksum{bspGlobals.vertexesChecksum};

int& g_numnodes{bspGlobals.nodesLength};
std::array<dnode_t, MAX_MAP_NODES>& g_dnodes{bspGlobals.nodes};
std::uint32_t& g_dnodes_checksum{bspGlobals.entityDataChecksum};

int& g_numtexinfo{bspGlobals.texInfosLength};
std::array<texinfo_t, MAX_INTERNAL_MAP_TEXINFOS>& g_texinfo{bspGlobals.texInfos};
std::uint32_t& g_texinfo_checksum{bspGlobals.texInfosChecksum};

int& g_numfaces{bspGlobals.facesLength};
std::array<dface_t, MAX_MAP_FACES>& g_dfaces{bspGlobals.faces};
std::uint32_t& g_dfaces_checksum{bspGlobals.facesChecksum};

int& g_iWorldExtent{bspGlobals.worldExtent}; // ENGINE_ENTITY_RANGE; // -worldextent // seedee

int& g_numclipnodes{bspGlobals.clipNodesLength};
std::array<dclipnode_t, MAX_MAP_CLIPNODES>& g_dclipnodes{bspGlobals.clipNodes};
std::uint32_t& g_dclipnodes_checksum{bspGlobals.clipNodesChecksum};

int& g_numedges{bspGlobals.edgesLength};
std::array<dedge_t, MAX_MAP_EDGES>& g_dedges{bspGlobals.edges};
std::uint32_t& g_dedges_checksum{bspGlobals.edgesChecksum};

int& g_nummarksurfaces{bspGlobals.markSurfacesLength};
std::array<std::uint16_t, MAX_MAP_MARKSURFACES>& g_dmarksurfaces{bspGlobals.markSurfaces};
std::uint32_t& g_dmarksurfaces_checksum{bspGlobals.markSurfacesChecksum};

int& g_numsurfedges{bspGlobals.surfEdgesLength};
std::array<std::int32_t, MAX_MAP_SURFEDGES>& g_dsurfedges{bspGlobals.surfEdges};
std::uint32_t& g_dsurfedges_checksum{bspGlobals.surfEdgesChecksum};

int& g_numentities{bspGlobals.entitiesLength};
std::array<entity_t, MAX_MAP_ENTITIES>& g_entities{bspGlobals.entities};

/*
 * ===============
 * fast_checksum
 * ===============
 */

static std::uint32_t rotleftu32(std::uint32_t value, std::uint32_t amt)
{
    std::uint32_t t1, t2;

    t1 = value >> (std::numeric_limits<std::uint32_t>::digits - amt);

    t2 = value << amt;
    return (t1 | t2);
}


template<class T> static std::uint32_t fast_checksum(std::span<T> elements)
{
	// TODO: Find a better way to hash that gives us the same output on all platforms.
	// This assertion fails for two possible reasons:
	// 1) NaN floats have can have different bit representations
	// 2) struct padding
	// static_assert(std::has_unique_object_representations_v<T>);

	struct element_as_bytes {
		unsigned char bytes[sizeof(T)];
	};

    std::uint32_t checksum = 0;
    for (const T& element : elements)
    {
		for(unsigned char byteInElement : std::bit_cast<element_as_bytes>(element).bytes) {
	        checksum = rotleftu32(checksum, 4) ^ byteInElement;
		}
    }

    return checksum;
}


/*
 * ===============
 * CompressVis
 * ===============
 */
int             CompressVis(const byte* const src, const unsigned int src_length, byte* dest, unsigned int dest_length)
{
    unsigned int    j;
    byte*           dest_p = dest;
    unsigned int    current_length = 0;

    for (j = 0; j < src_length; j++)
    {
        current_length++;
        hlassume(current_length <= dest_length, assume_COMPRESSVIS_OVERFLOW);

        *dest_p = src[j];
        dest_p++;

        if (src[j])
        {
            continue;
        }

        unsigned char   rep = 1;

        for (j++; j < src_length; j++)
        {
            if (src[j] || rep == 255)
            {
                break;
            }
            else
            {
                rep++;
            }
        }
        current_length++;
        hlassume(current_length <= dest_length, assume_COMPRESSVIS_OVERFLOW);

        *dest_p = rep;
        dest_p++;
        j--;
    }

    return dest_p - dest;
}

// =====================================================================================
//  DecompressVis
//      
// =====================================================================================
void            DecompressVis(const byte* src, byte* const dest, const unsigned int dest_length)
{
    unsigned int    current_length = 0;
    int             c;
    byte*           out;
    int             row;

	row = (g_dmodels[0].visleafs + 7) >> 3; // same as the length used by VIS program in CompressVis
	// The wrong size will cause DecompressVis to spend extremely long time once the source pointer runs into the invalid area in g_dvisdata (for example, in BuildFaceLights, some faces could hang for a few seconds), and sometimes to crash.

    out = dest;

    do
	{
		hlassume (src - (byte*)g_dvisdata.data() < g_visdatasize, assume_DECOMPRESSVIS_OVERFLOW);

        if (*src)
        {
            current_length++;
            hlassume(current_length <= dest_length, assume_DECOMPRESSVIS_OVERFLOW);

            *out = *src;
            out++;
            src++;
            continue;
        }
		
		hlassume (&src[1] - (byte*)g_dvisdata.data() < g_visdatasize, assume_DECOMPRESSVIS_OVERFLOW);

        c = src[1];
        src += 2;
        while (c)
        {
            current_length++;
            hlassume(current_length <= dest_length, assume_DECOMPRESSVIS_OVERFLOW);

            *out = 0;
            out++;
            c--;

            if (out - dest >= row)
            {
                return;
            }
        }
    }
    while (out - dest < row);
}

//
// =====================================================================================
//


// =====================================================================================
//  CopyLump
//      balh
// =====================================================================================
static int      CopyLump(int lump, void* dest, int size, const dheader_t* const header)
{
    int             length, ofs;

    length = header->lumps[lump].filelen;
    ofs = header->lumps[lump].fileofs;

    if (length % size)
    {
        Error("LoadBSPFile: odd lump size");
    }

    std::memcpy(dest, (byte*) header + ofs, length);

    return length / size;
}

template <lump_id LumpId>
static std::span<const lump_element_type<LumpId>> get_lump_data(const dheader_t* header)
{
	using lump_element = lump_element_type<LumpId>;

    std::uint32_t length = header->lumps[std::size_t(LumpId)].filelen;
    std::uint32_t ofs = header->lumps[std::size_t(LumpId)].fileofs;

    if (length % sizeof(lump_element))
    {
        Error("LoadBSPFile: odd lump size");
    }
	
	// Special handling for tex and lightdata to keep things from exploding
	if (LumpId == lump_id::textures) {
		hlassume(g_max_map_miptex > length, assume_MAX_MAP_MIPTEX);
	} else if(LumpId == lump_id::lighting) {
		hlassume(g_max_map_lightdata > length,assume_MAX_MAP_LIGHTING);
	}

	const std::byte* start = (const std::byte*) header + ofs;
	const std::byte* end = start + length;
	const std::span<const lump_element> data((const lump_element*) start, length / sizeof(lump_element));
	return data;
}


// =====================================================================================
//  LoadBSPFile
//      balh
// =====================================================================================
void            LoadBSPFile(const std::filesystem::path& filename)
{
    dheader_t* header;
    LoadFile(filename, (char**)&header);
    LoadBSPImage(header);
}

// =====================================================================================
//  LoadBSPImage
//      balh
// =====================================================================================
void            LoadBSPImage(dheader_t* const header)
{
    unsigned int     i;

    // swap the header
    for (i = 0; i < sizeof(dheader_t) / 4; i++)
    {
        ((int*)header)[i] = (((int*)header)[i]);
    }

    if (header->version != BSPVERSION)
    {
        Error("BSP is version %i, not %i", header->version, BSPVERSION);
    }

	auto modelData = get_lump_data<lump_id::models>(header);
	memcpy(g_dmodels.data(), modelData.data(), modelData.size() * sizeof(modelData[0]));
	g_nummodels = modelData.size();

    g_numvertexes = CopyLump(LUMP_VERTEXES, g_dvertexes.data(), sizeof(dvertex_t), header);
    g_numplanes = CopyLump(LUMP_PLANES, g_dplanes.data(), sizeof(dplane_t), header);
    g_numleafs = CopyLump(LUMP_LEAFS, g_dleafs.data(), sizeof(dleaf_t), header);
    g_numnodes = CopyLump(LUMP_NODES, g_dnodes.data(), sizeof(dnode_t), header);
    g_numtexinfo = CopyLump(LUMP_TEXINFO, g_texinfo.data(), sizeof(texinfo_t), header);
    g_numclipnodes = CopyLump(LUMP_CLIPNODES, g_dclipnodes.data(), sizeof(dclipnode_t), header);
    g_numfaces = CopyLump(LUMP_FACES, g_dfaces.data(), sizeof(dface_t), header);
    g_nummarksurfaces = CopyLump(LUMP_MARKSURFACES, g_dmarksurfaces.data(), sizeof(g_dmarksurfaces[0]), header);
    g_numsurfedges = CopyLump(LUMP_SURFEDGES, g_dsurfedges.data(), sizeof(g_dsurfedges[0]), header);
    g_numedges = CopyLump(LUMP_EDGES, g_dedges.data(), sizeof(dedge_t), header);

	auto textureData = get_lump_data<lump_id::textures>(header);
	memcpy(g_dtexdata.data(), textureData.data(), textureData.size() * sizeof(textureData[0]));
	g_texdatasize = textureData.size();

    g_visdatasize = CopyLump(LUMP_VISIBILITY, g_dvisdata.data(), 1, header);

	auto lightingData = get_lump_data<lump_id::lighting>(header);

// https://en.cppreference.com/w/cpp/feature_test#cpp_lib_containers_ranges
// Fingers crossed for GCC 15
#ifdef __cpp_lib_containers_ranges
	g_dlightdata.assign_range(lightingData);
#else
	g_dlightdata = lightingData | std::ranges::to<std::vector>();
#endif


    g_entdatasize = CopyLump(LUMP_ENTITIES, g_dentdata.data(), 1, header);

    Free(header);                                          // everything has been copied out

	// WTF???? THESE ARE UNUSED!
    g_dmodels_checksum = fast_checksum(std::span(g_dmodels.data(), g_nummodels));
    g_dvertexes_checksum = fast_checksum(std::span(g_dvertexes.data(), g_numvertexes));
    g_dplanes_checksum = fast_checksum(std::span(g_dplanes.data(), g_numplanes));
    g_dleafs_checksum = fast_checksum(std::span(g_dleafs.data(), g_numleafs));
    g_dnodes_checksum = fast_checksum(std::span(g_dnodes.data(), g_numnodes));
    g_texinfo_checksum = fast_checksum(std::span(g_texinfo.data(), g_numtexinfo));
    g_dclipnodes_checksum = fast_checksum(std::span(g_dclipnodes.data(), g_numclipnodes));
    g_dfaces_checksum = fast_checksum(std::span(g_dfaces.data(), g_numfaces));
    g_dmarksurfaces_checksum = fast_checksum(std::span(g_dmarksurfaces.data(), g_nummarksurfaces));
    g_dsurfedges_checksum = fast_checksum(std::span(g_dsurfedges.data(), g_numsurfedges));
    g_dedges_checksum = fast_checksum(std::span(g_dedges.data(), g_numedges));
    g_dtexdata_checksum = fast_checksum(std::span(g_dtexdata.data(), g_texdatasize));
    g_dvisdata_checksum = fast_checksum(std::span(g_dvisdata.data(), g_visdatasize));
    g_dlightdata_checksum = fast_checksum(std::span(g_dlightdata));
    g_dentdata_checksum = fast_checksum(std::span(g_dentdata.data(), g_entdatasize));
}

//
// =====================================================================================
//

// =====================================================================================
//  add_lump
// =====================================================================================


template<lump_id LumpId> void add_lump(std::span<const lump_element_type<LumpId>> data, dheader_t* header, FILE* bspfile)
{
//	Log("CHECKSUM %s%u %u\n",
//		g_Program,
//		(int) LumpId,
//		fast_checksum(data)
//	);

	using element_type = lump_element_type<LumpId>;
	const std::size_t byteLength = data.size() * sizeof(element_type);
    lump_t* lump = &header->lumps[std::size_t(LumpId)];
    lump->fileofs = ftell(bspfile);
    lump->filelen = byteLength;
    SafeWrite(bspfile, (const char*) data.data(), byteLength);


	// Why do we need padding??? Does the game need it?
	if(byteLength % 4) {
		const std::size_t paddingLength = 4 - (byteLength % 4);
		const std::array<std::byte, 3> zeroPadding{};
	    SafeWrite(bspfile, zeroPadding.data(), paddingLength);
	}
}

// =====================================================================================
//  WriteBSPFile
//      Swaps the bsp file in place, so it should not be referenced again
// =====================================================================================
void            WriteBSPFile(const std::filesystem::path& filename)
{
    dheader_t       outheader;
    dheader_t*      header;
    FILE*           bspfile;

    header = &outheader;
    memset(header, 0, sizeof(dheader_t));

    header->version = (BSPVERSION);

    bspfile = SafeOpenWrite(filename.c_str());
    SafeWrite(bspfile, header, sizeof(dheader_t));         // overwritten later

    //      LUMP TYPE       DATA            LENGTH                              HEADER  BSPFILE   
    add_lump<lump_id::planes>(std::span(g_dplanes.data(), g_numplanes), header, bspfile);
    add_lump<lump_id::leafs>(std::span(g_dleafs.data(), g_numleafs), header, bspfile);
    add_lump<lump_id::vertexes>(std::span(g_dvertexes.data(), g_numvertexes), header, bspfile);
    add_lump<lump_id::nodes>(std::span(g_dnodes.data(), g_numnodes), header, bspfile);
    add_lump<lump_id::texinfo>(std::span(g_texinfo.data(), g_numtexinfo), header, bspfile);
    add_lump<lump_id::faces>(std::span(g_dfaces.data(), g_numfaces), header, bspfile);
    add_lump<lump_id::clipnodes>(std::span(g_dclipnodes.data(), g_numclipnodes), header, bspfile);

    add_lump<lump_id::marksurfaces>(std::span(g_dmarksurfaces.data(), g_nummarksurfaces), header, bspfile);
    add_lump<lump_id::surfedges>(std::span(g_dsurfedges.data(), g_numsurfedges), header, bspfile);
    add_lump<lump_id::edges>(std::span(g_dedges.data(), g_numedges),       header, bspfile);
    add_lump<lump_id::models>(std::span(g_dmodels.data(), g_nummodels), header, bspfile);

    add_lump<lump_id::lighting>(g_dlightdata, header, bspfile);
    add_lump<lump_id::visibility>(std::span(g_dvisdata.data(), g_visdatasize), header, bspfile);
    add_lump<lump_id::entities>(std::span(g_dentdata.data(), g_entdatasize), header, bspfile);
    add_lump<lump_id::textures>(std::span(g_dtexdata.data(), g_texdatasize), header, bspfile);

    fseek(bspfile, 0, SEEK_SET);
    SafeWrite(bspfile, header, sizeof(dheader_t));

    fclose(bspfile);
}


// =====================================================================================
//  GetFaceExtents
// =====================================================================================

float CalculatePointVecsProduct (const volatile float *point, const volatile float *vecs)
{
	volatile double val;
	volatile double tmp;

	val = (double)point[0] * (double)vecs[0]; // always do one operation at a time and save to memory
	tmp = (double)point[1] * (double)vecs[1];
	val = val + tmp;
	tmp = (double)point[2] * (double)vecs[2];
	val = val + tmp;
	val = val + (double)vecs[3];

	return (float)val;
}

bool CalcFaceExtents_test ()
{
	const int numtestcases = 6;
	volatile float testcases[numtestcases][8] = {
		{1, 1, 1, 1, 0.375 * std::numeric_limits<double>::epsilon(), 0.375 * std::numeric_limits<double>::epsilon(), -1, 0},
		{1, 1, 1, 0.375 * std::numeric_limits<double>::epsilon(), 0.375 * std::numeric_limits<double>::epsilon(), 1, -1, std::numeric_limits<double>::epsilon()},
		{std::numeric_limits<double>::epsilon(), std::numeric_limits<double>::epsilon(), 1, 0.375, 0.375, 1, -1, std::numeric_limits<double>::epsilon()},
		{1, 1, 1, 1, 1, 0.375 * std::numeric_limits<float>::epsilon(), -2, 0.375 * std::numeric_limits<float>::epsilon()},
		{1, 1, 1, 1, 0.375 * std::numeric_limits<float>::epsilon(), 1, -2, 0.375 * std::numeric_limits<float>::epsilon()},
		{1, 1, 1, 0.375 * std::numeric_limits<float>::epsilon(), 1, 1, -2, 0.375 * std::numeric_limits<float>::epsilon()}};
	bool ok;

	// If the test failed, please check:
	//   1. whether the calculation is performed on FPU
	//   2. whether the register precision is too low

	ok = true;
	for (int i = 0; i < 6; i++)
	{
		float val = CalculatePointVecsProduct (&testcases[i][0], &testcases[i][3]);
		if (val != testcases[i][7])
		{
			Warning ("internal error: CalcFaceExtents_test failed on case %d (%.20f != %.20f).", i, val, testcases[i][7]);
			ok = false;
		}
	}
	return ok;
}

void GetFaceExtents (int facenum, int mins_out[2], int maxs_out[2])
{
	dface_t *f;
	float mins[2], maxs[2], val;
	int i, j, e;
	dvertex_t *v;
	texinfo_t *tex;
	int bmins[2], bmaxs[2];

	f = &g_dfaces[facenum];

	mins[0] = mins[1] = 999999;
	maxs[0] = maxs[1] = -99999;

	tex = &g_texinfo[ParseTexinfoForFace (f)];

	for (i = 0; i < f->numedges; i++)
	{
		e = g_dsurfedges[f->firstedge + i];
		if (e >= 0)
		{
			v = &g_dvertexes[g_dedges[e].v[0]];
		}
		else
		{
			v = &g_dvertexes[g_dedges[-e].v[1]];
		}
		for (j = 0; j < 2; j++)
		{
			// The old code: val = v->point[0] * tex->vecs[j][0] + v->point[1] * tex->vecs[j][1] + v->point[2] * tex->vecs[j][2] + tex->vecs[j][3];
			//   was meant to be compiled for x86 under MSVC (prior to VS 11), so the intermediate values were stored as 64-bit double by default.
			// The new code will produce the same result as the old code, but it's portable for different platforms.
			// See this article for details: Intermediate Floating-Point Precision by Bruce-Dawson http://www.altdevblogaday.com/2012/03/22/intermediate-floating-point-precision/

			// The essential reason for having this ugly code is to get exactly the same value as the counterpart of game engine.
			// The counterpart of game engine is the function CalcFaceExtents in HLSDK.
			// So we must also know how Valve compiles HLSDK. I think Valve compiles HLSDK with VC6.0 in the past.
			val = CalculatePointVecsProduct (v->point.data(), tex->vecs[j]);
			if (val < mins[j])
			{
				mins[j] = val;
			}
			if (val > maxs[j])
			{
				maxs[j] = val;
			}
		}
	}

	for (i = 0; i < 2; i++)
	{
		bmins[i] = (int)floor (mins[i] / TEXTURE_STEP);
		bmaxs[i] = (int)ceil (maxs[i] / TEXTURE_STEP);
	}

	for (i = 0; i < 2; i++)
	{
		mins_out[i] = bmins[i];
		maxs_out[i] = bmaxs[i];
	}
}

// =====================================================================================
//  WriteExtentFile
// =====================================================================================
void WriteExtentFile (const std::filesystem::path& filename)
{
	FILE *f;
	f = fopen (filename.c_str(), "w");
	if (!f)
	{
		Error ("Error opening %s: %s", filename.c_str(), strerror(errno));
	}
	fprintf (f, "%i\n", g_numfaces);
	for (int i = 0; i < g_numfaces; i++)
	{
		int mins[2];
		int maxs[2];
		GetFaceExtents (i, mins, maxs);
		fprintf (f, "%i %i %i %i\n", mins[0], mins[1], maxs[0], maxs[1]);
	}
	fclose (f);
}




// =====================================================================================
//  ParseImplicitTexinfoFromTexture
//      purpose: get the actual texinfo for a face. the tools shouldn't directly use f->texinfo after embedlightmap is done
// =====================================================================================
int	ParseImplicitTexinfoFromTexture (int miptex)
{
	int texinfo;
	int numtextures = g_texdatasize? ((dmiptexlump_t *)g_dtexdata.data())->nummiptex: 0;
	int offset;
	int size;
	miptex_t *mt;
	char name[16];
	
	if (miptex < 0 || miptex >= numtextures)
	{
		Warning ("ParseImplicitTexinfoFromTexture: internal error: invalid texture number %d.", miptex);
		return -1;
	}
	offset = ((dmiptexlump_t *)g_dtexdata.data())->dataofs[miptex];
	size = g_texdatasize - offset;
	if (offset < 0 || g_dtexdata.data() + offset < (const std::byte *)&((const dmiptexlump_t *)g_dtexdata.data())->dataofs[numtextures] ||
		size < (int)sizeof (miptex_t))
	{
		return -1;
	}

	mt = (miptex_t *)&g_dtexdata[offset];
	safe_strncpy (name, mt->name, 16);
	
	if (!(
		strlen (name) >= 6 &&
		std::string_view(name + 1, 4) == "_rad" &&
		'0' <= name[5] && name[5] <= '9'
	)) {
		return -1;
	}

	texinfo = atoi (&name[5]);
	if (texinfo < 0 || texinfo >= g_numtexinfo)
	{
		Warning ("Invalid index of original texinfo: %d parsed from texture name '%s'.", texinfo, name);
		return -1;
	}

	return texinfo;
}

int ParseTexinfoForFace (const dface_t *f)
{
	int texinfo;
	int miptex;
	int texinfo2;

	texinfo = f->texinfo;
	miptex = g_texinfo[texinfo].miptex;
	if (miptex != -1)
	{
		texinfo2 = ParseImplicitTexinfoFromTexture (miptex);
		if (texinfo2 != -1)
		{
			texinfo = texinfo2;
		}
	}

	return texinfo;
}

// =====================================================================================
//  DeleteEmbeddedLightmaps
//      removes all "?_rad*" textures that are created by hlrad
//      this function does nothing if the map has no textures with name "?_rad*"
// =====================================================================================
void DeleteEmbeddedLightmaps ()
{
	int countrestoredfaces = 0;
	int countremovedtexinfos = 0;
	int countremovedtextures = 0;
	int numtextures = g_texdatasize? ((dmiptexlump_t *)g_dtexdata.data())->nummiptex: 0;

	// Step 1: parse the original texinfo index stored in each "?_rad*" texture
	//         and restore the texinfo for the faces that have had their lightmap embedded

	for (int i = 0; i < g_numfaces; i++)
	{
		dface_t *f = &g_dfaces[i];
		const int texinfo = ParseTexinfoForFace (f);
		if (texinfo != f->texinfo)
		{
			f->texinfo = texinfo;
			countrestoredfaces++;
		}
	}

	// Step 2: remove redundant texinfo
	{
		bool *texinfoused = (bool *)malloc (g_numtexinfo * sizeof (bool));
		hlassume (texinfoused != nullptr, assume_NoMemory);

		for (int i = 0; i < g_numtexinfo; i++)
		{
			texinfoused[i] = false;
		}
		for (int i = 0; i < g_numfaces; i++)
		{
			const auto texinfo = g_dfaces[i].texinfo;

			if (texinfo < 0 || texinfo >= g_numtexinfo)
			{
				continue;
			}
			texinfoused[texinfo] = true;
		}
		int i;
		for (i = g_numtexinfo - 1; i > -1; i--)
		{
			const auto miptex = g_texinfo[i].miptex;

			if (texinfoused[i])
			{
				break; // still used by a face; should not remove this texinfo
			}
			if (miptex < 0 || miptex >= numtextures)
			{
				break; // invalid; should not remove this texinfo
			}
			if (ParseImplicitTexinfoFromTexture (miptex) == -1)
			{
				break; // not added by hlrad; should not remove this texinfo
			}
			countremovedtexinfos++;
		}
		g_numtexinfo = i + 1; // shrink g_texinfo
		free (texinfoused);
	}

	// Step 3: remove redundant textures
	{
		int numremaining; // number of remaining textures
		bool *textureused = (bool *)malloc (numtextures * sizeof (bool));
		hlassume (textureused != nullptr, assume_NoMemory);

		for (int i = 0; i < numtextures; i++)
		{
			textureused[i] = false;
		}
		for (int i = 0; i < g_numtexinfo; i++)
		{
			const auto miptex = g_texinfo[i].miptex;

			if (miptex < 0 || miptex >= numtextures)
			{
				continue;
			}
			textureused[miptex] = true;
		}
		int i;
		for (i = numtextures - 1; i > -1; i--)
		{
			if (textureused[i] || ParseImplicitTexinfoFromTexture (i) == -1)
			{
				break; // should not remove this texture
			}
			countremovedtextures++;
		}
		numremaining = i + 1;
		free (textureused);

		if (numremaining < numtextures)
		{
			dmiptexlump_t *texdata = (dmiptexlump_t *)g_dtexdata.data();
			std::byte *dataaddr = (std::byte *)&texdata->dataofs[texdata->nummiptex];
			int datasize = (g_dtexdata.data() + texdata->dataofs[numremaining]) - dataaddr;
			std::byte *newdataaddr = (std::byte *)&texdata->dataofs[numremaining];
			memmove (newdataaddr, dataaddr, datasize);
			g_texdatasize = (newdataaddr + datasize) - g_dtexdata.data();
			texdata->nummiptex = numremaining;
			for (i = 0; i < numremaining; i++)
			{
				if (texdata->dataofs[i] < 0) // bad texture
				{
					continue;
				}
				texdata->dataofs[i] += newdataaddr - dataaddr;
			}

			numtextures = texdata->nummiptex;
		}
	}

	if (countrestoredfaces > 0 || countremovedtexinfos > 0 || countremovedtextures > 0)
	{
		Log ("DeleteEmbeddedLightmaps: restored %d faces, removed %d texinfos and %d textures.\n",
				countrestoredfaces, countremovedtexinfos, countremovedtextures);
	}
}


// =====================================================================================
//  ParseEpair
//      entity key/value pairs
// =====================================================================================
std::unique_ptr<epair_t>        ParseEpair()
{
	std::unique_ptr<epair_t> e = std::make_unique<epair_t>();

    if (g_token.size() >= MAX_KEY - 1)
        Error("ParseEpair: Key token too long (%zu > MAX_KEY)", g_token.size());

    e->key = g_token;
    GetToken(false);

    if (g_token.size() >= MAX_VAL - 1) //MAX_VALUE //vluzacn
        Error("ParseEpar: Value token too long (%zu > MAX_VALUE)", g_token.size());

    e->value = g_token;

    return e;
}

/*
 * ================
 * ParseEntity
 * ================
 */

// AJM: each tool should have its own version of GetParamsFromEnt which parseentity calls
extern void     GetParamsFromEnt(entity_t* mapent);

bool            ParseEntity()
{
    if (!GetToken(true))
    {
        return false;
    }

    if (g_token != u8"{"sv)
    {
        Error("ParseEntity: { not found");
    }

    if (g_numentities == MAX_MAP_ENTITIES)
    {
        Error("g_numentities == MAX_MAP_ENTITIES");
    }

    entity_t* mapent = &g_entities[g_numentities];
    g_numentities++;

    while (1)
    {
        if (!GetToken(true))
        {
            Error("ParseEntity: EOF without closing brace");
        }
        if (g_token == u8"}"sv)
        {
            break;
        }
        std::unique_ptr<epair_t> e = ParseEpair();
        e->next = mapent->epairs;
        mapent->epairs = e.release();
    }

    if (key_value_is(mapent, u8"classname", u8"info_compile_parameters"))
    {
        Log("Map entity info_compile_parameters detected, using compile settings\n");
        GetParamsFromEnt(mapent);
    }
	// ugly code
	if (key_value_starts_with(mapent, u8"classname", u8"light") && key_value_is_not_empty(mapent, u8"_tex"))
	{
		SetKeyValue (mapent, u8"convertto", value_for_key(mapent, u8"classname"));
		SetKeyValue (mapent, u8"classname", u8"light_surface");
	}
	if (key_value_is(mapent, u8"convertfrom", u8"light_shadow")
		|| key_value_is (mapent, u8"convertfrom", u8"light_bounce")
		)
	{
		SetKeyValue (mapent, u8"convertto", value_for_key (mapent, u8"classname"));
		SetKeyValue (mapent, u8"classname", value_for_key (mapent, u8"convertfrom"));
		DeleteKey (mapent, u8"convertfrom");
	}
	if (classname_is(mapent, u8"light_environment") &&
		key_value_is(mapent, u8"convertfrom", u8"info_sunlight"))
	{
		DeleteAllKeys(mapent);
		memset (mapent, 0, sizeof(entity_t));
		g_numentities--;
		return true;
	}
	if (classname_is (mapent, u8"light_environment") &&
		IntForKey (mapent, u8"_fake"))
	{
		SetKeyValue (mapent, u8"classname", u8"info_sunlight");
	}

    return true;
}

// =====================================================================================
//  ParseEntities
//      Parses the dentdata string into entities
// =====================================================================================
void            ParseEntities()
{
    g_numentities = 0;
    ParseFromMemory((char*) g_dentdata.data(), g_entdatasize);

    while (ParseEntity())
    {
    }
}

// =====================================================================================
//  UnparseEntities
//      Generates the dentdata string from all the entities
// =====================================================================================
int anglesforvector (float angles[3], const float vector[3])
{
	float z = vector[2], r = sqrt (vector[0] * vector[0] + vector[1] * vector[1]);
	float tmp;
	if (sqrt (z*z + r*r) < NORMAL_EPSILON)
	{
		return -1;
	}
	else
	{
		tmp = sqrt (z*z + r*r);
		z /= tmp, r /= tmp;
		if (r < NORMAL_EPSILON)
		{
			if (z < 0)
			{
				angles[0] = -90, angles[1] = 0;
			}
			else
			{
				angles[0] = 90, angles[1] = 0;
			}
		}
		else
		{
			// std::numbers::pi_v<double> could be unnecessary precision, try using
			// std::numbers::pi_v<float> instead. -- Oskar
			angles[0] = atan (z / r) / std::numbers::pi_v<double> * 180;
			float x = vector[0], y = vector[1];
			tmp = sqrt (x*x + y*y);
			x /= tmp, y /= tmp;
			if (x < -1 + NORMAL_EPSILON)
			{
				angles[1] = -180;
			}
			else
			{
				if (y >= 0)
				{
					angles[1] = 2 * atan (y / (1+x)) / std::numbers::pi_v<double> * 180;
				}
				else
				{
					angles[1] = 2 * atan (y / (1+x)) / std::numbers::pi_v<double> * 180 + 360;
				}
			}
		}
	}
	angles[2] = 0;
	return 0;
}
void            UnparseEntities()
{
    char8_t*           buf;
    char8_t*           end;
    epair_t*        ep;
    char            line[MAXTOKEN];
    int             i;

    buf = g_dentdata.data();
    end = buf;
    *end = 0;

	for (i = 0; i < g_numentities; i++)
	{
		entity_t *mapent = &g_entities[i];
		if (
			classname_is(mapent, u8"info_sunlight") ||
			classname_is(mapent, u8"light_environment"))
		{
			float vec[3] = {0,0,0};
			{
				sscanf ((const char*) ValueForKey (mapent, u8"angles"), "%f %f %f", &vec[0], &vec[1], &vec[2]);
				float pitch = FloatForKey(mapent, u8"pitch");
				if (pitch)
					vec[0] = pitch;

				std::u8string_view target = value_for_key (mapent, u8"target");
				if (!target.empty())
				{
					entity_t *targetent = FindTargetEntity (target);
					if (targetent)
					{
						float origin1[3] = {0,0,0}, origin2[3] = {0,0,0}, normal[3];
						sscanf ((const char*) ValueForKey (mapent, u8"origin"), "%f %f %f", &origin1[0], &origin1[1], &origin1[2]);
						sscanf ((const char*) ValueForKey (targetent, u8"origin"), "%f %f %f", &origin2[0], &origin2[1], &origin2[2]);
						VectorSubtract (origin2, origin1, normal);
						anglesforvector (vec, normal);
					}
				}
			}
			char stmp[1024];
			safe_snprintf (stmp, 1024, "%g %g %g", vec[0], vec[1], vec[2]);
			SetKeyValue (mapent, u8"angles", (const char8_t*) stmp);
			DeleteKey (mapent, u8"pitch");

			if (!strcmp ((const char*) ValueForKey (mapent, u8"classname"), "info_sunlight"))
			{
				if (g_numentities == MAX_MAP_ENTITIES)
				{
					Error("g_numentities == MAX_MAP_ENTITIES");
				}
				entity_t *newent = &g_entities[g_numentities++];
				newent->epairs = mapent->epairs;
				SetKeyValue (newent, u8"classname",u8"light_environment");
				SetKeyValue (newent, u8"_fake", u8"1");
				mapent->epairs = nullptr;
			}
		}
	}
    for (i = 0; i < g_numentities; i++)
	{
		entity_t *mapent = &g_entities[i];
		if (!strcmp ((const char*) ValueForKey (mapent, u8"classname"), "light_shadow")
			|| !strcmp ((const char*) ValueForKey (mapent, u8"classname"), "light_bounce")
			)
		{
			SetKeyValue (mapent, u8"convertfrom", ValueForKey (mapent, u8"classname"));
			SetKeyValue (mapent, u8"classname", (*ValueForKey (mapent, u8"convertto")? ValueForKey (mapent, u8"convertto"): u8"light"));
			DeleteKey (mapent, u8"convertto");
		}
	}
	// ugly code
	for (i = 0; i < g_numentities; i++)
	{
		entity_t *mapent = &g_entities[i];
		if (classname_is(mapent, u8"light_surface"))
		{
			if (key_value_is_empty(mapent, u8"_tex"))
			{
				SetKeyValue (mapent, u8"_tex", u8"                ");
			}
			std::u8string_view newclassname = value_for_key (mapent, u8"convertto");
			if (newclassname.empty())
			{
				SetKeyValue (mapent, u8"classname", u8"light");
			}
			else if (!newclassname.starts_with(u8"light"))
			{
				Error ("New classname for 'light_surface' should begin with 'light' not '%s'.\n", (const char*) newclassname.data());
			}
			else
			{
				SetKeyValue (mapent, u8"classname", newclassname);
			}
			DeleteKey (mapent, u8"convertto");
		}
	}
#ifdef SDHLCSG //seedee
	extern bool g_nolightopt;
	if (!g_nolightopt)
	{
		int i, j;
		int count = 0;
		bool *lightneedcompare = (bool *)malloc (g_numentities * sizeof (bool));
		hlassume (lightneedcompare != nullptr, assume_NoMemory);
		memset (lightneedcompare, 0, g_numentities * sizeof(bool));
		for (i = g_numentities - 1; i > -1; i--)
		{
			entity_t *ent = &g_entities[i];
			const char8_t *classname = ValueForKey (ent, u8"classname");
			const char8_t *targetname = ValueForKey (ent, u8"targetname");
			int style = IntForKey (ent, u8"style");
			if (!targetname[0] || strcmp ((const char*) classname, "light") && strcmp ((const char*) classname, "light_spot") && strcmp ((const char*) classname, "light_environment"))
				continue;
			for (j = i + 1; j < g_numentities; j++)
			{
				if (!lightneedcompare[j])
					continue;
				entity_t *ent2 = &g_entities[j];
				const char8_t *targetname2 = ValueForKey (ent2, u8"targetname");
				int style2 = IntForKey (ent2, u8"style");
				if (style == style2 && !strcmp ((const char*) targetname, (const char*) targetname2))
					break;
			}
			if (j < g_numentities)
			{
				DeleteKey (ent, u8"targetname");
				count++;
			}
			else
			{
				lightneedcompare[i] = true;
			}
		}
		if (count > 0)
		{
			Log ("%d redundant named lights optimized.\n", count);
		}
		free (lightneedcompare);
	}
#endif
    for (i = 0; i < g_numentities; i++)
    {
        ep = g_entities[i].epairs;
        if (!ep)
        {
            continue;                                      // ent got removed
        }

        strcat((char*) end, (const char*) "{\n");
        end += 2;

        for (ep = g_entities[i].epairs; ep; ep = ep->next)
        {
            snprintf(line, sizeof(line), "\"%s\" \"%s\"\n", (const char*) ep->key.c_str(), (const char*) ep->value.c_str());
            strcat((char*) end, line);
            end += strlen(line);
        }
        strcat((char*) end, (const char*) u8"}\n");
        end += 2;

        if (end > buf + MAX_MAP_ENTSTRING)
        {
            Error("Entity text too long");
        }
    }
    g_entdatasize = end - buf + 1;
}

void DeleteAllKeys(entity_t* ent)
{
	epair_t **pep;
	for (pep = &ent->epairs; *pep; pep = &(*pep)->next)
	{
		epair_t *ep = *pep;
		*pep = ep->next;
		delete ep;
	}
}
void DeleteKey(entity_t* ent, std::u8string_view key)
{
	epair_t **pep;
	for (pep = &ent->epairs; *pep; pep = &(*pep)->next)
	{
		if ((*pep)->key == key)
		{
			epair_t *ep = *pep;
			*pep = ep->next;
			delete ep;
			return;
		}
	}
}
void SetKeyValue(entity_t* ent, std::u8string_view key, std::u8string_view value) {
	if (value.empty())
	{
		DeleteKey (ent, key);
		return;
	}
    for (epair_t* ep = ent->epairs; ep; ep = ep->next)
    {
        if (ep->key == key)
        {
			ep->value = std::move(value);
            return;
        }
    }
    std::unique_ptr<epair_t> ep = std::make_unique<epair_t>();
    ep->next = ent->epairs;
    ep->key = key;
    ep->value = std::move(value);
    ent->epairs = ep.release();
}

// =====================================================================================
//  ValueForKey
//      returns the value for a passed entity and key
// =====================================================================================
const char8_t*     ValueForKey(const entity_t* const ent, std::u8string_view key)
{
    epair_t*        ep;

    for (ep = ent->epairs; ep; ep = ep->next)
    {
        if (ep->key == key)
        {
            return ep->value.c_str();
        }
    }
    return u8"";
}


std::u8string_view value_for_key(const entity_t* const ent, std::u8string_view key)
{
	// TODO: This is inefficient. When ValueForKey is no longer used, move its code in here
	// but without the .c_str()
    return ValueForKey(ent, key);
}


bool key_value_is_not_empty(const entity_t* const ent, std::u8string_view key)
{
	return !key_value_is_empty(ent, key);
}
bool key_value_is_empty(const entity_t* const ent, std::u8string_view key)
{
	return value_for_key(ent, key).empty();
}
bool key_value_is(const entity_t* const ent, std::u8string_view key, std::u8string_view value)
{
	return value_for_key(ent, key) == value;
}
bool key_value_starts_with(const entity_t* const ent, std::u8string_view key, std::u8string_view prefix)
{
	return value_for_key(ent, key).starts_with(prefix);
}
bool classname_is(const entity_t* const ent, std::u8string_view classname)
{
	return key_value_is(ent, u8"classname", classname);
}


// =====================================================================================
//  IntForKey
// =====================================================================================
int             IntForKey(const entity_t* const ent, std::u8string_view key)
{
    return atoi((const char*) value_for_key(ent, key).data());
}

// =====================================================================================
//  FloatForKey
// =====================================================================================
vec_t           FloatForKey(const entity_t* const ent, std::u8string_view key)
{
    return atof((const char*) value_for_key(ent, key).data());
}

// =====================================================================================
//  GetVectorForKey
//      returns value for key in vec[0-2]
// =====================================================================================
void            GetVectorForKey(const entity_t* const ent, std::u8string_view key, vec3_array& vec)
{
    double          v1, v2, v3;

    // scanf into doubles, then assign, so it is vec_t size independent
    v1 = v2 = v3 = 0;
    sscanf((const char*) value_for_key(ent, key).data(), "%lf %lf %lf", &v1, &v2, &v3);
    vec[0] = v1;
    vec[1] = v2;
    vec[2] = v3;
}

// =====================================================================================
//  FindTargetEntity
//      
// =====================================================================================
entity_t *FindTargetEntity(std::u8string_view target) {
	for(entity_t& ent : std::span(&g_entities[0], g_numentities)) {
        if (key_value_is(&ent, u8"targetname", target)) {
            return &ent;
        }
	}
    return nullptr;
}


void            dtexdata_init()
{
    g_dtexdata.resize(g_max_map_miptex, std::byte(0));
}

void dtexdata_free()
{
    g_dtexdata.clear();
	g_dtexdata.shrink_to_fit();
	g_dlightdata.clear();
	g_dlightdata.shrink_to_fit();
}

// =====================================================================================
//  GetTextureByNumber
//      Touchy function, can fail with a page fault if all the data isnt kosher 
//      (i.e. map was compiled with missing textures)
// =====================================================================================
static char emptystring[1] = {'\0'};
char*           GetTextureByNumber(int texturenumber)
{
	if (texturenumber == -1)
		return emptystring;
    texinfo_t*      info;
    miptex_t*       miptex;
    int             ofs;

    info = &g_texinfo[texturenumber];
    ofs = ((dmiptexlump_t*)g_dtexdata.data())->dataofs[info->miptex];
    miptex = (miptex_t*)(&g_dtexdata[ofs]);

    return miptex->name;
}

// =====================================================================================
//  EntityForModel
//      returns entity addy for given modelnum
// =====================================================================================
entity_t*       EntityForModel(const int modnum)
{
    std::array<char8_t, 16> name;

    snprintf((char*) name.data(), name.size(), "*%i", modnum);
    // search the entities for one using modnum
    for (std::size_t i = 0; i < g_numentities; i++)
    {
        if (value_for_key(&g_entities[i], u8"model") == std::u8string_view(name.data()))
        {
            return &g_entities[i];
        }
    }

    return &g_entities[0];
}