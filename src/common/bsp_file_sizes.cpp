#include "bsp_file_sizes.h"

#include "bspfile.h"
#include "log.h"
#include "worldspawn_wad_value_parser.h"

#include <cstring>
#include <optional>
#include <string_view>

using namespace std::literals;

bool no_wad_textures(bsp_data const & bspData) {
	int const numtextures = bspGlobals.textureDataByteSize
		? ((dmiptexlump_t const *) bspData.textureData.data())->nummiptex
		: 0;
	for (int i = 0; i < numtextures; i++) {
		int const offset = ((dmiptexlump_t const *)
								bspData.textureData.data())
							   ->dataofs[i];
		std::size_t size = bspGlobals.textureDataByteSize - offset;
		if (offset < 0 || size < sizeof(miptex_t)) {
			// Missing textures have offset = -1
			continue;
		}
		miptex_t const * mt
			= (miptex_t const *) &bspData.textureData[offset];
		if (!mt->offsets[0]) // Check for valid mip texture
		{
			return false;
		}
	}
	return true;
}

// Returns nullopt for syntax error.
static std::optional<std::u8string> find_wad_value(bsp_data const & bspData
) {
	bool inentity = false;
	for (std::size_t linestart = 0; linestart < bspData.entityDataLength;) {
		std::size_t lineend;
		for (lineend = linestart; lineend < bspData.entityDataLength;
			 lineend++) {
			if (bspData.entityData[lineend] == u8'\r'
				|| bspData.entityData[lineend] == u8'\n') {
				break;
			}
		}
		if (lineend == linestart + 1) {
			if (bspData.entityData[linestart] == u8'{') {
				if (inentity) {
					return std::nullopt;
				}
				inentity = true;
			} else if (bspData.entityData[linestart] == u8'}') {
				if (!inentity) {
					return std::nullopt;
				}
				inentity = false;
				return u8""; // only parse the first entity
			} else {
				return std::nullopt;
			}
		} else {
			if (!inentity) {
				return std::nullopt;
			}
			std::array<int, 4> quotes;
			std::size_t i, j;
			for (i = 0, j = linestart; i < 4; i++, j++) {
				for (; j < lineend; j++) {
					if (bspData.entityData[j] == u8'\"') {
						break;
					}
				}
				if (j >= lineend) {
					break;
				}
				quotes[i] = j;
			}
			if (i != 4 || quotes[0] != linestart
				|| quotes[3] != lineend - 1) {
				return std::nullopt;
			}
			if (quotes[1] - (quotes[0] + 1) == (int) u8"wad"sv.length()
				&& !strncmp(
					(char const *) &bspData.entityData[quotes[0] + 1],
					"wad",
					u8"wad"sv.length()
				)) {
				std::size_t len = quotes[3] - (quotes[2] + 1);
				char8_t const * start
					= (char8_t const *) &bspData.entityData[quotes[2] + 1];
				return std::u8string(start, start + len);
			}
		}
		for (linestart = lineend; linestart < bspData.entityDataLength;
			 linestart++) {
			if (bspData.entityData[linestart] != u8'\r'
				&& bspData.entityData[linestart] != u8'\n') {
				break;
			}
		}
	}
	return std::nullopt;
}

static int array_usage(
	char const * const szItem,
	int const items,
	int const maxitems,
	int const itemsize
) {
	float percentage = maxitems ? items * 100.0 / maxitems : 0.0;

	Log("%-13s %7i/%-7i %8i/%-8i (%4.1f%%)\n",
		szItem,
		items,
		maxitems,
		items * itemsize,
		maxitems * itemsize,
		percentage);

	return items * itemsize;
}

static int global_usage(
	char const * const szItem, int const itemstorage, int const maxstorage
) {
	float percentage = maxstorage ? itemstorage * 100.0 / maxstorage : 0.0;

	Log("%-13s    [variable]   %8i/%-8i (%4.1f%%)\n",
		szItem,
		itemstorage,
		maxstorage,
		percentage);

	return itemstorage;
}

// =====================================================================================
//  PrintBSPFileSizes
//      Dumps info about current file
// =====================================================================================
void print_bsp_file_sizes(bsp_data const & bspData) {
	int numtextures = bspData.textureDataByteSize
		? ((dmiptexlump_t*) bspData.textureData.data())->nummiptex
		: 0;
	int totalmemory = 0;
	bool nowadtextures = no_wad_textures(bspData
	); // We don't have this check at hlcsg, because only legacy compile
	   // tools don't empty "wad" value in "-nowadtextures" compiles.

	Log("\n");
	Log("Object names  Objects/Maxobjs  Memory / Maxmem  Fullness\n");
	Log("------------  ---------------  ---------------  --------\n");

	totalmemory += array_usage(
		"models",
		bspData.mapModelsLength,
		bspData.mapModels.size(),
		sizeof(bspData.mapModels[0])
	);
	totalmemory += array_usage(
		"planes",
		bspData.planesLength,
		MAX_MAP_PLANES,
		sizeof(bspData.planes[0])
	);
	totalmemory += array_usage(
		"vertexes",
		bspData.vertexesLength,
		bspData.vertexes.size(),
		sizeof(bspData.vertexes[0])
	);
	totalmemory += array_usage(
		"nodes",
		bspData.nodesLength,
		bspData.nodes.size(),
		sizeof(bspData.nodes[0])
	);
	totalmemory += array_usage(
		"texinfos",
		bspData.texInfosLength,
		FINAL_MAX_MAP_TEXINFO,
		sizeof(bspData.texInfos[0])
	);
	totalmemory += array_usage(
		"faces",
		bspData.facesLength,
		bspData.faces.size(),
		sizeof(bspData.faces[0])
	);
	totalmemory += array_usage(
		"* worldfaces",
		(bspData.mapModelsLength > 0 ? bspData.mapModels[0].numfaces : 0),
		MAX_MAP_WORLDFACES,
		0
	);
	totalmemory += array_usage(
		"clipnodes",
		bspData.clipNodesLength,
		bspData.clipNodes.size(),
		sizeof(bspData.clipNodes[0])
	);
	totalmemory += array_usage(
		"leaves",
		bspData.leafsLength,
		MAX_MAP_LEAFS,
		sizeof(bspData.leafs[0])
	);
	totalmemory += array_usage(
		"* worldleaves",
		(bspData.mapModelsLength > 0 ? bspData.mapModels[0].visleafs : 0),
		MAX_MAP_LEAFS_ENGINE,
		0
	);
	totalmemory += array_usage(
		"marksurfaces",
		bspData.markSurfacesLength,
		bspData.markSurfaces.size(),
		sizeof(bspData.markSurfaces[0])
	);
	totalmemory += array_usage(
		"surfedges",
		bspData.surfEdgesLength,
		bspData.surfEdges.size(),
		sizeof(bspData.surfEdges[0])
	);
	totalmemory += array_usage(
		"edges",
		bspData.edgesLength,
		bspData.edges.size(),
		sizeof(bspData.edges[0])
	);

	totalmemory += global_usage(
		"texdata", bspGlobals.textureDataByteSize, g_max_map_miptex
	);
	totalmemory += global_usage(
		"lightdata", bspGlobals.lightData.size(), g_max_map_lightdata
	);
	totalmemory += global_usage(
		"visdata", bspGlobals.visDataByteSize, bspData.mapModels.size()
	);
	totalmemory += global_usage(
		"entdata",
		bspGlobals.entityDataLength,
		bspData.entityData.size() * sizeof(bspData.entityData[0])
	);

	Log("%i textures referenced\n", numtextures);

	Log("=== Total BSP file data space used: %d bytes ===\n\n",
		totalmemory);

	if (nowadtextures) {
		Log("No wad files required to run the map\n");
	} else {
		std::optional<std::u8string> wadValue = find_wad_value(bspData);
		if (!wadValue) {
			Log("Wad files required to run the map: (Couldn't parse wad keyvalue from entity data)\n"
			);
		} else // If we have any wads still required
		{
			Log("Wad files required to run the map\n");
			Log("---------------------------------\n");
			for (std::u8string_view wadFilename :
				 worldspawn_wad_value_parser(wadValue.value())) {
				Log("%s\n",
					(char const *) std::u8string(wadFilename).c_str());
			}
			Log("---------------------------------\n\n");
		}
	}
}
