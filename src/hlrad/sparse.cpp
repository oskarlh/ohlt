#include "hlrad.h"
#include "log.h"
#include "threads.h"

struct sparse_row_t final {
	std::uint32_t offset : 24;
	std::uint32_t values : 8;
};

using sparse_column_t = std::vector<sparse_row_t>;

std::vector<sparse_column_t> s_vismatrix;

// Vismatrix protected
static unsigned IsVisbitInArray(std::uint32_t x, std::uint32_t y) {
	int y_byte = y / 8;
	sparse_column_t const & column = s_vismatrix[x];

	if (column.empty()) {
		return -1;
	}

	// Binary search to find visbit
	sparse_row_t const * base = column.data();
	std::size_t n = column.size();
	while (n > 1) {
		std::size_t middle = n / 2;
		base += (y_byte < base[middle].offset) ? 0 : middle;
		n -= middle;
	}
	std::size_t const result = base - column.data();
	bool const match = base->offset == y_byte;

	return match ? result : -1;
}

static void SetVisColumn(
	int patchnum, bool uncompressedcolumn[MAX_SPARSE_VISMATRIX_PATCHES]
) {
	int mbegin;
	int m;
	int i;
	unsigned int bits;

	sparse_column_t& column = s_vismatrix[patchnum];
	if (!column.empty()) {
		Error("SetVisColumn: column has been set");
	}

	std::size_t rowCount = 0;
	for (mbegin = 0; mbegin < g_patches.size(); mbegin += 8) {
		bits = 0;
		for (m = mbegin; m < mbegin + 8; m++) {
			if (m >= g_patches.size()) {
				break;
			}
			if (uncompressedcolumn[m]) // visible
			{
				if (m < patchnum) {
					Error("SetVisColumn: invalid parameter: m < patchnum");
				}
				bits |= (1 << (m - mbegin));
			}
		}
		if (bits) {
			rowCount++;
		}
	}

	if (!rowCount) {
		return;
	}

	column.resize(rowCount);

	i = 0;
	for (mbegin = 0; mbegin < g_patches.size(); mbegin += 8) {
		bits = 0;
		for (m = mbegin; m < mbegin + 8; m++) {
			if (m >= g_patches.size()) {
				break;
			}
			if (uncompressedcolumn[m]) // Visible
			{
				bits |= (1 << (m - mbegin));
			}
		}
		if (bits) {
			column[i].offset = mbegin / 8;
			column[i].values = bits;
			i++;
		}
	}
	if (i != column.size()) {
		Error("SetVisColumn: internal error");
	}
}

// Vismatrix public
static bool CheckVisBitSparse(
	std::uint32_t x,
	std::uint32_t y,
	float3_array& transparency_out,
	unsigned int& next_index,
	std::vector<float3_array> const & transparencyList
) {
	transparency_out.fill(1.0);

	if (x == y) {
		return 1;
	}

	std::uint32_t const a = x;
	std::uint32_t const b = y;

	if (x > y) {
		x = b;
		y = a;
	}

	if (x > g_patches.size()) {
		Warning("in CheckVisBit(), x > num_patches");
	}
	if (y > g_patches.size()) {
		Warning("in CheckVisBit(), y > num_patches");
	}
	int offset = IsVisbitInArray(x, y);
	if (offset != -1) {
		if (g_customshadow_with_bouncelight) {
			GetTransparency(
				a, b, transparency_out, next_index, transparencyList
			);
		}
		return s_vismatrix[x][offset].values & (1 << (y & 7));
	}

	return false;
}

/*
 * ==============
 * TestPatchToFace
 *
 * Sets vis bits for all patches in the face
 * ==============
 */
static void TestPatchToFace(
	unsigned const patchnum,
	int const facenum,
	int const head,
	byte* pvs,
	bool uncompressedcolumn[MAX_SPARSE_VISMATRIX_PATCHES],
	std::vector<float3_array>& transparencyList
) {
	patch_t* patch = &g_patches[patchnum];
	patch_t* patch2 = g_face_patches[facenum];

	// if emitter is behind that face plane, skip all patches

	if (patch2) {
		dplane_t const * plane2 = getPlaneFromFaceNumber(facenum);

		if (dot_product(patch->origin, plane2->normal)
		    > PatchPlaneDist(patch2) + ON_EPSILON - patch->emitter_range) {
			// we need to do a real test
			dplane_t const * plane = getPlaneFromFaceNumber(
				patch->faceNumber
			);

			for (; patch2; patch2 = patch2->next) {
				unsigned m = patch2 - &g_patches.front();

				float3_array transparency = { 1.0, 1.0, 1.0 };
				int opaquestyle = -1;

				// check vis between patch and patch2
				// if bit has not already been set
				//  && v2 is not behind light plane
				//  && v2 is visible from v1
				if (m > patchnum) {
					if (patch2->leafnum == 0
					    || !(
							pvs[(patch2->leafnum - 1) >> 3]
							& (1 << ((patch2->leafnum - 1) & 7))
						)) {
						continue;
					}
					float3_array origin1, origin2;
					float const dist = distance_between_points(
						patch->origin, patch2->origin
					);
					if (dist < patch2->emitter_range - ON_EPSILON) {
						GetAlternateOrigin(
							patch->origin, plane->normal, patch2, origin2
						);
					} else {
						origin2 = patch2->origin;
					}
					if (dot_product(origin2, plane->normal)
					    <= PatchPlaneDist(patch) + MINIMUM_PATCH_DISTANCE) {
						continue;
					}
					if (dist < patch->emitter_range - ON_EPSILON) {
						GetAlternateOrigin(
							patch2->origin, plane2->normal, patch, origin1
						);
					} else {
						origin1 = patch->origin;
					}
					if (dot_product(origin1, plane2->normal)
					    <= PatchPlaneDist(patch2)
					        + MINIMUM_PATCH_DISTANCE) {
						continue;
					}
					if (TestLine(origin1, origin2) != contents_t::EMPTY) {
						continue;
					}
					if (TestSegmentAgainstOpaqueList(
							origin1, origin2, transparency, opaquestyle
						)) {
						continue;
					}

					if (opaquestyle != -1) {
						AddStyleToStyleArray(m, patchnum, opaquestyle);
						AddStyleToStyleArray(patchnum, m, opaquestyle);
					}

					if (g_customshadow_with_bouncelight
					    && !vectors_almost_same(
							transparency, float3_array{ 1.0, 1.0, 1.0 }
						)) {
						AddTransparencyToRawArray(
							patchnum, m, transparency, transparencyList
						);
					}
					uncompressedcolumn[m] = true;
				}
			}
		}
	}
}

/*
 * ===========
 * BuildVisLeafs
 *
 * This is run by multiple threads
 * ===========
 */

static void BuildVisLeafs(int threadnum) {
	std::array<std::byte, (MAX_MAP_LEAFS + 7) / 8> pvs;
	std::unique_ptr<bool[]> uncompressedcolumn = std::make_unique<bool[]>(
		MAX_SPARSE_VISMATRIX_PATCHES
	);

	while (1) {
		//
		// build a minimal BSP tree that only
		// covers areas relevent to the PVS
		//
		int i = GetThreadWork();
		if (i == -1) {
			break;
		}
		i++; // skip leaf 0
		dleaf_t const & srcleaf = g_dleafs[i];
		if (!g_visdatasize) {
			std::fill(
				pvs.begin(),
				pvs.begin() + (g_dmodels[0].visleafs + 7) / 8,
				std::byte(0xFF)
			);
		} else if (srcleaf.visofs != -1) {
			DecompressVis(
				(byte*) &g_dvisdata[srcleaf.visofs],
				(byte*) pvs.data(),
				sizeof(pvs)
			);
		} else {
			Developer(
				developer_level::error, "Error: No visdata for leaf %d\n", i
			);
			continue;
		}

		//
		// go through all the faces inside the
		// leaf, and process the patches that
		// actually have origins inside
		//
		for (int facenum = 0; facenum < g_numfaces; facenum++) {
			for (patch_t const * patch = g_face_patches[facenum]; patch;
			     patch = patch->next) {
				if (patch->leafnum != i) {
					continue;
				}
				std::uint32_t patchnum = patch - &g_patches.front();
				for (std::size_t m = 0; m < g_patches.size(); m++) {
					uncompressedcolumn[m] = false;
				}
				for (int facenum2 = facenum + 1; facenum2 < g_numfaces;
				     facenum2++) {
					TestPatchToFace(
						patchnum,
						facenum2,
						0,
						(byte*) pvs.data(),
						uncompressedcolumn.get(),
						g_transparencyList
					);
				}
				SetVisColumn(patchnum, uncompressedcolumn.get());
			}
		}
	}
}

/*
 * ==============
 * BuildVisMatrix
 * ==============
 */
static void BuildVisMatrix() {
	s_vismatrix.resize(g_patches.size());

	NamedRunThreadsOn(g_dmodels[0].visleafs, g_estimate, BuildVisLeafs);
}

static void FreeVisMatrix() {
	s_vismatrix.clear();
	s_vismatrix.shrink_to_fit();
}

static void DumpVismatrixInfo() {
	std::size_t total_vismatrix_memory;
	total_vismatrix_memory = sizeof(sparse_column_t) * g_patches.size();

	sparse_column_t* column_end = &s_vismatrix[g_patches.size()];
	sparse_column_t* column = &s_vismatrix[0];

	while (column < column_end) {
		total_vismatrix_memory += column->size() * sizeof(sparse_row_t);
		column++;
	}

	Log("%-20s: %5.1f megs\n",
	    "visibility matrix",
	    total_vismatrix_memory / (1024 * 1024.0));
}

//
// end old vismat.c
////////////////////////////

void MakeScalesSparseVismatrix() {
	hlassume(
		g_patches.size() < MAX_SPARSE_VISMATRIX_PATCHES,
		assume_msg::exceeded_MAX_PATCHES
	);

	std::filesystem::path const transferFilePath{
		path_to_temp_file_with_extension(g_Mapname, u8".inc").c_str()
	};

	if (!g_incremental
	    || !readtransfers(transferFilePath.c_str(), g_patches.size())) {
		// determine visibility between g_patches
		BuildVisMatrix();
		DumpVismatrixInfo();
		g_CheckVisBit = CheckVisBitSparse;

		CreateFinalTransparencyArrays(
			"custom shadow array", g_transparencyList
		);

		if (g_rgb_transfers) {
			NamedRunThreadsOn(g_patches.size(), g_estimate, MakeRGBScales);
		} else {
			NamedRunThreadsOn(g_patches.size(), g_estimate, MakeScales);
		}
		FreeVisMatrix();
		FreeTransparencyArrays();

		if (g_incremental) {
			writetransfers(transferFilePath.c_str(), g_patches.size());
		} else {
			std::filesystem::remove(transferFilePath);
		}
		// release visibility matrix
		DumpTransfersMemoryUsage();
		CreateFinalStyleArrays("dynamic shadow array");
	}
}
