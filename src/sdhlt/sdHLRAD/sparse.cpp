#include "qrad.h"



struct sparse_row_t
{
    std::uint32_t        offset:24;
    std::uint32_t        values:8;
};

typedef std::vector<sparse_row_t> sparse_column_t;

std::vector<sparse_column_t> s_vismatrix;

// Vismatrix protected
static unsigned IsVisbitInArray(std::uint32_t x, std::uint32_t y)
{
    int y_byte = y / 8;
    const sparse_column_t& column = s_vismatrix[x];

    if (column.empty())
    {
        return -1;
    }



    // Binary search to find visbit
	const sparse_row_t* base = column.data();
	std::size_t n = column.size();
	while (n > 1) {
			std::size_t middle = n / 2;
			base += (y_byte < base[middle].offset) ? 0 : middle;
			n -= middle;
	}
	const std::size_t result = base - column.data();
	const bool match = base->offset == y_byte;
	
	return match ? result : -1;
}

static void SetVisColumn (int patchnum, bool uncompressedcolumn[MAX_SPARSE_VISMATRIX_PATCHES])
{
	int mbegin;
	int m;
	int i;
	unsigned int bits;
	
	sparse_column_t& column = s_vismatrix[patchnum];
	if (!column.empty())
	{
		Error ("SetVisColumn: column has been set");
	}

    std::size_t rowCount = 0;
	for (mbegin = 0; mbegin < g_num_patches; mbegin += 8)
	{
		bits = 0;
		for (m = mbegin; m < mbegin + 8; m++)
		{
			if (m >= g_num_patches)
			{
				break;
			}
			if (uncompressedcolumn[m]) // visible
			{
				if (m < patchnum)
				{
					Error ("SetVisColumn: invalid parameter: m < patchnum");
				}
				bits |= (1 << (m - mbegin));
			}
		}
		if (bits)
		{
            rowCount++;
		}
	}

	if (!rowCount)
	{
		return;
	}

    column.resize(rowCount);

	i = 0;
	for (mbegin = 0; mbegin < g_num_patches; mbegin += 8)
	{
		bits = 0;
		for (m = mbegin; m < mbegin + 8; m++)
		{
			if (m >= g_num_patches)
			{
				break;
			}
			if (uncompressedcolumn[m]) // Visible
			{
				bits |= (1 << (m - mbegin));
			}
		}
		if (bits)
		{
			column[i].offset = mbegin / 8;
			column[i].values = bits;
			i++;
		}
	}
	if (i != column.size())
	{
		Error ("SetVisColumn: internal error");
	}
}

// Vismatrix public
static bool CheckVisBitSparse(std::uint32_t x, std::uint32_t y
								  , vec3_array &transparency_out
								  , unsigned int &next_index,
								  const std::vector<vec3_array>& transparencyList
								  )
{
    VectorFill(transparency_out, 1.0);

    if (x == y)
    {
        return 1;
    }

    const std::uint32_t a = x;
    const std::uint32_t b = y;

    if (x > y)
    {
        x = b;
        y = a;
    }

    if (x > g_num_patches)
    {
        Warning("in CheckVisBit(), x > num_patches");
    }
    if (y > g_num_patches)
    {
        Warning("in CheckVisBit(), y > num_patches");
    }
    int offset = IsVisbitInArray(x, y);
    if (offset != -1)
    {
    	if(g_customshadow_with_bouncelight)
    	{
    	     GetTransparency(a, b, transparency_out, next_index, transparencyList);
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
static void     TestPatchToFace(const unsigned patchnum, const int facenum, const int head
								, byte *pvs
								, bool uncompressedcolumn[MAX_SPARSE_VISMATRIX_PATCHES],
								std::vector<vec3_array>& transparencyList
								)
{
    patch_t*        patch = &g_patches[patchnum];
    patch_t*        patch2 = g_face_patches[facenum];

    // if emitter is behind that face plane, skip all patches

    if (patch2)
    {
        const dplane_t* plane2 = getPlaneFromFaceNumber(facenum);

		if (DotProduct (patch->origin, plane2->normal) > PatchPlaneDist (patch2) + ON_EPSILON - patch->emitter_range)
        {
            // we need to do a real test
            const dplane_t* plane = getPlaneFromFaceNumber(patch->faceNumber);

            for (; patch2; patch2 = patch2->next)
            {
                unsigned        m = patch2 - g_patches;

                vec3_array		transparency = {1.0,1.0,1.0};
				int opaquestyle = -1;

                // check vis between patch and patch2
                // if bit has not already been set
                //  && v2 is not behind light plane
                //  && v2 is visible from v1
                if (m > patchnum)
				{
					if (patch2->leafnum == 0 || !(pvs[(patch2->leafnum - 1) >> 3] & (1 << ((patch2->leafnum - 1) & 7))))
					{
						continue;
					}
					vec3_array origin1, origin2;
					vec3_array delta;
					vec_t dist;
					VectorSubtract (patch->origin, patch2->origin, delta);
					dist = VectorLength (delta);
					if (dist < patch2->emitter_range - ON_EPSILON)
					{
						GetAlternateOrigin (patch->origin, plane->normal, patch2, origin2);
					}
					else
					{
						VectorCopy (patch2->origin, origin2);
					}
					if (DotProduct (origin2, plane->normal) <= PatchPlaneDist (patch) + MINIMUM_PATCH_DISTANCE)
					{
						continue;
					}
					if (dist < patch->emitter_range - ON_EPSILON)
					{
						GetAlternateOrigin (patch2->origin, plane2->normal, patch, origin1);
					}
					else
					{
						VectorCopy (patch->origin, origin1);
					}
					if (DotProduct (origin1, plane2->normal) <= PatchPlaneDist (patch2) + MINIMUM_PATCH_DISTANCE)
					{
						continue;
					}
                    if (TestLine(
						origin1, origin2
						) != CONTENTS_EMPTY)
					{
						continue;
					}
                    if (TestSegmentAgainstOpaqueList(
						origin1, origin2
						, transparency
						, opaquestyle
					))
					{
						continue;
					}

					if (opaquestyle != -1)
					{
						AddStyleToStyleArray (m, patchnum, opaquestyle);
						AddStyleToStyleArray (patchnum, m, opaquestyle);
					}
                                        

                    if(g_customshadow_with_bouncelight && !VectorCompare(transparency, {1.0,1.0,1.0}) )
                    {
                    	AddTransparencyToRawArray(patchnum, m, transparency, transparencyList);
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

static void     BuildVisLeafs(int threadnum)
{
    std::array<std::byte, (MAX_MAP_LEAFS + 7) / 8> pvs;
	std::unique_ptr<bool[]> uncompressedcolumn = std::make_unique<bool[]>(MAX_SPARSE_VISMATRIX_PATCHES);

    while (1)
    {
        //
        // build a minimal BSP tree that only
        // covers areas relevent to the PVS
        //
        int i = GetThreadWork();
        if (i == -1)
        {
            break;
        }
        i++; // skip leaf 0
        const dleaf_t& srcleaf = g_dleafs[i];
        if (!g_visdatasize)
		{
			memset (pvs.data(), 255, (g_dmodels[0].visleafs + 7) / 8);
		}
		else
		{
		if (srcleaf.visofs == -1)
		{
			Developer (DEVELOPER_LEVEL_ERROR, "Error: No visdata for leaf %d\n", i);
			continue;
		}
        DecompressVis((byte*) &g_dvisdata[srcleaf.visofs], (byte*) pvs.data(), sizeof(pvs));
		}

        //
        // go through all the faces inside the
        // leaf, and process the patches that
        // actually have origins inside
        //
		for (int facenum = 0; facenum < g_numfaces; facenum++)
		{
			for (const patch_t* patch = g_face_patches[facenum]; patch; patch = patch->next)
			{
				if (patch->leafnum != i)
					continue;
				std::uint32_t patchnum = patch - g_patches;
				for (std::size_t m = 0; m < g_num_patches; m++)
				{
					uncompressedcolumn[m] = false;
				}
				for (int facenum2 = facenum + 1; facenum2 < g_numfaces; facenum2++)
					TestPatchToFace (patchnum, facenum2, 0, (byte*) pvs.data()
									, uncompressedcolumn.get(),
									g_transparencyList
									);
				SetVisColumn (patchnum, uncompressedcolumn.get());
			}
		}

    }
}

/*
 * ==============
 * BuildVisMatrix
 * ==============
 */
static void BuildVisMatrix()
{
    s_vismatrix.resize(g_num_patches);

    NamedRunThreadsOn(g_dmodels[0].visleafs, g_estimate, BuildVisLeafs);
}

static void     FreeVisMatrix()
{
    s_vismatrix.clear();
    s_vismatrix.shrink_to_fit();
}

static void     DumpVismatrixInfo()
{
    unsigned totals[8];
    std::size_t total_vismatrix_memory;
	total_vismatrix_memory = sizeof(sparse_column_t) * g_num_patches;

    sparse_column_t* column_end = &s_vismatrix[g_num_patches];
    sparse_column_t* column = &s_vismatrix[0];

    memset(totals, 0, sizeof(totals));

    while (column < column_end)
    {
        total_vismatrix_memory += column->size() * sizeof(sparse_row_t);
        column++;
    }

    Log("%-20s: %5.1f megs\n", "visibility matrix", total_vismatrix_memory / (1024 * 1024.0));
}

//
// end old vismat.c
////////////////////////////

void            MakeScalesSparseVismatrix()
{
    char            transferfile[_MAX_PATH];

    hlassume(g_num_patches < MAX_SPARSE_VISMATRIX_PATCHES, assume_MAX_PATCHES);

	safe_snprintf(transferfile, _MAX_PATH, "%s.inc", g_Mapname);

    if (!g_incremental || !readtransfers(transferfile, g_num_patches))
    {
        // determine visibility between g_patches
        BuildVisMatrix();
        DumpVismatrixInfo();
        g_CheckVisBit = CheckVisBitSparse;

        CreateFinalTransparencyArrays("custom shadow array", g_transparencyList);
        
	if(g_rgb_transfers)
		{NamedRunThreadsOn(g_num_patches, g_estimate, MakeRGBScales);}
	else
		{NamedRunThreadsOn(g_num_patches, g_estimate, MakeScales);}
        FreeVisMatrix();
        FreeTransparencyArrays();

        if (g_incremental)
        {
            writetransfers(transferfile, g_num_patches);
        }
        else
        {
            std::filesystem::remove(transferfile);
        }
        // release visibility matrix
        DumpTransfersMemoryUsage();
		CreateFinalStyleArrays ("dynamic shadow array");
    }
}
