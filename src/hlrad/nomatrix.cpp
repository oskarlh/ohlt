#include "qrad.h"

// =====================================================================================
//  CheckVisBit
// =====================================================================================
static bool CheckVisBitNoVismatrix(
	unsigned patchnum1,
	unsigned patchnum2,
	float3_array& transparency_out,
	unsigned int&,
	std::vector<float3_array> const & transparencyList
)
// patchnum1=receiver, patchnum2=emitter.
// //HLRAD_CheckVisBitNoVismatrix_NOSWAP
{
	if (patchnum1 > g_num_patches) {
		Warning("in CheckVisBit(), patchnum1 > num_patches");
	}
	if (patchnum2 > g_num_patches) {
		Warning("in CheckVisBit(), patchnum2 > num_patches");
	}

	patch_t* patch = &g_patches[patchnum1];
	patch_t* patch2 = &g_patches[patchnum2];

	transparency_out.fill(1.0);

	// if emitter is behind that face plane, skip all patches

	if (patch2) {
		dplane_t const * plane2 = getPlaneFromFaceNumber(patch2->faceNumber
		);

		if (DotProduct(patch->origin, plane2->normal)
			> PatchPlaneDist(patch2) + ON_EPSILON - patch->emitter_range) {
			// we need to do a real test

			dplane_t const * plane = getPlaneFromFaceNumber(
				patch->faceNumber
			);

			float3_array transparency = { 1.0, 1.0, 1.0 };
			int opaquestyle = -1;

			// check vis between patch and patch2
			//  if v2 is not behind light plane
			//  && v2 is visible from v1
			float3_array origin1, origin2;
			float3_array delta;
			float dist;
			VectorSubtract(patch->origin, patch2->origin, delta);
			dist = vector_length(delta);
			if (dist < patch2->emitter_range - ON_EPSILON) {
				GetAlternateOrigin(
					patch->origin, plane->normal, patch2, origin2
				);
			} else {
				VectorCopy(patch2->origin, origin2);
			}
			if (DotProduct(origin2, plane->normal)
				<= PatchPlaneDist(patch) + MINIMUM_PATCH_DISTANCE) {
				return false;
			}
			if (dist < patch->emitter_range - ON_EPSILON) {
				GetAlternateOrigin(
					patch2->origin, plane2->normal, patch, origin1
				);
			} else {
				VectorCopy(patch->origin, origin1);
			}
			if (DotProduct(origin1, plane2->normal)
				<= PatchPlaneDist(patch2) + MINIMUM_PATCH_DISTANCE) {
				return false;
			}
			if (TestLine(origin1, origin2) != CONTENTS_EMPTY) {
				return false;
			}
			if (TestSegmentAgainstOpaqueList(
					origin1, origin2, transparency, opaquestyle
				)) {
				return false;
			}

			{
				if (opaquestyle != -1) {
					AddStyleToStyleArray(patchnum1, patchnum2, opaquestyle);
				}
				if (g_customshadow_with_bouncelight) {
					VectorCopy(transparency, transparency_out);
				}
				return true;
			}
		}
	}

	return false;
}

bool CheckVisBitBackwards(
	unsigned receiver,
	unsigned emitter,
	float3_array const & backorigin,
	float3_array const & backnormal,
	float3_array& transparency_out
) {
	patch_t* patch = &g_patches[receiver];
	patch_t* emitpatch = &g_patches[emitter];

	transparency_out.fill(1.0);

	if (emitpatch) {
		dplane_t const * emitplane = getPlaneFromFaceNumber(
			emitpatch->faceNumber
		);

		if (DotProduct(backorigin, emitplane->normal)
			> (PatchPlaneDist(emitpatch) + MINIMUM_PATCH_DISTANCE)) {
			float3_array transparency = { 1.0, 1.0, 1.0 };
			int opaquestyle = -1;

			float3_array emitorigin;
			float3_array delta;
			float dist;
			VectorSubtract(backorigin, emitpatch->origin, delta);
			dist = vector_length(delta);
			if (dist < emitpatch->emitter_range - ON_EPSILON) {
				GetAlternateOrigin(
					backorigin, backnormal, emitpatch, emitorigin
				);
			} else {
				VectorCopy(emitpatch->origin, emitorigin);
			}
			if (DotProduct(emitorigin, backnormal)
				<= DotProduct(backorigin, backnormal)
					+ MINIMUM_PATCH_DISTANCE) {
				return false;
			}
			if (TestLine(backorigin, emitorigin) != CONTENTS_EMPTY) {
				return false;
			}
			if (TestSegmentAgainstOpaqueList(
					backorigin, emitorigin, transparency, opaquestyle
				)) {
				return false;
			}

			{
				if (opaquestyle != -1) {
					AddStyleToStyleArray(receiver, emitter, opaquestyle);
				}
				if (g_customshadow_with_bouncelight) {
					VectorCopy(transparency, transparency_out);
				}
				return true;
			}
		}
	}

	return false;
}

//
// end old vismat.c
////////////////////////////

void MakeScalesNoVismatrix() {
	char transferfile[_MAX_PATH];

	hlassume(g_num_patches < MAX_PATCHES, assume_MAX_PATCHES);

	safe_snprintf(transferfile, _MAX_PATH, "%s.inc", g_Mapname);

	if (!g_incremental || !readtransfers(transferfile, g_num_patches)) {
		g_CheckVisBit = CheckVisBitNoVismatrix;
		if (g_rgb_transfers) {
			NamedRunThreadsOn(g_num_patches, g_estimate, MakeRGBScales);
		} else {
			NamedRunThreadsOn(g_num_patches, g_estimate, MakeScales);
		}

		if (g_incremental) {
			writetransfers(transferfile, g_num_patches);
		} else {
			std::filesystem::remove(transferfile);
		}
		DumpTransfersMemoryUsage();
		CreateFinalStyleArrays("dynamic shadow array");
	}
}
