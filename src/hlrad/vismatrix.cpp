#include "hlrad.h"
#include "log.h"
#include "threads.h"

////////////////////////////
// begin old vismat.c
//
#define HALFBIT

// =====================================================================================
//
//      VISIBILITY MATRIX
//      Determine which patches can see each other
//      Use the PVS to accelerate if available
//
// =====================================================================================

static byte* s_vismatrix;

// =====================================================================================
//  TestPatchToFace
//      Sets vis bits for all patches in the face
// =====================================================================================
static void TestPatchToFace(
	unsigned const patchnum,
	int const facenum,
	int const head,
	unsigned int const bitpos,
	byte* pvs,
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
				unsigned m = patch2 - g_patches;

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
					// Log("SDF::3\n");

					// patchnum can see patch m
					unsigned bitset = bitpos + m;

					if (g_customshadow_with_bouncelight
						&& !vectors_almost_same(
							transparency, float3_array{ 1.0, 1.0, 1.0 }
						))
					// zhlt3.4: if(g_customshadow_with_bouncelight &&
					// vectors_almost_same(transparency, {1.0,1.0,1.0})) .
					// --vluzacn
					{
						AddTransparencyToRawArray(
							patchnum, m, transparency, transparencyList
						);
					}

					ThreadLock(); //--vluzacn
					s_vismatrix[bitset >> 3] |= 1 << (bitset & 7);
					ThreadUnlock(); //--vluzacn
				}
			}
		}
	}
}

// =====================================================================================
// BuildVisLeafs
//      This is run by multiple threads
// =====================================================================================

static void BuildVisLeafs(int threadnum) {
	int i;
	int lface, facenum, facenum2;
	std::array<std::byte, (MAX_MAP_LEAFS + 7) / 8> pvs;
	dleaf_t* srcleaf;
	dleaf_t* leaf;
	patch_t* patch;
	int head;
	unsigned bitpos;
	unsigned patchnum;

	while (1) {
		//
		// build a minimal BSP tree that only
		// covers areas relevent to the PVS
		//
		i = GetThreadWork();
		if (i == -1) {
			break;
		}
		i++; // skip leaf 0
		srcleaf = &g_dleafs[i];
		if (!g_visdatasize) {
			std::fill(
				pvs.begin(),
				pvs.begin() + (g_dmodels[0].visleafs + 7) / 8,
				std::byte(0xFF)
			);
		} else if (srcleaf->visofs != -1) {
			DecompressVis(
				(byte*) &g_dvisdata[srcleaf->visofs],
				(byte*) pvs.data(),
				sizeof(pvs)
			);
		} else {
			Developer(
				developer_level::error, "Error: No visdata for leaf %d\n", i
			);
			continue;
		}
		head = 0;

		//
		// go through all the faces inside the
		// leaf, and process the patches that
		// actually have origins inside
		//
		for (facenum = 0; facenum < g_numfaces; facenum++) {
			for (patch = g_face_patches[facenum]; patch;
				 patch = patch->next) {
				if (patch->leafnum != i) {
					continue;
				}
				patchnum = patch - g_patches;
#ifdef HALFBIT
				bitpos = patchnum * g_num_patches
					- (patchnum * (patchnum + 1)) / 2;
#else
				bitpos = patchnum * g_num_patches;
#endif
				for (facenum2 = facenum + 1; facenum2 < g_numfaces;
					 facenum2++) {
					TestPatchToFace(
						patchnum,
						facenum2,
						head,
						bitpos,
						(byte*) pvs.data(),
						g_transparencyList
					);
				}
			}
		}
	}
}

// =====================================================================================
// BuildVisMatrix
// =====================================================================================
static void BuildVisMatrix() {
	int c;

#ifdef HALFBIT
	c = ((g_num_patches + 1) * (g_num_patches + 1)) / 16;
	c += 1; //--vluzacn
#else
	c = g_num_patches * ((g_num_patches + 7) / 8);
#endif

	Log("%-20s: %5.1f megs\n", "visibility matrix", c / (1024 * 1024.0));

	s_vismatrix = (byte*) new std::byte[c]();

	if (!s_vismatrix) {
		Log("Failed to allocate s_vismatrix");
		hlassume(s_vismatrix != nullptr, assume_NoMemory);
	}

	NamedRunThreadsOn(g_dmodels[0].visleafs, g_estimate, BuildVisLeafs);
}

static void FreeVisMatrix() {
	if (s_vismatrix) {
		delete[] s_vismatrix;
		s_vismatrix = nullptr;
	}
}

// =====================================================================================
// CheckVisBit
// =====================================================================================
static bool CheckVisBitVismatrix(
	unsigned p1,
	unsigned p2,
	float3_array& transparency_out,
	unsigned int& next_index,
	std::vector<float3_array> const & transparencyList
) {
	unsigned bitpos;

	unsigned const a = p1;
	unsigned const b = p2;

	transparency_out.fill(1.0);

	if (p1 > p2) {
		p1 = b;
		p2 = a;
	}

	if (p1 > g_num_patches) {
		Warning("in CheckVisBit(), p1 > num_patches");
	}
	if (p2 > g_num_patches) {
		Warning("in CheckVisBit(), p2 > num_patches");
	}

#ifdef HALFBIT
	bitpos = p1 * g_num_patches - (p1 * (p1 + 1)) / 2 + p2;
#else
	bitpos = p1 * g_num_patches + p2;
#endif

	if (s_vismatrix[bitpos >> 3] & (1 << (bitpos & 7))) {
		if (g_customshadow_with_bouncelight) {
			GetTransparency(
				a, b, transparency_out, next_index, transparencyList
			);
		}
		return true;
	}

	return false;
}

//
// end old vismat.c
////////////////////////////

// =====================================================================================
// MakeScalesVismatrix
// =====================================================================================
void MakeScalesVismatrix() {
	hlassume(g_num_patches < MAX_VISMATRIX_PATCHES, assume_MAX_PATCHES);

	std::filesystem::path const transferFilePath{
		path_to_temp_file_with_extension(g_Mapname, u8".inc").c_str()
	};
	if (!g_incremental
		|| !readtransfers(transferFilePath.c_str(), g_num_patches)) {
		// determine visibility between g_patches
		BuildVisMatrix();
		g_CheckVisBit = CheckVisBitVismatrix;

		CreateFinalTransparencyArrays(
			"custom shadow array", g_transparencyList
		);

		if (g_rgb_transfers) {
			NamedRunThreadsOn(g_num_patches, g_estimate, MakeRGBScales);
		} else {
			NamedRunThreadsOn(g_num_patches, g_estimate, MakeScales);
		}
		FreeVisMatrix();
		FreeTransparencyArrays();

		if (g_incremental) {
			writetransfers(transferFilePath.c_str(), g_num_patches);
		} else {
			std::filesystem::remove(transferFilePath);
		}
		DumpTransfersMemoryUsage();
		CreateFinalStyleArrays("dynamic shadow array");
	}
}
