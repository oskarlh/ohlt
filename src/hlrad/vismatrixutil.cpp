#include "qrad.h"

#include <numbers>

funcCheckVisBit g_CheckVisBit = nullptr;
std::vector<float3_array> g_transparencyList{
	1, float3_array{ 1.0, 1.0, 1.0 }
};

size_t g_total_transfer = 0;
size_t g_transfer_index_bytes = 0;
size_t g_transfer_data_bytes = 0;

constexpr bool ENABLE_COMPRESSED_TRANSFERS = true;

int FindTransferOffsetPatchnum(
	transfer_index_t* tIndex,
	patch_t const * const patch,
	unsigned const patchnum
) {
	//
	// binary search for match
	//
	int low = 0;
	int high = patch->iIndex - 1;
	int offset;

	while (1) {
		offset = (low + high) / 2;

		if ((tIndex[offset].index + tIndex[offset].size) < patchnum) {
			low = offset + 1;
		} else if (tIndex[offset].index > patchnum) {
			high = offset - 1;
		} else {
			unsigned x;
			unsigned int rval = 0;
			transfer_index_t* pIndex = tIndex;

			for (x = 0; x < offset; x++, pIndex++) {
				rval += pIndex->size + 1;
			}
			rval += patchnum - tIndex[offset].index;
			return rval;
		}
		if (low > high) {
			return -1;
		}
	}
}

static unsigned GetLengthOfRun(
	transfer_raw_index_t const * raw, transfer_raw_index_t const * const end
) {
	unsigned run_size = 0;

	while (raw < end) {
		if (((*raw) + 1) == (*(raw + 1))) {
			raw++;
			run_size++;

			if (run_size >= MAX_COMPRESSED_TRANSFER_INDEX_SIZE) {
				return run_size;
			}
		} else {
			return run_size;
		}
	}
	return run_size;
}

static transfer_index_t* CompressTransferIndicies(
	transfer_raw_index_t* tRaw, std::uint32_t rawSize, std::uint32_t* iSize
) {
	transfer_index_t* compressedArray{};
	if constexpr (ENABLE_COMPRESSED_TRANSFERS) {
		std::uint32_t size = rawSize;
		std::uint32_t compressed_count = 0;

		transfer_raw_index_t* raw = tRaw;
		transfer_raw_index_t* end = tRaw + rawSize
			- 1; // -1 since we are comparing current with next and get
				 // errors when bumping into the 'end'

		unsigned compressed_count_1 = 0;

		for (std::uint32_t x = 0; x < rawSize; x++) {
			x += GetLengthOfRun(tRaw + x, tRaw + rawSize - 1);
			compressed_count_1++;
		}

		if (!compressed_count_1) {
			return nullptr;
		}

		compressedArray = new transfer_index_t[compressed_count_1]();
		transfer_index_t* compressed = compressedArray;

		for (std::uint32_t x = 0; x < size; x++, raw++, compressed++) {
			compressed->index = (*raw);
			compressed->size = GetLengthOfRun(
				raw, end
			); // Zero based (count 0 still implies 1 item in the list, so
			   // 256 max entries result)
			raw += compressed->size;
			x += compressed->size;
			compressed_count++; // number of entries in compressed table
		}

		*iSize = compressed_count;

		if (compressed_count != compressed_count_1) {
			Error("CompressTransferIndicies: internal error");
		}

		ThreadLock();
		g_transfer_index_bytes += sizeof(transfer_index_t)
			* compressed_count;
		ThreadUnlock();

	} else {
		std::uint32_t size = rawSize;
		std::uint32_t compressed_count = 0;

		transfer_raw_index_t* raw = tRaw;
		transfer_raw_index_t* end = tRaw + rawSize;

		if (!size) {
			return nullptr;
		}

		compressedArray = (transfer_index_t*) new transfer_index_t[size]();
		transfer_index_t* compressed = compressedArray;

		for (std::uint32_t x = 0; x < size; x++, raw++, compressed++) {
			compressed->index = (*raw);
			compressed->size = 0;
			compressed_count++; // number of entries in compressed table
		}

		*iSize = compressed_count;

		ThreadLock();
		g_transfer_index_bytes += sizeof(transfer_index_t) * size;
		ThreadUnlock();
	}

	return compressedArray;
}

/*
 * =============
 * MakeScales
 *
 * This is the primary time sink.
 * It can be run multi threaded.
 * =============
 */

void MakeScales(int const threadnum) {
	int i;
	unsigned j;
	float3_array delta;
	float dist;
	int count;
	float trans;
	patch_t* patch;
	patch_t* patch2;
	float send;
	float3_array origin;
	float area;

	unsigned int fastfind_index = 0;

	float total;

	transfer_raw_index_t* tIndex;
	float* tData;

	transfer_raw_index_t* tIndex_All
		= (transfer_raw_index_t*) new transfer_index_t[g_num_patches + 1]();
	float* tData_All = (float*) new float[g_num_patches + 1]();

	count = 0;

	while (1) {
		i = GetThreadWork();
		if (i == -1) {
			break;
		}

		patch = g_patches + i;
		patch->iIndex = 0;
		patch->iData = 0;

		tIndex = tIndex_All;
		tData = tData_All;

		origin = patch->origin;
		float3_array const normal1
			= getPlaneFromFaceNumber(patch->faceNumber)->normal;

		area = patch->area;
		float3_array backorigin;
		float3_array backnormal;
		if (patch->translucent_b) {
			backorigin = vector_fma(
				normal1,
				-(g_translucentdepth + 2 * PATCH_HUNT_OFFSET),
				patch->origin
			);
			backnormal = negate_vector(normal1);
		}
		bool lighting_diversify;
		float lighting_power;
		float lighting_scale;
		int miptex = g_texinfo[g_dfaces[patch->faceNumber].texinfo].miptex;
		lighting_power = g_lightingconeinfo[miptex].power;
		lighting_scale = g_lightingconeinfo[miptex].scale;
		lighting_diversify
			= (lighting_power != 1.0 || lighting_scale != 1.0);

		// find out which patch2's will collect light
		// from patch
		// HLRAD_NOSWAP: patch collect light from patch2

		for (j = 0, patch2 = g_patches; j < g_num_patches; j++, patch2++) {
			float dot1;
			float dot2;

			float3_array transparency = { 1.0, 1.0, 1.0 };
			bool useback;
			useback = false;

			if (!g_CheckVisBit(
					i, j, transparency, fastfind_index, g_transparencyList
				)
				|| (i == j)) {
				if (patch->translucent_b) {
					if ((i == j)
						|| !CheckVisBitBackwards(
							i, j, backorigin, backnormal, transparency
						)) {
						continue;
					}
					useback = true;
				} else {
					continue;
				}
			}

			float3_array const & normal2
				= getPlaneFromFaceNumber(patch2->faceNumber)->normal;

			// calculate transferemnce
			VectorSubtract(patch2->origin, origin, delta);
			if (useback) {
				VectorSubtract(patch2->origin, backorigin, delta);
			}
			// move emitter back to its plane
			delta = vector_fma(normal2, -PATCH_HUNT_OFFSET, delta);

			dist = normalize_vector(delta);
			dot1 = dot_product(delta, normal1);
			if (useback) {
				dot1 = dot_product(delta, backnormal);
			}
			dot2 = -dot_product(delta, normal2);
			bool light_behind_surface = false;
			if (dot1 <= NORMAL_EPSILON) {
				light_behind_surface = true;
			}
			if (dot2 * dist <= MINIMUM_PATCH_DISTANCE) {
				continue;
			}

			if (lighting_diversify && !light_behind_surface) {
				dot1 = lighting_scale * pow(dot1, lighting_power);
			}
			trans = (dot1 * dot2)
				/ (dist * dist); // Inverse square falloff factoring angle
								 // between patch normals
			if (trans * patch2->area > 0.8f) {
				trans = 0.8f / patch2->area;
			}
			if (dist < patch2->emitter_range - ON_EPSILON) {
				if (light_behind_surface) {
					trans = 0.0;
				}
				float sightarea;
				float3_array receiver_origin{ origin };
				float3_array receiver_normal{ normal1 };
				fast_winding const * emitter_winding;
				if (useback) {
					receiver_origin = backorigin;
					receiver_normal = backnormal;
				}
				emitter_winding = patch2->winding;
				sightarea = CalcSightArea(
					receiver_origin,
					receiver_normal,
					emitter_winding,
					patch2->emitter_skylevel,
					lighting_power,
					lighting_scale
				);

				float frac;
				frac = dist / patch2->emitter_range;
				frac = (frac - 0.5f) * 2.0f; // make a smooth transition
											 // between the two methods
				frac = std::max((float) 0, std::min(frac, (float) 1));
				trans = frac * trans
					+ (1 - frac)
						* (sightarea / patch2->area
						); // because later we will multiply this back
			} else {
				if (light_behind_surface) {
					continue;
				}
			}

			trans *= patch2->exposure;
			// hullu: add transparency effect
			trans = trans * vector_average(transparency);
			if (patch->translucent_b) {
				if (useback) {
					trans *= vector_average(patch->translucent_v);
				} else {
					trans *= 1 - vector_average(patch->translucent_v);
				}
			}

			{ trans = trans * patch2->area; }
			if (trans <= 0.0) {
				continue;
			}

			*tData = trans;
			*tIndex = j;
			tData++;
			tIndex++;
			patch->iData++;
			count++;
		}

		// copy the transfers out
		if (patch->iData) {
			unsigned data_size = patch->iData
					* float_size[(std::size_t) g_transfer_compress_type]
				+ unused_size;

			patch->tData
				= (transfer_data_t*) new transfer_data_t[data_size]();
			patch->tIndex = CompressTransferIndicies(
				tIndex_All, patch->iData, &patch->iIndex
			);

			hlassume(patch->tData != nullptr, assume_NoMemory);
			hlassume(patch->tIndex != nullptr, assume_NoMemory);

			ThreadLock();
			g_transfer_data_bytes += data_size;
			ThreadUnlock();

			total = 1 / std::numbers::pi_v<float>;
			{
				unsigned x;
				transfer_data_t* t1 = patch->tData;
				float* t2 = tData_All;

				float f;
				for (x = 0; x < patch->iData;
					 x++,
					t1
					 += float_size[(std::size_t) g_transfer_compress_type],
					t2++) {
					f = (*t2) * total;
					float_compress(g_transfer_compress_type, t1, f);
				}
			}
		}
	}

	delete[] tIndex_All;
	tIndex_All = nullptr;
	delete[] tData_All;
	tData_All = nullptr;

	ThreadLock();
	g_total_transfer += count;
	ThreadUnlock();
}

/*
 * =============
 * SwapTransfersTask
 *
 * Change transfers from light sent out to light collected in.
 * In an ideal world, they would be exactly symetrical, but
 * because the form factors are only aproximated, then normalized,
 * they will actually be rather different.
 * =============
 */

/*
 * =============
 * MakeScales
 *
 * This is the primary time sink.
 * It can be run multi threaded.
 * =============
 */

void MakeRGBScales(int const threadnum) {
	int i;
	unsigned j;
	float3_array delta;
	float dist;
	int count;
	float3_array trans;
	float trans_one;
	patch_t* patch;
	patch_t* patch2;
	float send;
	float3_array origin;
	float area;

	unsigned int fastfind_index = 0;
	float total;

	transfer_raw_index_t* tIndex;

	// Why are the types different?
	transfer_raw_index_t* tIndex_All
		= (transfer_raw_index_t*) new transfer_index_t[g_num_patches + 1]();

	std::unique_ptr<float3_array[]> tRGBData_All
		= std::make_unique_for_overwrite<float3_array[]>(g_num_patches + 1);

	count = 0;

	while (1) {
		i = GetThreadWork();
		if (i == -1) {
			break;
		}

		patch = g_patches + i;
		patch->iIndex = 0;
		patch->iData = 0;

		tIndex = tIndex_All;

		float3_array* tRGBData = tRGBData_All.get();

		origin = patch->origin;
		float3_array const normal1
			= getPlaneFromFaceNumber(patch->faceNumber)->normal;

		area = patch->area;
		float3_array backorigin;
		float3_array backnormal;
		if (patch->translucent_b) {
			backorigin = vector_fma(
				normal1,
				-(g_translucentdepth + 2 * PATCH_HUNT_OFFSET),
				patch->origin
			);
			backnormal = negate_vector(normal1);
		}
		bool lighting_diversify;
		float lighting_power;
		float lighting_scale;
		int miptex = g_texinfo[g_dfaces[patch->faceNumber].texinfo].miptex;
		lighting_power = g_lightingconeinfo[miptex].power;
		lighting_scale = g_lightingconeinfo[miptex].scale;
		lighting_diversify
			= (lighting_power != 1.0 || lighting_scale != 1.0);

		// find out which patch2's will collect light
		// from patch
		// HLRAD_NOSWAP: patch collect light from patch2

		for (j = 0, patch2 = g_patches; j < g_num_patches; j++, patch2++) {
			float dot1;
			float dot2;
			float3_array transparency = { 1.0, 1.0, 1.0 };
			bool useback = false;

			if (!g_CheckVisBit(
					i, j, transparency, fastfind_index, g_transparencyList
				)
				|| (i == j)) {
				if (patch->translucent_b) {
					if (!CheckVisBitBackwards(
							i, j, backorigin, backnormal, transparency
						)
						|| (i == j)) {
						continue;
					}
					useback = true;
				} else {
					continue;
				}
			}

			float3_array const & normal2
				= getPlaneFromFaceNumber(patch2->faceNumber)->normal;

			// calculate transferemnce
			VectorSubtract(patch2->origin, origin, delta);
			if (useback) {
				VectorSubtract(patch2->origin, backorigin, delta);
			}
			// move emitter back to its plane
			delta = vector_fma(normal2, -PATCH_HUNT_OFFSET, delta);

			dist = normalize_vector(delta);
			dot1 = dot_product(delta, normal1);
			if (useback) {
				dot1 = dot_product(delta, backnormal);
			}
			dot2 = -dot_product(delta, normal2);
			bool light_behind_surface = false;
			if (dot1 <= NORMAL_EPSILON) {
				light_behind_surface = true;
			}
			if (dot2 * dist <= MINIMUM_PATCH_DISTANCE) {
				continue;
			}

			if (lighting_diversify && !light_behind_surface) {
				dot1 = lighting_scale * pow(dot1, lighting_power);
			}
			trans_one = (dot1 * dot2)
				/ (dist * dist); // Inverse square falloff factoring angle
								 // between patch normals

			if (trans_one * patch2->area > 0.8f) {
				trans_one = 0.8f / patch2->area;
			}
			if (dist < patch2->emitter_range - ON_EPSILON) {
				if (light_behind_surface) {
					trans_one = 0.0;
				}
				float sightarea;
				float3_array receiver_origin{ origin };
				float3_array receiver_normal{ normal1 };
				fast_winding const * emitter_winding;
				if (useback) {
					receiver_origin = backorigin;
					receiver_normal = backnormal;
				}
				emitter_winding = patch2->winding;
				sightarea = CalcSightArea(
					receiver_origin,
					receiver_normal,
					emitter_winding,
					patch2->emitter_skylevel,
					lighting_power,
					lighting_scale
				);

				float frac;
				frac = dist / patch2->emitter_range;
				frac = (frac - 0.5f) * 2.0f; // make a smooth transition
											 // between the two methods
				frac = std::max((float) 0, std::min(frac, (float) 1));
				trans_one = frac * trans_one
					+ (1 - frac)
						* (sightarea / patch2->area
						); // because later we will multiply this back
			} else if (light_behind_surface) {
				continue;
			}
			trans_one *= patch2->exposure;
			trans = vector_scale(
				transparency, trans_one
			); // hullu: add transparency effect
			if (patch->translucent_b) {
				if (useback) {
					for (int x = 0; x < 3; x++) {
						trans[x] = patch->translucent_v[x] * trans[x];
					}
				} else {
					for (int x = 0; x < 3; x++) {
						trans[x] = (1 - patch->translucent_v[x]) * trans[x];
					}
				}
			}

			if (trans_one <= 0.0) {
				continue;
			}
			trans = vector_scale(trans, patch2->area);

			*tRGBData = trans;
			*tIndex = j;
			++tRGBData;
			tIndex++;
			patch->iData++;
			count++;
		}

		// copy the transfers out
		if (patch->iData) {
			std::size_t data_size = patch->iData
					* vector_size[(std::size_t) g_rgbtransfer_compress_type]
				+ unused_size;

			patch->tRGBData
				= (rgb_transfer_data_t*) new rgb_transfer_data_t[data_size](
				);
			patch->tIndex = CompressTransferIndicies(
				tIndex_All, patch->iData, &patch->iIndex
			);

			hlassume(patch->tRGBData != nullptr, assume_NoMemory);
			hlassume(patch->tIndex != nullptr, assume_NoMemory);

			ThreadLock();
			g_transfer_data_bytes += data_size;
			ThreadUnlock();

			total = 1 / std::numbers::pi_v<float>;
			{
				unsigned x;
				rgb_transfer_data_t* t1 = patch->tRGBData;
				float3_array const * t2 = tRGBData_All.get();

				float3_array f;
				for (x = 0; x < patch->iData; x++,
					t1 += vector_size[(std::size_t
					) g_rgbtransfer_compress_type],
					++t2) {
					f = vector_scale(*t2, total);
					vector_compress(
						g_rgbtransfer_compress_type, t1, f[0], f[1], f[2]
					);
				}
			}
		}
	}

	delete[] tIndex_All;
	tIndex_All = nullptr;

	ThreadLock();
	g_total_transfer += count;
	ThreadUnlock();
}

/*
 * =============
 * SwapTransfersTask
 *
 * Change transfers from light sent out to light collected in.
 * In an ideal world, they would be exactly symetrical, but
 * because the form factors are only aproximated, then normalized,
 * they will actually be rather different.
 * =============
 */

// More human readable numbers
void DumpTransfersMemoryUsage() {
	if (g_total_transfer > 1000 * 1000) {
		Log("Transfer Lists : %11.0f : %8.2fM transfers\n",
			(float) g_total_transfer,
			(float) g_total_transfer / (1000.0f * 1000.0f));
	} else if (g_total_transfer > 1000) {
		Log("Transfer Lists : %11.0f : %8.2fk transfers\n",
			(float) g_total_transfer,
			(float) g_total_transfer / 1000.0f);
	} else {
		Log("Transfer Lists : %11.0f transfers\n",
			(float) g_total_transfer);
	}

	if (g_transfer_index_bytes > 1024 * 1024) {
		Log("       Indices : %11.0f : %8.2fM bytes\n",
			(double) g_transfer_index_bytes,
			(double) g_transfer_index_bytes / (1024.0f * 1024.0f));
	} else if (g_transfer_index_bytes > 1024) {
		Log("       Indices : %11.0f : %8.2fk bytes\n",
			(double) g_transfer_index_bytes,
			(double) g_transfer_index_bytes / 1024.0f);
	} else {
		Log("       Indices : %11.0f bytes\n",
			(double) g_transfer_index_bytes);
	}

	if (g_transfer_data_bytes > 1024 * 1024) {
		Log("          Data : %11.0f : %8.2fM bytes\n",
			(double) g_transfer_data_bytes,
			(double) g_transfer_data_bytes / (1024.0f * 1024.0f));
	} else if (g_transfer_data_bytes > 1024) {
		Log("          Data : %11.0f : %8.2fk bytes\n",
			(double) g_transfer_data_bytes,
			(double) g_transfer_data_bytes / 1024.0f);
	} else {
		Log("          Data : %11.0f bytes\n",
			(double) g_transfer_data_bytes);
	}
}
