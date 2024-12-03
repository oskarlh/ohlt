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
    int first, last, current;
    int y_byte = y / 8;
    const sparse_column_t& column = s_vismatrix[x];

    if (column.empty())
    {
        return -1;
    }

    first = 0;
    last = column.size() - 1;

    // Warning("Searching . . .");
    // binary search to find visbit
    while (1)
    {
        current = (first + last) / 2;
        const sparse_row_t& row = column[current];
        if ((row.offset) < y_byte)
        {
            first = current + 1;
        }
        else if ((row.offset) > y_byte)
        {
            last = current - 1;
        }
        else
        {
            return current;
        }
        if (first > last)
        {
            return -1;
        }
    }
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
								  , unsigned int &next_index
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
    	     GetTransparency(a, b, transparency_out, next_index);
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
								, bool uncompressedcolumn[MAX_SPARSE_VISMATRIX_PATCHES]
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
						GetAlternateOrigin (const_vec3_arg(patch->origin), plane->normal, patch2, origin2);
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
						GetAlternateOrigin (const_vec3_arg(patch2->origin), plane2->normal, patch, origin1);
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
						origin1.data(), origin2.data()
						) != CONTENTS_EMPTY)
					{
						continue;
					}
                    if (TestSegmentAgainstOpaqueList(
						origin1.data(), origin2.data()
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
                                        
                    if(g_customshadow_with_bouncelight && !VectorCompare(transparency, vec3_one) )
                    {
                    	AddTransparencyToRawArray(patchnum, m, transparency.data());
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
    int             i;
    int             lface, facenum, facenum2;
    byte            pvs[(MAX_MAP_LEAFS + 7) / 8];
    dleaf_t*        srcleaf;
    dleaf_t*        leaf;
    patch_t*        patch;
    int             head;
    unsigned        patchnum;
	std::unique_ptr<bool[]> uncompressedcolumn = std::make_unique<bool[]>(MAX_SPARSE_VISMATRIX_PATCHES);
	hlassume (uncompressedcolumn != nullptr, assume_NoMemory);

    while (1)
    {
        //
        // build a minimal BSP tree that only
        // covers areas relevent to the PVS
        //
        i = GetThreadWork();
        if (i == -1)
        {
            break;
        }
        i++;                                               // skip leaf 0
        srcleaf = &g_dleafs[i];
        if (!g_visdatasize)
		{
			memset (pvs, 255, (g_dmodels[0].visleafs + 7) / 8);
		}
		else
		{
		if (srcleaf->visofs == -1)
		{
			Developer (DEVELOPER_LEVEL_ERROR, "Error: No visdata for leaf %d\n", i);
			continue;
		}
        DecompressVis(&g_dvisdata[srcleaf->visofs], pvs, sizeof(pvs));
		}
        head = 0;

        //
        // go through all the faces inside the
        // leaf, and process the patches that
        // actually have origins inside
        //
		for (facenum = 0; facenum < g_numfaces; facenum++)
		{
			for (patch = g_face_patches[facenum]; patch; patch = patch->next)
			{
				if (patch->leafnum != i)
					continue;
				patchnum = patch - g_patches;
				for (std::size_t m = 0; m < g_num_patches; m++)
				{
					uncompressedcolumn[m] = false;
				}
				for (facenum2 = facenum + 1; facenum2 < g_numfaces; facenum2++)
					TestPatchToFace (patchnum, facenum2, head, pvs
									, uncompressedcolumn.get()
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

        CreateFinalTransparencyArrays("custom shadow array");
        
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
