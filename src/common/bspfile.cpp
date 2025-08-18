#include "bspfile.h"

#include "cli_option_defaults.h"
#include "filelib.h"
#include "log.h"
#include "map_entity_parser.h"
#include "mathtypes.h"
#include "messages.h"
#include "numeric_string_conversions.h"

#include <charconv>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <ranges>
#include <span>
#include <type_traits>

using namespace std::literals;

//=============================================================================

std::ptrdiff_t g_max_map_miptex = cli_option_defaults::max_map_miptex;

bsp_data bspGlobals{};

std::uint32_t& g_nummodels{ bspGlobals.mapModelsLength };
std::array<dmodel_t, MAX_MAP_MODELS>& g_dmodels{ bspGlobals.mapModels };

std::uint32_t& g_visdatasize{ bspGlobals.visDataByteSize };
std::array<std::byte, MAX_MAP_VISIBILITY>& g_dvisdata{ bspGlobals.visData };

std::vector<std::byte>& g_dlightdata{ bspGlobals.lightData };

int& g_texdatasize{ bspGlobals.textureDataByteSize };
std::vector<std::byte>& g_dtexdata{
	bspGlobals.textureData
}; // (dmiptexlump_t)

std::uint32_t& g_entdatasize{ bspGlobals.entityDataLength };
std::array<char8_t, MAX_MAP_ENTSTRING>& g_dentdata{ bspGlobals.entityData };

int& g_numleafs{ bspGlobals.leafsLength };
std::array<dleaf_t, MAX_MAP_LEAFS>& g_dleafs{ bspGlobals.leafs };

int& g_numplanes{ bspGlobals.planesLength };
std::array<dplane_t, MAX_INTERNAL_MAP_PLANES>& g_dplanes{
	bspGlobals.planes
};

int& g_numvertexes{ bspGlobals.vertexesLength };
std::array<dvertex_t, MAX_MAP_VERTS>& g_dvertexes{ bspGlobals.vertexes };

int& g_numnodes{ bspGlobals.nodesLength };
std::array<dnode_t, MAX_MAP_NODES>& g_dnodes{ bspGlobals.nodes };

texinfo_count& g_numtexinfo{ bspGlobals.texInfosLength };
std::array<texinfo_t, INITIAL_MAX_MAP_TEXINFO>& g_texinfo{
	bspGlobals.texInfos
};

int& g_numfaces{ bspGlobals.facesLength };
std::array<dface_t, MAX_MAP_FACES>& g_dfaces{ bspGlobals.faces };

int& g_iWorldExtent{
	bspGlobals.worldExtent
}; // ENGINE_ENTITY_RANGE; // -worldextent

int& g_numclipnodes{ bspGlobals.clipNodesLength };
std::array<dclipnode_t, MAX_MAP_CLIPNODES>& g_dclipnodes{
	bspGlobals.clipNodes
};

int& g_numedges{ bspGlobals.edgesLength };
std::array<dedge_t, MAX_MAP_EDGES>& g_dedges{ bspGlobals.edges };

int& g_nummarksurfaces{ bspGlobals.markSurfacesLength };
std::array<std::uint16_t, MAX_MAP_MARKSURFACES>& g_dmarksurfaces{
	bspGlobals.markSurfaces
};

int& g_numsurfedges{ bspGlobals.surfEdgesLength };
std::array<std::int32_t, MAX_MAP_SURFEDGES>& g_dsurfedges{
	bspGlobals.surfEdges
};

entity_count& g_numentities{ bspGlobals.entitiesLength };
std::array<entity_t, MAX_MAP_ENTITIES>& g_entities{ bspGlobals.entities };

std::u8string_view ContentsToString(contents_t contents) noexcept {
	switch (contents) {
		case contents_t::EMPTY:
		case contents_t::TOEMPTY:
			return u8"EMPTY";
		case contents_t::SOLID:
			return u8"SOLID";
		case contents_t::WATER:
			return u8"WATER";
		case contents_t::SLIME:
			return u8"SLIME";
		case contents_t::LAVA:
			return u8"LAVA";
		case contents_t::SKY:
			return u8"SKY";
		case contents_t::ORIGIN:
			return u8"ORIGIN";
		case contents_t::HINT:
			return u8"HINT";
		case contents_t::BOUNDINGBOX:
			return u8"BOUNDINGBOX";
		case contents_t::CURRENT_0:
			return u8"CURRENT_0";
		case contents_t::CURRENT_90:
			return u8"CURRENT_90";
		case contents_t::CURRENT_180:
			return u8"CURRENT_180";
		case contents_t::CURRENT_270:
			return u8"CURRENT_270";
		case contents_t::CURRENT_UP:
			return u8"CURRENT_UP";
		case contents_t::CURRENT_DOWN:
			return u8"CURRENT_DOWN";
		case contents_t::TRANSLUCENT:
			return u8"TRANSLUCENT";
		case contents_t::CONTENTS_NULL:
			return u8"NULL";
		default:
			return u8"UNKNOWN";
	}
}

/*
 * ===============
 * CompressVis
 * ===============
 */
int CompressVis(
	byte const * const src,
	unsigned int const src_length,
	byte* dest,
	unsigned int dest_length
) {
	unsigned int j;
	byte* dest_p = dest;
	unsigned int current_length = 0;

	for (j = 0; j < src_length; j++) {
		current_length++;
		hlassume(
			current_length <= dest_length, assume_COMPRESSVIS_OVERFLOW
		);

		*dest_p = src[j];
		dest_p++;

		if (src[j]) {
			continue;
		}

		unsigned char rep = 1;

		for (j++; j < src_length; j++) {
			if (src[j] || rep == 255) {
				break;
			} else {
				rep++;
			}
		}
		current_length++;
		hlassume(
			current_length <= dest_length, assume_COMPRESSVIS_OVERFLOW
		);

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
void DecompressVis(
	byte const * src, byte* const dest, unsigned int const dest_length
) {
	unsigned int current_length = 0;
	int c;
	byte* out;
	int row;

	row = (g_dmodels[0].visleafs + 7)
		>> 3; // same as the length used by VIS program in CompressVis
	// The wrong size will cause DecompressVis to spend extremely long time
	// once the source pointer runs into the invalid area in g_dvisdata (for
	// example, in BuildFaceLights, some faces could hang for a few
	// seconds), and sometimes to crash.

	out = dest;

	do {
		hlassume(
			src - (byte*) g_dvisdata.data() < g_visdatasize,
			assume_DECOMPRESSVIS_OVERFLOW
		);

		if (*src) {
			current_length++;
			hlassume(
				current_length <= dest_length, assume_DECOMPRESSVIS_OVERFLOW
			);

			*out = *src;
			out++;
			src++;
			continue;
		}

		hlassume(
			&src[1] - (byte*) g_dvisdata.data() < g_visdatasize,
			assume_DECOMPRESSVIS_OVERFLOW
		);

		c = src[1];
		src += 2;
		while (c) {
			current_length++;
			hlassume(
				current_length <= dest_length, assume_DECOMPRESSVIS_OVERFLOW
			);

			*out = 0;
			out++;
			c--;

			if (out - dest >= row) {
				return;
			}
		}
	} while (out - dest < row);
}

//
// =====================================================================================
//

// =====================================================================================
//  CopyLump
//      balh
// =====================================================================================
static std::uint32_t
CopyLump(int lump, void* dest, int size, dheader_t const * const header) {
	std::uint32_t length = header->lumps[lump].filelen;
	std::uint32_t ofs = header->lumps[lump].fileofs;

	if (length % size) {
		Error("LoadBSPFile: odd lump size");
	}

	std::memcpy(dest, (byte*) header + ofs, length);

	return length / size;
}

template <lump_id LumpId>
static std::span<lump_element_type<LumpId> const>
get_lump_data(dheader_t const * header) {
	using lump_element = lump_element_type<LumpId>;

	std::uint32_t length = header->lumps[std::size_t(LumpId)].filelen;
	std::uint32_t ofs = header->lumps[std::size_t(LumpId)].fileofs;

	if (length % sizeof(lump_element)) {
		Error("LoadBSPFile: odd lump size");
	}

	// Special handling for tex and lightdata to keep things from
	// exploding
	if (LumpId == lump_id::textures) {
		hlassume(g_max_map_miptex > length, assume_MAX_MAP_MIPTEX);
	} else if (LumpId == lump_id::lighting) {
		hlassume(g_max_map_lightdata > length, assume_MAX_MAP_LIGHTING);
	}

	lump_element const * start
		= (lump_element const *) ((std::byte const *) header + ofs);
	return std::span{ start, length / sizeof(lump_element) };
}

// =====================================================================================
//  LoadBSPFile
//      balh
// =====================================================================================
void LoadBSPFile(std::filesystem::path const & filename) {
	auto [readBspSuccessfully, bspSize, bsp] = read_binary_file(filename);
	if (!readBspSuccessfully || bspSize < sizeof(dheader_t)) {
		Error("Failed to load BSP file %s", filename.c_str());
	}
	LoadBSPImage((dheader_t*) bsp.get());
}

// =====================================================================================
//  LoadBSPImage
//      balh
// =====================================================================================
void LoadBSPImage(dheader_t* const header) {
	if (header->version != BSPVERSION) {
		Error("BSP is version %i, not %i", header->version, BSPVERSION);
	}

	auto modelData = get_lump_data<lump_id::models>(header);
	memcpy(
		g_dmodels.data(),
		modelData.data(),
		modelData.size() * sizeof(modelData[0])
	);
	g_nummodels = modelData.size();

	g_numvertexes = CopyLump(
		LUMP_VERTEXES, g_dvertexes.data(), sizeof(dvertex_t), header
	);
	g_numplanes = CopyLump(
		LUMP_PLANES, g_dplanes.data(), sizeof(dplane_t), header
	);
	g_numleafs = CopyLump(
		LUMP_LEAFS, g_dleafs.data(), sizeof(dleaf_t), header
	);
	g_numnodes = CopyLump(
		LUMP_NODES, g_dnodes.data(), sizeof(dnode_t), header
	);
	g_numtexinfo = CopyLump(
		LUMP_TEXINFO, g_texinfo.data(), sizeof(texinfo_t), header
	);
	g_numclipnodes = CopyLump(
		LUMP_CLIPNODES, g_dclipnodes.data(), sizeof(dclipnode_t), header
	);
	g_numfaces = CopyLump(
		LUMP_FACES, g_dfaces.data(), sizeof(dface_t), header
	);
	g_nummarksurfaces = CopyLump(
		LUMP_MARKSURFACES,
		g_dmarksurfaces.data(),
		sizeof(g_dmarksurfaces[0]),
		header
	);
	g_numsurfedges = CopyLump(
		LUMP_SURFEDGES, g_dsurfedges.data(), sizeof(g_dsurfedges[0]), header
	);
	g_numedges = CopyLump(
		LUMP_EDGES, g_dedges.data(), sizeof(dedge_t), header
	);

	auto textureData = get_lump_data<lump_id::textures>(header);
	memcpy(
		g_dtexdata.data(),
		textureData.data(),
		textureData.size() * sizeof(textureData[0])
	);
	g_texdatasize = textureData.size();

	g_visdatasize = CopyLump(LUMP_VISIBILITY, g_dvisdata.data(), 1, header);

	auto lightingData = get_lump_data<lump_id::lighting>(header);

	g_dlightdata.assign(lightingData.begin(), lightingData.end());

	g_entdatasize = CopyLump(LUMP_ENTITIES, g_dentdata.data(), 1, header);
}

// For debugging purposes
std::size_t hash_data() {
	return hash_multiple(
		0
		// std::span(g_dmodels.data(), g_nummodels),
		// std::span(g_dvertexes.data(), g_numvertexes),
		// std::span(g_dplanes.data(), g_numplanes),
		// std::span(g_dleafs.data(), g_numleafs),
		// std::span(g_dnodes.data(), g_numnodes),
		// std::span(g_texinfo.data(), g_numtexinfo),
		// std::span(g_dclipnodes.data(), g_numclipnodes),
		// std::span(g_dfaces.data(), g_numfaces),
		// std::span(g_dmarksurfaces.data(), g_nummarksurfaces),
		// std::span(g_dsurfedges.data(), g_numsurfedges),
		// std::span(g_dedges.data(), g_numedges),
		// std::span(g_dtexdata.data(), g_texdatasize),
		// std::span(g_dvisdata.data(), g_visdatasize),
		// std::span(g_dlightdata),
		// std::span(g_dentdata.data(), g_entdatasize)
	);
}

//
// =====================================================================================
//

// =====================================================================================
//  add_lump
// =====================================================================================

template <lump_id LumpId>
void add_lump(
	std::span<lump_element_type<LumpId> const> data,
	dheader_t* header,
	FILE* bspfile
) {
	using element_type = lump_element_type<LumpId>;
	std::size_t const byteLength = data.size() * sizeof(element_type);
	lump_t* lump = &header->lumps[std::size_t(LumpId)];
	lump->fileofs = ftell(bspfile);
	lump->filelen = byteLength;
	SafeWrite(bspfile, (char const *) data.data(), byteLength);

	// Why do we need padding??? Does the game need it?
	if (byteLength % 4) {
		std::size_t const paddingLength = 4 - (byteLength % 4);
		std::array<std::byte, 3> const zeroPadding{};
		SafeWrite(bspfile, zeroPadding.data(), paddingLength);
	}
}

// =====================================================================================
//  WriteBSPFile
//      Swaps the bsp file in place, so it should not be referenced again
// =====================================================================================
void WriteBSPFile(std::filesystem::path const & filename) {
	dheader_t outheader;
	dheader_t* header;
	FILE* bspfile;

	header = &outheader;
	*header = {};

	header->version = (BSPVERSION);

	bspfile = SafeOpenWrite(filename.c_str());
	SafeWrite(bspfile, header, sizeof(dheader_t)); // overwritten later

	//      LUMP TYPE       DATA            LENGTH HEADER  BSPFILE
	add_lump<lump_id::planes>(
		std::span(g_dplanes.data(), g_numplanes), header, bspfile
	);
	add_lump<lump_id::leafs>(
		std::span(g_dleafs.data(), g_numleafs), header, bspfile
	);
	add_lump<lump_id::vertexes>(
		std::span(g_dvertexes.data(), g_numvertexes), header, bspfile
	);
	add_lump<lump_id::nodes>(
		std::span(g_dnodes.data(), g_numnodes), header, bspfile
	);
	add_lump<lump_id::texinfo>(
		std::span(g_texinfo.data(), g_numtexinfo), header, bspfile
	);
	add_lump<lump_id::faces>(
		std::span(g_dfaces.data(), g_numfaces), header, bspfile
	);
	add_lump<lump_id::clipnodes>(
		std::span(g_dclipnodes.data(), g_numclipnodes), header, bspfile
	);

	add_lump<lump_id::marksurfaces>(
		std::span(g_dmarksurfaces.data(), g_nummarksurfaces),
		header,
		bspfile
	);
	add_lump<lump_id::surfedges>(
		std::span(g_dsurfedges.data(), g_numsurfedges), header, bspfile
	);
	add_lump<lump_id::edges>(
		std::span(g_dedges.data(), g_numedges), header, bspfile
	);
	add_lump<lump_id::models>(
		std::span(g_dmodels.data(), g_nummodels), header, bspfile
	);

	add_lump<lump_id::lighting>(g_dlightdata, header, bspfile);
	add_lump<lump_id::visibility>(
		std::span(g_dvisdata.data(), g_visdatasize), header, bspfile
	);
	add_lump<lump_id::entities>(
		std::span(g_dentdata.data(), g_entdatasize), header, bspfile
	);
	add_lump<lump_id::textures>(
		std::span(g_dtexdata.data(), g_texdatasize), header, bspfile
	);

	fseek(bspfile, 0, SEEK_SET);
	SafeWrite(bspfile, header, sizeof(dheader_t));

	fclose(bspfile);
}

// =====================================================================================
//  GetFaceExtents
// =====================================================================================

float CalculatePointVecsProduct(
	float3_array const & point, tex_vec const & vecs
) {
	double volatile val;
	double volatile tmp;

	//	float3_array const volatile  & x {vecs.xyz[0]};
	//	float const volatile  & offset {vecs.offset};

	val = (double) point[0]
		* (double) vecs.xyz[0]; // Always do one operation at a time and
								// save to memory
	tmp = (double) point[1] * (double) vecs.xyz[1];
	val = val + tmp;
	tmp = (double) point[2] * (double) vecs.xyz[2];
	val = val + tmp;
	val = val + (double) vecs.offset;

	return (float) val;
}

bool CalcFaceExtents_test() {
	struct test_case final {
		float3_array point;
		tex_vec tv;
		float expectedResult;
	};

	std::array<test_case, 6> testCases = {
		test_case{
			float3_array{ 1.0f, 1.0f, 1.0f },
			tex_vec{ float3_array{
						 1.0f,
						 0.375 * std::numeric_limits<double>::epsilon(),
						 0.375 * std::numeric_limits<double>::epsilon() },
					 -1.0f },
			0.0f },
		test_case{
			float3_array{ 1.0f, 1.0f, 1.0f },
			tex_vec{ float3_array{
						 0.375 * std::numeric_limits<double>::epsilon(),
						 0.375 * std::numeric_limits<double>::epsilon(),
						 1.0f },
					 -1.0f },
			std::numeric_limits<double>::epsilon(),
		},
		test_case{
			float3_array{ std::numeric_limits<double>::epsilon(),
						  std::numeric_limits<double>::epsilon(),
						  1.0f },
			tex_vec{ float3_array{ 0.375f, 0.375f, 1.0f }, -1.0f },
			std::numeric_limits<double>::epsilon(),
		},
		test_case{
			float3_array{ 1.0f, 1.0f, 1.0f },
			tex_vec{ float3_array{
						 1.0f,
						 1.0f,
						 0.375 * std::numeric_limits<float>::epsilon() },
					 -2.0f },
			0.375 * std::numeric_limits<float>::epsilon(),
		},
		test_case{
			float3_array{ 1.0f, 1.0f, 1.0f },
			tex_vec{
				float3_array{ 1.0f,
							  0.375 * std::numeric_limits<float>::epsilon(),
							  1.0f },
				-2.0f },
			0.375 * std::numeric_limits<float>::epsilon(),
		},
		test_case{
			float3_array{ 1.0f, 1.0f, 1.0f },
			tex_vec{
				float3_array{ 0.375 * std::numeric_limits<float>::epsilon(),
							  1.0f,
							  1.0f },
				-2.0f },
			0.375 * std::numeric_limits<float>::epsilon(),
		}
	};

	// If the test failed, please check:
	//   1. whether the calculation is performed on FPU
	//   2. whether the register precision is too low

	bool ok = true;
	for (std::size_t i = 0; i < testCases.size(); ++i) {
		test_case const & testCase = testCases[i];
		float const result = CalculatePointVecsProduct(
			testCase.point, testCase.tv
		);
		if (result != testCase.expectedResult) {
			Warning(
				"internal error: CalcFaceExtents_test failed on case %zu "
				"(%.20f "
				"!= %.20f).",
				i,
				result,
				testCase.expectedResult
			);
			ok = false;
		}
	}
	return ok;
}

face_extents get_face_extents(int facenum) noexcept {
	face_extents result;
	dface_t* f;
	float val;
	int i, j, e;
	dvertex_t* v;
	texinfo_t* tex;

	f = &g_dfaces[facenum];
	std::array<float, 2> mins{ 999'999, 999'999 };
	std::array<float, 2> maxs{ -999'999, -999'999 };

	tex = &g_texinfo[ParseTexinfoForFace(*f)];

	for (i = 0; i < f->numedges; i++) {
		e = g_dsurfedges[f->firstedge + i];
		if (e >= 0) {
			v = &g_dvertexes[g_dedges[e].v[0]];
		} else {
			v = &g_dvertexes[g_dedges[-e].v[1]];
		}
		for (j = 0; j < 2; j++) {
			// The old code: val = v->point[0] * tex->vecs[j][0] +
			// v->point[1] * tex->vecs[j][1] + v->point[2] *
			// tex->vecs[j][2]
			// + tex->vecs[j][3];
			//   was meant to be compiled for x86 under MSVC (prior
			//   to VS 11), so the intermediate values were stored
			//   as 64-bit double by default.
			// The new code will produce the same result as the old
			// code, but it's portable for different platforms. See
			// this article for details: Intermediate Floating-Point
			// Precision by Bruce-Dawson
			// http://www.altdevblogaday.com/2012/03/22/intermediate-floating-point-precision/

			// The essential reason for having this ugly code is to
			// get exactly the same value as the counterpart of game
			// engine. The counterpart of game engine is the
			// function CalcFaceExtents in HLSDK. So we must also
			// know how Valve compiles HLSDK. I think Valve compiles
			// HLSDK with VC6.0 in the past.
			val = CalculatePointVecsProduct(v->point, tex->vecs[j]);
			if (val < mins[j]) {
				mins[j] = val;
			}
			if (val > maxs[j]) {
				maxs[j] = val;
			}
		}
	}

	for (i = 0; i < 2; i++) {
		result.mins[i] = floor(mins[i] / TEXTURE_STEP);
		result.maxs[i] = ceil(maxs[i] / TEXTURE_STEP);
	}
	return result;
}

// =====================================================================================
//  WriteExtentFile
// =====================================================================================
void WriteExtentFile(std::filesystem::path const & filename) {
	FILE* f;
	f = fopen(filename.c_str(), "w");
	if (!f) {
		Error("Error opening %s: %s", filename.c_str(), strerror(errno));
	}
	fprintf(f, "%i\n", g_numfaces);
	for (int i = 0; i < g_numfaces; i++) {
		face_extents const extents{ get_face_extents(i) };
		fprintf(
			f,
			"%i %i %i %i\n",
			extents.mins[0],
			extents.mins[1],
			extents.maxs[0],
			extents.maxs[1]
		);
	}
	fclose(f);
}

// =====================================================================================
//  ParseImplicitTexinfoFromTexture
//      purpose: get the actual texinfo for a face. the tools shouldn't
//      directly use f->texinfo after embedlightmap is done
// =====================================================================================
static texinfo_count ParseImplicitTexinfoFromTexture(int miptex) {
	int numtextures = g_texdatasize
		? ((dmiptexlump_t*) g_dtexdata.data())->nummiptex
		: 0;
	int offset;
	int size;

	if (miptex < 0 || miptex >= numtextures) {
		Warning(
			"ParseImplicitTexinfoFromTexture: internal error: invalid "
			"texture "
			"number %d.",
			miptex
		);
		return -1;
	}
	offset = ((dmiptexlump_t*) g_dtexdata.data())->dataofs[miptex];
	size = g_texdatasize - offset;
	if (offset < 0
		|| g_dtexdata.data() + offset
			< (std::byte const *) &((dmiptexlump_t const *) g_dtexdata.data(
									))
				  ->dataofs[numtextures]
		|| size < (int) sizeof(miptex_t)) {
		return -1;
	}

	miptex_t const & mt = (miptex_t const &) (g_dtexdata[offset]);

	std::optional<texinfo_count> maybeTexinfoIndex
		= mt.name.original_texinfo_index_for_embedded_lightmap();
	if (!maybeTexinfoIndex) {
		return no_texinfo;
	}

	if (maybeTexinfoIndex.value() >= g_numtexinfo) {
		Warning(
			"Invalid index of original texinfo: %d parsed from texture "
			"name "
			"'%s'.",
			maybeTexinfoIndex.value(),
			mt.name.c_str()
		);
		return -1;
	}

	return maybeTexinfoIndex.value();
}

texinfo_count ParseTexinfoForFace(dface_t const & f) {
	texinfo_count texinfo = f.texinfo;
	int miptex = g_texinfo[texinfo].miptex;
	if (miptex != -1) {
		texinfo_count texinfo2 = ParseImplicitTexinfoFromTexture(miptex);
		if (texinfo2 != no_texinfo) {
			texinfo = texinfo2;
		}
	}

	return texinfo;
}

// =====================================================================================
//  DeleteEmbeddedLightmaps
//      removes all "?_rad*" textures that are created by hlrad
//      this function does nothing if the map has no textures with name
//      "?_rad*"
// =====================================================================================
void DeleteEmbeddedLightmaps() {
	int countrestoredfaces = 0;
	int countremovedtexinfos = 0;
	int countremovedtextures = 0;
	int numtextures = g_texdatasize
		? ((dmiptexlump_t*) g_dtexdata.data())->nummiptex
		: 0;

	// Step 1: parse the original texinfo index stored in each "?_rad*"
	// texture
	//         and restore the texinfo for the faces that have had their
	//         lightmap embedded

	for (int i = 0; i < g_numfaces; i++) {
		dface_t& f = g_dfaces[i];
		texinfo_count const texinfo = ParseTexinfoForFace(f);
		if (texinfo != f.texinfo) {
			f.texinfo = texinfo;
			countrestoredfaces++;
		}
	}

	// Step 2: remove redundant texinfo
	{
		bool* texinfoused = (bool*) malloc(g_numtexinfo * sizeof(bool));
		hlassume(texinfoused != nullptr, assume_NoMemory);

		for (texinfo_count i = 0; i < g_numtexinfo; ++i) {
			texinfoused[i] = false;
		}
		for (int i = 0; i < g_numfaces; i++) {
			texinfo_count const texinfo = g_dfaces[i].texinfo;

			if (texinfo >= g_numtexinfo) {
				continue;
			}
			texinfoused[texinfo] = true;
		}
		texinfo_count i;
		for (i = g_numtexinfo; i > 0; i--) {
			auto const miptex = g_texinfo[i - 1].miptex;

			if (texinfoused[i - 1]) {
				break; // still used by a face; should not remove
					   // this texinfo
			}
			if (miptex < 0 || miptex >= numtextures) {
				break; // invalid; should not remove this texinfo
			}
			if (ParseImplicitTexinfoFromTexture(miptex) == no_texinfo) {
				break; // not added by hlrad; should not remove this
					   // texinfo
			}
			countremovedtexinfos++;
		}
		g_numtexinfo = i; // shrink g_texinfo
		free(texinfoused);
	}

	// Step 3: remove redundant textures
	{
		int numremaining; // number of remaining textures
		bool* textureused = (bool*) malloc(numtextures * sizeof(bool));
		hlassume(textureused != nullptr, assume_NoMemory);

		for (int i = 0; i < numtextures; i++) {
			textureused[i] = false;
		}
		for (int i = 0; i < g_numtexinfo; i++) {
			auto const miptex = g_texinfo[i].miptex;

			if (miptex < 0 || miptex >= numtextures) {
				continue;
			}
			textureused[miptex] = true;
		}
		int i;
		for (i = numtextures - 1; i > -1; i--) {
			if (textureused[i]
				|| ParseImplicitTexinfoFromTexture(i) == no_texinfo) {
				break; // should not remove this texture
			}
			countremovedtextures++;
		}
		numremaining = i + 1;
		free(textureused);

		if (numremaining < numtextures) {
			dmiptexlump_t* texdata = (dmiptexlump_t*) g_dtexdata.data();
			std::byte* dataaddr
				= (std::byte*) &texdata->dataofs[texdata->nummiptex];
			int datasize = (g_dtexdata.data()
							+ texdata->dataofs[numremaining])
				- dataaddr;
			std::byte* newdataaddr
				= (std::byte*) &texdata->dataofs[numremaining];
			memmove(newdataaddr, dataaddr, datasize);
			g_texdatasize = (newdataaddr + datasize) - g_dtexdata.data();
			texdata->nummiptex = numremaining;
			for (i = 0; i < numremaining; i++) {
				if (texdata->dataofs[i] < 0) // bad texture
				{
					continue;
				}
				texdata->dataofs[i] += newdataaddr - dataaddr;
			}

			numtextures = texdata->nummiptex;
		}
	}

	if (countrestoredfaces > 0 || countremovedtexinfos > 0
		|| countremovedtextures > 0) {
		Log("DeleteEmbeddedLightmaps: restored %d faces, removed %d "
			"texinfos "
			"and %d textures.\n",
			countrestoredfaces,
			countremovedtexinfos,
			countremovedtextures);
	}
}

/*
 * ================
 * ParseEntity
 * ================
 */

// Each tool should has its own version of GetParamsFromEnt which
// parseentity calls
extern void GetParamsFromEnt(entity_t* mapent);

void add_entity_from_bsp_file(parsed_entity& parsedEntity) {
	if (g_numentities == MAX_MAP_ENTITIES) {
		Error("g_numentities == MAX_MAP_ENTITIES");
	}

	entity_t* mapent = &g_entities[g_numentities];
	g_numentities++;
	mapent->keyValues = parsedEntity.keyValues;

	if (key_value_is(mapent, u8"classname", u8"info_compile_parameters")) {
		Log("Map entity info_compile_parameters detected, using compile "
			"settings\n");
		GetParamsFromEnt(mapent);
	}
	// Ugly code
	if (key_value_starts_with(mapent, u8"classname", u8"light")
		&& has_key_value(mapent, u8"_tex")) {
		set_key_value(mapent, u8"convertto", get_classname(*mapent));
		set_key_value(mapent, u8"classname", u8"light_surface");
	}
	if (key_value_is(mapent, u8"convertfrom", u8"light_shadow")
		|| key_value_is(mapent, u8"convertfrom", u8"light_bounce")) {
		set_key_value(
			mapent, u8"convertto", value_for_key(mapent, u8"classname")
		);
		set_key_value(
			mapent, u8"classname", value_for_key(mapent, u8"convertfrom")
		);
		DeleteKey(mapent, u8"convertfrom");
	}
	if (classname_is(mapent, u8"light_environment")) {
		if (key_value_is(mapent, u8"convertfrom", u8"info_sunlight")) {
			*mapent = entity_t{};
			g_numentities--;
		} else if (IntForKey(mapent, u8"_fake")) {
			set_key_value(mapent, u8"classname", u8"info_sunlight");
		}
	}
}

// Parses the dentdata string into entities
void parse_entities_from_bsp_file() {
	g_numentities = 0;

	map_entity_parser parser{ { g_dentdata.data(), g_entdatasize } };
	parse_entity_outcome parseOutcome;
	parsed_entity parsedEntity;
	while ((parseOutcome = parser.parse_entity(parsedEntity))
		   == parse_entity_outcome::entity_parsed) {
		add_entity_from_bsp_file(parsedEntity);
	}

	if (parseOutcome == parse_entity_outcome::bad_input) {
		Error(
			"MAP parsing error near %s %i",
			(char const *) parser.remaining_input().substr(0, 80).data(),
			(int) parser.remaining_input()[0]
		);
	} else if (parseOutcome
			   == parse_entity_outcome::not_valve220_map_format) {
		Error(
			"It looks like you are trying to compile a map made with a very old editor or one which outputs incompatible .map files. The compiler supports only the Valve220 .map format. Try opening the .map in Hammer Editor 3.5, J.A.C.K., or another modern editor, and exporting the .map again.\n"
		);
	}
}

static entity_key_value const * get_key_value_pointer(
	entity_t const & ent, std::u8string_view key
) noexcept {
	if (!key.empty()) {
		for (entity_key_value const & kv : ent.keyValues) {
			if (kv.key() == key) {
				return &kv;
			}
		}
	}
	return nullptr;
}

static entity_key_value*
get_key_value_pointer(entity_t& ent, std::u8string_view key) noexcept {
	return (entity_key_value*) get_key_value_pointer(
		(entity_t const &) ent, key
	);
}

void DeleteKey(entity_t* ent, std::u8string_view key) {
	entity_key_value* kv = get_key_value_pointer(*ent, key);
	if (kv) {
		kv->remove();
	}
}

void set_key_value(entity_t* ent, entity_key_value&& newKeyValue) {
	entity_key_value* replaceable{ nullptr };
	for (entity_key_value& kv : ent->keyValues) {
		if (kv.is_removed()) {
			replaceable = &kv;
		} else if (kv.key() == newKeyValue.key()) {
			replaceable = &kv;
			break;
		}
	}

	if (replaceable) {
		*replaceable = std::move(newKeyValue);
	} else {
		ent->keyValues.emplace_back(std::move(newKeyValue));
	}
}

void set_key_value(
	entity_t* ent, std::u8string_view key, std::u8string_view value
) {
	set_key_value(ent, std::move(entity_key_value{ key, value }));
}

// =====================================================================================
//  ValueForKey
//      returns the value for a passed entity and key
// =====================================================================================
char8_t const * ValueForKey(entity_t const * ent, std::u8string_view key) {
	entity_key_value const * kv = get_key_value_pointer(*ent, key);
	if (kv) {
		return kv->value().data();
	}
	return u8"";
}

std::u8string_view
value_for_key(entity_t const * ent, std::u8string_view key) {
	entity_key_value const * kv = get_key_value_pointer(*ent, key);
	if (kv) {
		return kv->value();
	}
	return u8"";
}

std::u8string_view get_targetname(entity_t const & ent) {
	return value_for_key(&ent, u8"targetname");
}

bool has_key_value(entity_t const * ent, std::u8string_view key) {
	return !key_value_is_empty(ent, key);
}

bool key_value_is_empty(entity_t const * ent, std::u8string_view key) {
	return get_key_value_pointer(*ent, key) == nullptr;
}

bool key_value_is(
	entity_t const * ent, std::u8string_view key, std::u8string_view value
) {
	return value_for_key(ent, key) == value;
}

bool key_value_starts_with(
	entity_t const * ent, std::u8string_view key, std::u8string_view prefix
) {
	return value_for_key(ent, key).starts_with(prefix);
}

bool classname_is(entity_t const * ent, std::u8string_view classname) {
	return key_value_is(ent, u8"classname", classname);
}

std::u8string_view get_classname(entity_t const & ent) {
	return value_for_key(&ent, u8"classname");
}

// =====================================================================================
//  IntForKey
// =====================================================================================
std::int32_t
IntForKey(entity_t const * ent, std::u8string_view key) noexcept {
	std::u8string_view const valueString{ value_for_key(ent, key) };

	std::int32_t result{};
	std::from_chars(
		(char const *) valueString.begin(),
		(char const *) valueString.end(),
		result
	);
	return result;
}

bool bool_key_value(entity_t const & ent, std::u8string_view key) noexcept {
	std::u8string_view const valueString{ value_for_key(&ent, key) };
	return !valueString.empty() && valueString != u8"0";
}

std::optional<double> clamp_double_key_value(
	entity_t const & ent, std::u8string_view key, double min, double max
) noexcept {
	std::u8string_view const valueString{ value_for_key(&ent, key) };

	// TODO: Once we no longer care about Clang < 20 support, we can use
	// std::from_chars for floating-point numbers too

	std::string zeroTerminatedCopy{ (char const *) valueString.data(),
									valueString.length() };

	char* rest = zeroTerminatedCopy.data();
	double const parsed{ std::strtod(zeroTerminatedCopy.data(), &rest) };
	bool const parsingFailed = rest == zeroTerminatedCopy.data();
	if (parsingFailed || std::isnan(parsed)) [[unlikely]] {
		return std::nullopt;
	}

	return std::clamp(parsed, min, max);
}

std::optional<std::uint64_t> clamp_unsigned_integer_from_string_key_value(
	entity_t const & ent,
	std::u8string_view key,
	std::uint64_t min,
	std::uint64_t max
) noexcept {
	std::u8string_view const valueString{ value_for_key(&ent, key) };

	return clamp_unsigned_integer_from_string(
		value_for_key(&ent, key), min, max
	);
}

std::optional<std::int64_t> clamp_signed_integer_from_string_key_value(
	entity_t const & ent,
	std::u8string_view key,
	std::int64_t min,
	std::int64_t max
) noexcept {
	return clamp_signed_integer_from_string(
		value_for_key(&ent, key), min, max
	);
}

float float_for_key(entity_t const & ent, std::u8string_view key) noexcept {
	return atof((char const *) value_for_key(&ent, key).data());
}

double3_array
get_double3_for_key(entity_t const & ent, std::u8string_view key) {
	double3_array result{};
	sscanf(
		(char const *) value_for_key(&ent, key).data(),
		"%lf %lf %lf",
		&result[0],
		&result[1],
		&result[2]
	);
	return result;
}

float3_array
get_float3_for_key(entity_t const & ent, std::u8string_view key) {
	float3_array result{};
	sscanf(
		(char const *) value_for_key(&ent, key).data(),
		"%f %f %f",
		&result[0],
		&result[1],
		&result[2]
	);
	return result;
}

// =====================================================================================
//  FindTargetEntity
//
// =====================================================================================
std::optional<std::reference_wrapper<entity_t>>
find_target_entity(std::u8string_view target) {
	if (target.empty()) {
		return std::nullopt;
	}

	for (entity_t& ent : std::span(&g_entities[0], g_numentities)) {
		if (key_value_is(&ent, u8"targetname", target)) {
			return ent;
		}
	}
	return std::nullopt;
}

void dtexdata_init() {
	g_dtexdata.resize(g_max_map_miptex, std::byte(0));
}

// =====================================================================================
//  GetTextureByNumber
//      Touchy function, can fail with a page fault if all the data isnt
//      kosher (i.e. map was compiled with missing textures)
// =====================================================================================

wad_texture_name get_texture_by_number(texinfo_count texturenumber) {
	if (texturenumber == no_texinfo) {
		return wad_texture_name{};
	}

	texinfo_t const * info = &g_texinfo[texturenumber];
	std::int32_t ofs
		= ((dmiptexlump_t*) g_dtexdata.data())->dataofs[info->miptex];
	miptex_t const * miptex = (miptex_t*) (&g_dtexdata[ofs]);

	return miptex->name;
}

// =====================================================================================
//  EntityForModel
//      returns entity addy for given modelnum
// =====================================================================================
entity_t* EntityForModel(int const modnum) {
	std::array<char8_t, 16> name;

	snprintf((char*) name.data(), name.size(), "*%i", modnum);
	// search the entities for one using modnum
	for (std::size_t i = 0; i < g_numentities; i++) {
		if (key_value_is(
				&g_entities[i], u8"model", std::u8string_view(name.data())
			)) {
			return &g_entities[i];
		}
	}

	return &g_entities[0];
}
