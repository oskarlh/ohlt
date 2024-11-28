#include "bsp_file_sizes.h"
#include "bspfile.h"
#include "cmdlib.h"
#include "log.h"
#include <algorithm>
#include <optional>

constexpr std::int32_t BLOCK_HEIGHT = 128;
constexpr std::int32_t BLOCK_WIDTH = 128;

struct lightmapblock
{
	std::array<std::int32_t, BLOCK_WIDTH> allocated = {};
	bool used = false;
};


void do_alloc_block (std::deque<lightmapblock>& blocks, int w, int h)
{
	if (w < 1 || h < 1)
	{
		Error("DoAllocBlock: internal error.");
	}
	std::int32_t best, best2;
	std::int32_t x;
	for (lightmapblock& block : blocks)
	{
		best = BLOCK_HEIGHT;
		for (std::int32_t i = 0; i < BLOCK_WIDTH - w; i++)
		{
			best2 = 0;
			std::int32_t j;
			for (j = 0; j < w; j++)
			{
				if (block.allocated[i + j] >= best)
					break;
				if (block.allocated[i + j] > best2)
					best2 = block.allocated[i + j];
			}
			if (j == w)
			{
				x = i;
				best = best2;
			}
		}
		if (best + h <= BLOCK_HEIGHT)
		{
			block.used = true;
			for (std::size_t i = 0; i < w; i++)
			{
				block.allocated[x + i] = best + h;
			}
			return;
		}
		const bool isLastBlock = &block == &blocks.back();
		if (isLastBlock)
		{ // need to allocate a new block
			if (!block.used)
			{
				Warning ("CountBlocks: invalid extents %dx%d", w, h);
				return;
			}
			blocks.emplace_back();
		}
	}
}
std::size_t count_blocks (const bsp_data& bspData)
{
	std::deque<lightmapblock> blocks;
	blocks.emplace_back();
	for (int k = 0; k < bspData.facesLength; k++)
	{
		const dface_t *f = &bspData.faces[k];
		const char *texname =  GetTextureByNumber (ParseTexinfoForFace (f));
		if (!strncmp (texname, "sky", 3) //sky, no lightmap allocation.
			|| !strncmp (texname, "!", 1) || !strncasecmp (texname, "water", 5) || !strncasecmp (texname, "laser", 5) //water, no lightmap allocation.
			|| (g_texinfo[ParseTexinfoForFace (f)].flags & TEX_SPECIAL) //aaatrigger, I don't know.
			)
		{
			continue;
		}
		int extents[2];
		vec3_t point;
		{
			int bmins[2];
			int bmaxs[2];
			int i;
			GetFaceExtents (k, bmins, bmaxs);
			for (i = 0; i < 2; i++)
			{
				extents[i] = (bmaxs[i] - bmins[i]) * TEXTURE_STEP;
			}

			VectorClear (point);
			if (f->numedges > 0)
			{
				int e = g_dsurfedges[f->firstedge];
				dvertex_t *v = &g_dvertexes[g_dedges[abs (e)].v[e >= 0? 0: 1]];
				VectorCopy (v->point, point);
			}
		}
		if (extents[0] < 0 || extents[1] < 0 || extents[0] > std::max(512, MAX_SURFACE_EXTENT * TEXTURE_STEP) || extents[1] > std::max(512, MAX_SURFACE_EXTENT * TEXTURE_STEP))
			// the default restriction from the engine is 512, but place 'max (512, MAX_SURFACE_EXTENT * TEXTURE_STEP)' here in case someone raise the limit
		{
			Warning ("Bad surface extents %d/%d at position (%.0f,%.0f,%.0f)", extents[0], extents[1], point[0], point[1], point[2]);
			continue;
		}
		do_alloc_block (blocks, (extents[0] / TEXTURE_STEP) + 1, (extents[1] / TEXTURE_STEP) + 1);
	}
	return std::ranges::count_if(blocks, [] (const lightmapblock& block) {
		return block.used;
	});
}

bool no_wad_textures (const bsp_data& bspData)
{
	const int numtextures = bspGlobals.textureDataByteSize ? ((const dmiptexlump_t *) bspData.textureData)->nummiptex: 0;
	for (int i = 0; i < numtextures; i++)
	{
		const int offset = ((const dmiptexlump_t*) bspData.textureData)->dataofs[i];
		std::size_t size = bspGlobals.textureDataByteSize - offset;
		if (offset < 0 || size < sizeof (miptex_t))
		{
			// Missing textures have offset = -1
			continue;
		}
		const miptex_t *mt = (const miptex_t *) &bspData.textureData[offset];
		if (!mt->offsets[0]) // Check for valid mip texture
		{
			return false;
		}
	}
	return true;
}


// Returns nullopt for syntax error.
static std::optional<std::u8string> find_wad_value (const bsp_data& bspData)
{
	
	bool inentity = false;
	for (std::size_t linestart = 0; linestart < bspData.entityDataLength; ) {
		std::size_t lineend;
		for (lineend = linestart; lineend < bspData.entityDataLength; lineend++) {
			if (bspData.entityData[lineend] == u8'\r' || bspData.entityData[lineend] == u8'\n') {
				break;
			}
		}
		if (lineend == linestart + 1) {
			if (bspData.entityData[linestart] == u8'{') {
				if (inentity)
					return std::nullopt;
				inentity = true;
			} else if (bspData.entityData[linestart] == u8'}') {
				if (!inentity)
					return std::nullopt;
				inentity = false;
				return u8""; // only parse the first entity
			} else {
				return std::nullopt;
			}
		}
		else
		{
			if (!inentity) {
				return std::nullopt;
			}
			std::array<int, 4> quotes;
			int i, j;
			for (i = 0, j = linestart; i < 4; i++, j++)
			{
				for (; j < lineend; j++)
					if (bspData.entityData[j] == u8'\"')
						break;
				if (j >= lineend)
					break;
				quotes[i] = j;
			}
			if (i != 4 || quotes[0] != linestart || quotes[3] != lineend - 1)
			{
				return std::nullopt;
			}
			if (quotes[1] - (quotes[0] + 1) == (int) strlen ("wad") && !strncmp ((const char*) &bspData.entityData[quotes[0] + 1], "wad", strlen ("wad")))
			{
				std::size_t len = quotes[3] - (quotes[2] + 1);
				char *value = (char *)malloc (len + 1);
				const char8_t* start = (const char8_t*) &bspData.entityData[quotes[2] + 1];
				return std::u8string(start, start + len);
			}
		}
		for (linestart = lineend; linestart < bspData.entityDataLength; linestart++) {
			if (bspData.entityData[linestart] != u8'\r' && bspData.entityData[linestart] != u8'\n') {
				break;
			}
		}
	}
	return std::nullopt;
}

static int      array_usage(const char* const szItem, const int items, const int maxitems, const int itemsize)
{
    float           percentage = maxitems ? items * 100.0 / maxitems : 0.0;

    Log("%-13s %7i/%-7i %8i/%-8i (%4.1f%%)\n", szItem, items, maxitems, items * itemsize, maxitems * itemsize, percentage);

    return items * itemsize;
}
static int      global_usage(const char* const szItem, const int itemstorage, const int maxstorage)
{
    float           percentage = maxstorage ? itemstorage * 100.0 / maxstorage : 0.0;

    Log("%-13s    [variable]   %8i/%-8i (%4.1f%%)\n", szItem, itemstorage, maxstorage, percentage);

    return itemstorage;
}

#define ENTRIES(a)		(sizeof(a)/sizeof(*(a)))
#define ENTRYSIZE(a)	(sizeof(*(a)))

// =====================================================================================
//  PrintBSPFileSizes
//      Dumps info about current file
// =====================================================================================
void print_bsp_file_sizes(const bsp_data& bspData)
{
    int             numtextures = bspData.textureDataByteSize ? ((dmiptexlump_t*)bspData.textureData)->nummiptex : 0;
    int             totalmemory = 0;
	std::size_t numallocblocks = count_blocks (bspData);
	std::size_t maxallocblocks = 64;
	bool nowadtextures = no_wad_textures (bspData); // We don't have this check at hlcsg, because only legacy compile tools don't empty "wad" value in "-nowadtextures" compiles.

    Log("\n");
    Log("Object names  Objects/Maxobjs  Memory / Maxmem  Fullness\n");
    Log("------------  ---------------  ---------------  --------\n");

    totalmemory += array_usage("models", bspData.mapModelsLength, bspData.mapModels.size(), sizeof(bspData.mapModels[0]));
    totalmemory += array_usage("planes", g_numplanes, MAX_MAP_PLANES, ENTRYSIZE(g_dplanes));
    totalmemory += array_usage("vertexes", g_numvertexes, ENTRIES(g_dvertexes), ENTRYSIZE(g_dvertexes));
    totalmemory += array_usage("nodes", g_numnodes, ENTRIES(g_dnodes), ENTRYSIZE(g_dnodes));
    totalmemory += array_usage("texinfos", g_numtexinfo, MAX_MAP_TEXINFO, ENTRYSIZE(g_texinfo));
    totalmemory += array_usage("faces", g_numfaces, bspData.faces.size(), sizeof(bspData.faces[0]));
	totalmemory += array_usage("* worldfaces", (bspData.mapModelsLength > 0 ? bspData.mapModels[0].numfaces: 0), MAX_MAP_WORLDFACES, 0);
    totalmemory += array_usage("clipnodes", g_numclipnodes, ENTRIES(g_dclipnodes), ENTRYSIZE(g_dclipnodes));
    totalmemory += array_usage("leaves", g_numleafs, MAX_MAP_LEAFS, ENTRYSIZE(g_dleafs));
    totalmemory += array_usage("* worldleaves", (bspData.mapModelsLength > 0 ? bspData.mapModels[0].visleafs: 0), MAX_MAP_LEAFS_ENGINE, 0);
    totalmemory += array_usage("marksurfaces", g_nummarksurfaces, ENTRIES(g_dmarksurfaces), ENTRYSIZE(g_dmarksurfaces));
    totalmemory += array_usage("surfedges", g_numsurfedges, ENTRIES(g_dsurfedges), ENTRYSIZE(g_dsurfedges));
    totalmemory += array_usage("edges", g_numedges, ENTRIES(g_dedges), ENTRYSIZE(g_dedges));

    totalmemory += global_usage("texdata", g_texdatasize, g_max_map_miptex);
    totalmemory += global_usage("lightdata", g_lightdatasize, g_max_map_lightdata);
    totalmemory += global_usage("visdata", g_visdatasize, bspData.mapModels.size());
    totalmemory += global_usage("entdata", g_entdatasize, bspData.entityData.size() * sizeof(bspData.entityData[0]));
	if (numallocblocks == -1)
	{
		Log ("* AllocBlock    [ not available to the " PLATFORM_VERSION " version ]\n");
	}
	else
	{
		totalmemory += array_usage ("* AllocBlock", numallocblocks, maxallocblocks, 0);
	}

    Log("%i textures referenced\n", numtextures);

    Log("=== Total BSP file data space used: %d bytes ===\n\n", totalmemory);

	if (nowadtextures)
	{
		Log ("No wad files required to run the map\n");
	}
	else {
		std::optional<std::u8string> wadvalue = find_wad_value(bspData);
		if (!wadvalue.has_value())
		{
			Log ("Wad files required to run the map: (Couldn't parse wad keyvalue from entity data)\n");
		}
		else //If we have any wads still required //seedee
		{
			Log("Wad files required to run the map\n");
			Log("---------------------------------\n");
			for (size_t i = 0; i < wadvalue.value().length(); ++i) {
				if (wadvalue.value()[i] == u8';') {
					wadvalue.value()[i] = u8'\n';
				}
			}
			Log("%s", (const char*) wadvalue.value().c_str());
			Log("---------------------------------\n\n");
		}
	}
}
