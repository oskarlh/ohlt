// Cached mesh for tracing custom objects

#include "meshdesc.h"

#include "log.h"
#include "mathlib.h"
#include "time_counter.h"

#include <numbers>

constexpr bool AABB_OFFSET = false;

constexpr float SIMPLIFICATION_FACTOR_HIGH{ 0.15f };
constexpr float SIMPLIFICATION_FACTOR_MED{ 0.55f };
constexpr float SIMPLIFICATION_FACTOR_LOW{ 0.85f };

CMeshDesc ::CMeshDesc() :
	areanodes(
		std::make_unique_for_overwrite<std::array<areanode_t, AREA_NODES>>()
	) { }

CMeshDesc ::~CMeshDesc() {
	FreeMesh();
}

void CMeshDesc ::InsertLinkBefore(link_t* l, link_t* before) {
	l->next = before;
	l->prev = before->prev;
	l->prev->next = l;
	l->next->prev = l;
}

void CMeshDesc ::RemoveLink(link_t* l) {
	l->next->prev = l->prev;
	l->prev->next = l->next;
}

void CMeshDesc ::ClearLink(link_t* l) {
	l->prev = l->next = l;
}

/*
===============
CreateAreaNode

builds a uniformly subdivided tree for the given mesh size
===============
*/
areanode_t* CMeshDesc::CreateAreaNode(
	int depth, float3_array const & mins, float3_array const & maxs
) {
	areanode_t* anode{ &(*areanodes)[numareanodes++] };

	ClearLink(&anode->facets);

	if (depth == AREA_DEPTH) {
		anode->axis = -1;
		anode->children[0] = anode->children[1] = nullptr;
		return anode;
	}

	float3_array size = vector_subtract(maxs, mins);

	if (size[0] > size[1]) {
		anode->axis = 0;
	} else {
		anode->axis = 1;
	}

	anode->dist = 0.5f * (maxs[anode->axis] + mins[anode->axis]);

	float3_array mins2{ mins };
	float3_array maxs1{ maxs };

	maxs1[anode->axis] = mins2[anode->axis] = anode->dist;
	anode->children[0] = CreateAreaNode(depth + 1, mins2, maxs);
	anode->children[1] = CreateAreaNode(depth + 1, mins, maxs1);

	return anode;
}

void CMeshDesc::clear() {
	FreeMesh();
	m_debugName.clear();
}

void CMeshDesc::FreeMesh() {
	if (m_mesh.numfacets <= 0) {
		return;
	}

	m_mesh = {};

	FreeMeshBuild();
	m_mesh = {};
}

bool CMeshDesc ::InitMeshBuild(std::u8string debug_name, int numTriangles) {
	if (numTriangles <= 0) {
		return false;
	}

	m_debugName = std::move(debug_name);

	// perfomance warning
	if (numTriangles >= 65536) {
		Log("Error: %s has too many triangles (%i). Mesh cannot be build\n",
		    (char const *) m_debugName.c_str(),
		    numTriangles);
		return false; // failed to build (too many triangles)
	} else if (numTriangles >= 32768) {
		Warning(
			"%s has too many triangles (%i)\n",
			(char const *) m_debugName.c_str(),
			numTriangles
		);
	} else if (numTriangles >= 16384) {
		Developer(
			developer_level::warning,
			"%s has too many triangles (%i)\n",
			(char const *) m_debugName.c_str(),
			numTriangles
		);
	}

	// Too many triangles invoke AABB tree construction
	has_tree = numTriangles >= 256;

	ClearBounds(m_mesh.mins, m_mesh.maxs);

	areanodes->fill({});
	numareanodes = 0;
	m_iNumTris = numTriangles;
	m_iTotalPlanes = 0;

	// create pools for construct mesh
	facets = std::make_unique<mfacet_t[]>(numTriangles);
	planehash = std::make_unique<mesh_plane_count[]>(mesh_plane_hashes);
	planepool = std::make_unique<hashplane_t[]>(max_mesh_planes);

	// create default invalid plane for index 0
	mplane_t badplane{};
	AddPlaneToPool(&badplane);

	return true;
}

bool CMeshDesc ::PlaneEqual(mplane_t const * p0, mplane_t const * p1) {
	float t;

	if (-PLANE_DIST_EPSILON < (t = p0->dist - p1->dist)
	    && t < PLANE_DIST_EPSILON
	    && -PLANE_NORMAL_EPSILON < (t = p0->normal[0] - p1->normal[0])
	    && t < PLANE_NORMAL_EPSILON
	    && -PLANE_NORMAL_EPSILON < (t = p0->normal[1] - p1->normal[1])
	    && t < PLANE_NORMAL_EPSILON
	    && -PLANE_NORMAL_EPSILON < (t = p0->normal[2] - p1->normal[2])
	    && t < PLANE_NORMAL_EPSILON) {
		return true;
	}

	return false;
}

mesh_plane_count CMeshDesc ::AddPlaneToPool(mplane_t const * pl) {
	int hash;

	// trying to find equal plane
	hash = (int) fabs(pl->dist);
	hash &= (mesh_plane_hashes - 1);

	// search the border bins as well
	for (int i = -1; i <= 1; i++) {
		int h = (hash + i) & (mesh_plane_hashes - 1);
		for (mesh_plane_count p = planehash[h]; p;
		     p = planepool[p].planePoolIndex) {
			if (PlaneEqual(&planepool[p].pl, pl)) {
				return p; // already exist
			}
		}
	}

	if (m_mesh.numPlanes >= max_mesh_planes) {
		Error(
			"AddPlaneToPool: plane limit exceeded: planes %u, max planes %u\n",
			m_mesh.numPlanes,
			max_mesh_planes
		);
		return 0; // index of our bad plane
	}

	// Create a new one
	mesh_plane_count planePoolIndex = m_mesh.numPlanes++;
	hashplane_t* p = &planepool[planePoolIndex];
	p->planePoolIndex = planehash[hash];
	planehash[hash] = planePoolIndex;

	// record the new plane
	p->pl = *pl;

	return m_mesh.numPlanes - 1;
}

/*
=====================
PlaneFromPoints

Returns false if the triangle is degenrate.
The normal will point out of the clock for clockwise ordered points
=====================
*/
bool CMeshDesc ::PlaneFromPoints(
	mvert_t const triangle[3], mplane_t* plane
) {
	float3_array const v1 = vector_subtract(
		triangle[1].point, triangle[0].point
	);
	float3_array const v2 = vector_subtract(
		triangle[2].point, triangle[0].point
	);
	plane->normal = cross_product(v2, v1);

	if (vector_length(plane->normal) == 0.0f) {
		plane->normal = {};
		return false;
	}

	normalize_vector(plane->normal);
	plane->dist = dot_product(triangle[0].point, plane->normal);

	return true;
}

/*
=================
ComparePlanes
=================
*/
bool CMeshDesc ::ComparePlanes(
	mplane_t const * plane, float3_array const & normal, float dist
) {
	if (fabs(plane->normal[0] - normal[0]) < PLANE_NORMAL_EPSILON
	    && fabs(plane->normal[1] - normal[1]) < PLANE_NORMAL_EPSILON
	    && fabs(plane->normal[2] - normal[2]) < PLANE_NORMAL_EPSILON
	    && fabs(plane->dist - dist) < PLANE_DIST_EPSILON) {
		return true;
	}
	return false;
}

/*
==================
SnapVectorToGrid
==================
*/
void CMeshDesc::SnapVectorToGrid(float3_array& normal) {
	for (std::size_t i = 0; i < 3; ++i) {
		if (fabs(normal[i] - 1.0f) < PLANE_NORMAL_EPSILON) {
			normal = {};
			normal[i] = 1.0f;
			break;
		}

		if (fabs(normal[i] - -1.0f) < PLANE_NORMAL_EPSILON) {
			normal = {};
			normal[i] = -1.0f;
			break;
		}
	}
}

/*
==============
SnapPlaneToGrid
==============
*/
void CMeshDesc ::SnapPlaneToGrid(mplane_t* plane) {
	SnapVectorToGrid(plane->normal);

	if (fabs(plane->dist - std::round(plane->dist)) < PLANE_DIST_EPSILON) {
		plane->dist = std::round(plane->dist);
	}
}

/*
=================
CategorizePlane

A slightly more complex version of SignbitsForPlane and
plane_type_for_normal, which also tries to fix possible floating point
glitches (like -0.00000 cases)
=================
*/
void CMeshDesc ::CategorizePlane(mplane_t* plane) {
	plane->signbits = 0;
	plane->type = planetype::plane_anyx; // Non-axial

	for (int i = 0; i < 3; i++) {
		if (plane->normal[i] < 0.0f) {
			plane->signbits |= (1 << i);

			if (plane->normal[i] == -1.0f) {
				plane->signbits = (1 << i);
				plane->normal = {};
				plane->normal[i] = -1.0f;
				break;
			}
		} else if (plane->normal[i] == 1.0f) {
			plane->type = planetype(i);
			plane->signbits = 0;
			plane->normal = {};
			plane->normal[i] = 1.0f;
			break;
		}
	}
}

void CMeshDesc::AngleQuaternion(float3_array const & angles, vec4_t& quat) {
	float sr, sp, sy, cr, cp, cy;
	float angle;

	// FIXME: rescale the inputs to 1/2 angle
	angle = angles[2] * 0.5;
	sy = sin(angle);
	cy = cos(angle);
	angle = angles[1] * 0.5;
	sp = sin(angle);
	cp = cos(angle);
	angle = angles[0] * 0.5;
	sr = sin(angle);
	cr = cos(angle);

	quat[0] = sr * cp * cy - cr * sp * sy; // X
	quat[1] = cr * sp * cy + sr * cp * sy; // Y
	quat[2] = cr * cp * sy - sr * sp * cy; // Z
	quat[3] = cr * cp * cy + sr * sp * sy; // W
}

void CMeshDesc::AngleMatrix(
	float3_array const & angles,
	float3_array const & origin,
	float3_array const & scale,
	matrix3x4& matrix
) {
	float sr, sp, sy, cr, cp, cy;
	float angle;

	angle = angles[1] * (std::numbers::pi_v<float> * 2.0f / 360.0f);
	sy = sin(angle);
	cy = cos(angle);
	angle = angles[0] * (std::numbers::pi_v<float> * 2.0f / 360.0f);
	sp = sin(angle);
	cp = cos(angle);
	angle = angles[2] * (std::numbers::pi_v<float> * 2.0f / 360.0f);
	sr = sin(angle);
	cr = cos(angle);

	// matrix = (YAW * PITCH) * ROLL
	matrix[0][0] = cp * cy * scale[0];
	matrix[1][0] = cp * sy * scale[0];
	matrix[2][0] = -sp * scale[0];
	matrix[0][1] = sr * sp * cy + cr * -sy * scale[1];
	matrix[1][1] = sr * sp * sy + cr * cy * scale[1];
	matrix[2][1] = sr * cp * scale[1];
	matrix[0][2] = (cr * sp * cy + -sr * -sy) * scale[2];
	matrix[1][2] = (cr * sp * sy + -sr * cy) * scale[2];
	matrix[2][2] = cr * cp * scale[2];
	matrix[0][3] = origin[0];
	matrix[1][3] = origin[1];
	matrix[2][3] = origin[2];
}

void CMeshDesc ::QuaternionMatrix(
	vec4_t const & quat, float3_array const & origin, matrix3x4& matrix
) {
	matrix[0][0] = 1.0 - 2.0 * quat[1] * quat[1] - 2.0 * quat[2] * quat[2];
	matrix[1][0] = 2.0 * quat[0] * quat[1] + 2.0 * quat[3] * quat[2];
	matrix[2][0] = 2.0 * quat[0] * quat[2] - 2.0 * quat[3] * quat[1];

	matrix[0][1] = 2.0 * quat[0] * quat[1] - 2.0 * quat[3] * quat[2];
	matrix[1][1] = 1.0 - 2.0 * quat[0] * quat[0] - 2.0 * quat[2] * quat[2];
	matrix[2][1] = 2.0 * quat[1] * quat[2] + 2.0 * quat[3] * quat[0];

	matrix[0][2] = 2.0 * quat[0] * quat[2] + 2.0 * quat[3] * quat[1];
	matrix[1][2] = 2.0 * quat[1] * quat[2] - 2.0 * quat[3] * quat[0];
	matrix[2][2] = 1.0 - 2.0 * quat[0] * quat[0] - 2.0 * quat[1] * quat[1];

	matrix[0][3] = origin[0];
	matrix[1][3] = origin[1];
	matrix[2][3] = origin[2];
}

void CMeshDesc::ConcatTransforms(
	matrix3x4 const & in1, matrix3x4 const & in2, matrix3x4& out
) {
	out[0][0] = in1[0][0] * in2[0][0] + in1[0][1] * in2[1][0]
		+ in1[0][2] * in2[2][0];
	out[0][1] = in1[0][0] * in2[0][1] + in1[0][1] * in2[1][1]
		+ in1[0][2] * in2[2][1];
	out[0][2] = in1[0][0] * in2[0][2] + in1[0][1] * in2[1][2]
		+ in1[0][2] * in2[2][2];
	out[0][3] = in1[0][0] * in2[0][3] + in1[0][1] * in2[1][3]
		+ in1[0][2] * in2[2][3] + in1[0][3];
	out[1][0] = in1[1][0] * in2[0][0] + in1[1][1] * in2[1][0]
		+ in1[1][2] * in2[2][0];
	out[1][1] = in1[1][0] * in2[0][1] + in1[1][1] * in2[1][1]
		+ in1[1][2] * in2[2][1];
	out[1][2] = in1[1][0] * in2[0][2] + in1[1][1] * in2[1][2]
		+ in1[1][2] * in2[2][2];
	out[1][3] = in1[1][0] * in2[0][3] + in1[1][1] * in2[1][3]
		+ in1[1][2] * in2[2][3] + in1[1][3];
	out[2][0] = in1[2][0] * in2[0][0] + in1[2][1] * in2[1][0]
		+ in1[2][2] * in2[2][0];
	out[2][1] = in1[2][0] * in2[0][1] + in1[2][1] * in2[1][1]
		+ in1[2][2] * in2[2][1];
	out[2][2] = in1[2][0] * in2[0][2] + in1[2][1] * in2[1][2]
		+ in1[2][2] * in2[2][2];
	out[2][3] = in1[2][0] * in2[0][3] + in1[2][1] * in2[1][3]
		+ in1[2][2] * in2[2][3] + in1[2][3];
}

void CMeshDesc::VectorTransform(
	float3_array const & in1, matrix3x4 const & in2, float3_array& out
) {
	out[0] = dot_product(in1, std::array{ in2[0][0], in2[0][1], in2[0][2] })
		+ in2[0][3];
	out[1] = dot_product(in1, std::array{ in2[1][0], in2[1][1], in2[1][2] })
		+ in2[1][3];
	out[2] = dot_product(in1, std::array{ in2[2][0], in2[2][1], in2[2][2] })
		+ in2[2][3];
}

void CMeshDesc ::StudioCalcBoneQuaterion(
	mstudiobone_t* pbone, mstudioanim_t* panim, vec4_t& q
) {
	mstudioanimvalue_t* panimvalue;
	float3_array angle;

	for (std::size_t i = 0; i < 3; ++i) {
		if (panim->offset[i + 3] == 0) {
			angle[i] = pbone->value[i + 3]; // default;
		} else {
			panimvalue = (mstudioanimvalue_t*) ((byte*) panim
			                                    + panim->offset[i + 3]);
			angle[i] = panimvalue[1].value;
			angle[i] = pbone->value[i + 3] + angle[i] * pbone->scale[i + 3];
		}
	}

	AngleQuaternion(angle, q);
}

void CMeshDesc ::StudioCalcBonePosition(
	mstudiobone_t* pbone, mstudioanim_t* panim, float3_array& pos
) {
	mstudioanimvalue_t* panimvalue;

	for (int j = 0; j < 3; j++) {
		pos[j] = pbone->value[j]; // default;

		if (panim->offset[j] != 0) {
			panimvalue = (mstudioanimvalue_t*) ((byte*) panim
			                                    + panim->offset[j]);
			pos[j] += panimvalue[1].value * pbone->scale[j];
		}
	}
}

bool CMeshDesc ::StudioConstructMesh(model_t* pModel) {
	studiohdr_t* phdr = (studiohdr_t*) pModel->extradata.get();

	if (!phdr || phdr->numbones < 1) {
		Developer(
			developer_level::error,
			"StudioConstructMesh: bad model header\n"
		);
		return false;
	}

	time_counter timeCounter;

	bool const simplifyModel{
		pModel->trace_mode == trace_method::shadow_slow
	}; // trying to reduce polycount and the speedup compilation

	// compute default pose for building mesh from
	mstudioseqdesc_t* pseqdesc = (mstudioseqdesc_t*) ((byte*) phdr
	                                                  + phdr->seqindex);

	// Sanity check
	if (pseqdesc->seqgroup != 0) {
		Developer(
			developer_level::error,
			"StudioConstructMesh: bad sequence group (must be 0)\n"
		);
		return false;
	}
	mstudioseqgroup_t* pseqgroup
		= (mstudioseqgroup_t*) ((byte*) phdr + phdr->seqgroupindex)
		+ pseqdesc->seqgroup;

	// The code here should probably match
	// https://github.com/ValveSoftware/halflife/blob/master/cl_dll/StudioModelRenderer.cpp
	// but I'm not sure it does entirely
	mstudioanim_t* panim = (mstudioanim_t*) ((byte*) phdr + pseqgroup->data
	                                         + pseqdesc->animindex);

	mstudiobone_t* pbone = (mstudiobone_t*) ((byte*) phdr + phdr->boneindex
	);
	std::array<float3_array, MAXSTUDIOBONES> pos;
	std::array<vec4_t, MAXSTUDIOBONES> q;

	for (std::size_t i = 0; i < phdr->numbones; i++, pbone++, panim++) {
		StudioCalcBoneQuaterion(pbone, panim, q[i]);
		StudioCalcBonePosition(pbone, panim, pos[i]);
	}

	pbone = (mstudiobone_t*) ((byte*) phdr + phdr->boneindex);
	matrix3x4 transform;
	matrix3x4 boneMatrix;
	std::array<matrix3x4, MAXSTUDIOBONES> boneTransform;
	AngleMatrix(pModel->angles, pModel->origin, pModel->scale, transform);
	// Compute bones for default anim
	for (std::int32_t i = 0; i < phdr->numbones; ++i) {
		// Initialize bonematrix
		QuaternionMatrix(q[i], pos[i], boneMatrix);

		if (pbone[i].parent == -1) {
			ConcatTransforms(transform, boneMatrix, boneTransform[i]);
		} else {
			ConcatTransforms(
				boneTransform[pbone[i].parent], boneMatrix, boneTransform[i]
			);
		}
	}

	std::int32_t totalVertSize = 0;
	std::int32_t maxVertSize = 0;
	// through all bodies to determine max vertices count
	for (std::int32_t i = 0; i < phdr->numbodyparts; ++i) {
		mstudiobodyparts_t* pbodypart
			= (mstudiobodyparts_t*) ((byte*) phdr + phdr->bodypartindex)
			+ i;

		int index = pModel->body / pbodypart->base;
		index = index % pbodypart->nummodels;

		mstudiomodel_t* psubmodel
			= (mstudiomodel_t*) ((byte*) phdr + pbodypart->modelindex)
			+ index;
		totalVertSize += psubmodel->numverts;
		maxVertSize = std::max(maxVertSize, psubmodel->numverts);
	}

	std::unique_ptr<float3_array[]> verts{
		std::make_unique_for_overwrite<float3_array[]>(totalVertSize * 8)
	}; // Temporary vertices
	std::unique_ptr<float[]> coords{
		std::make_unique_for_overwrite<float[]>(totalVertSize * 16)
	}; // Temporary vertices
	mstudiotexture_t** textures = (mstudiotexture_t**) malloc(
		sizeof(mstudiotexture_t*) * totalVertSize * 8
	); // lame way...
	unsigned int* indices = (unsigned int*) malloc(
		sizeof(int) * totalVertSize * 24
	);
	std::uint32_t numVerts = 0, numElems = 0, numTris = 0;
	mvert_t triangle[3];

	std::unique_ptr<float3_array[]> m_verts
		= std::make_unique_for_overwrite<float3_array[]>(maxVertSize);
	std::vector<float3_array> gr;
	for (int k = 0; k < phdr->numbodyparts; k++) {
		mstudiobodyparts_t* pbodypart
			= (mstudiobodyparts_t*) ((byte*) phdr + phdr->bodypartindex)
			+ k;

		int index = pModel->body / pbodypart->base;
		index = index % pbodypart->nummodels;
		int m_skinnum = std::min(std::max(0, pModel->skin), MAXSTUDIOSKINS);

		mstudiomodel_t* psubmodel
			= (mstudiomodel_t*) ((byte*) phdr + pbodypart->modelindex)
			+ index;
		float3_array* pstudioverts = (float3_array*) ((byte*) phdr
		                                              + psubmodel->vertindex
		);
		byte* pvertbone = ((byte*) phdr + psubmodel->vertinfoindex);

		// setup all the vertices
		for (std::int32_t i = 0; i < psubmodel->numverts; ++i) {
			VectorTransform(
				pstudioverts[i], boneTransform[pvertbone[i]], m_verts[i]
			);
		}

		mstudiotexture_t* ptexture
			= (mstudiotexture_t*) ((byte*) phdr + phdr->textureindex);
		short* pskinref = (short*) ((byte*) phdr + phdr->skinindex);
		if (m_skinnum != 0 && m_skinnum < phdr->numskinfamilies) {
			pskinref += (m_skinnum * phdr->numskinref);
		}

		for (std::int32_t j = 0; j < psubmodel->nummesh; ++j) {
			mstudiomesh_t* pmesh = (mstudiomesh_t*) ((byte*) phdr
			                                         + psubmodel->meshindex)
				+ j;
			std::int16_t* ptricmds = (std::int16_t*) ((byte*) phdr
			                                          + pmesh->triindex);
			int flags = ptexture[pskinref[pmesh->skinref]].flags;
			float s = 1.0f
				/ (float) ptexture[pskinref[pmesh->skinref]].width;
			float t = 1.0f
				/ (float) ptexture[pskinref[pmesh->skinref]].height;

			std::int16_t i;
			while ((i = *(ptricmds++))) {
				int vertexState = 0;
				bool tri_strip;

				if (i < 0) {
					tri_strip = false;
					i = -i;
				} else {
					tri_strip = true;
				}

				numTris += (i - 2);

				for (; i > 0; i--, ptricmds += 4) {
					// build in indices
					if (vertexState++ < 3) {
						indices[numElems++] = numVerts;
					} else if (tri_strip) {
						// flip triangles between clockwise and counter
						// clockwise
						if (vertexState & 1) {
							// draw triangle [n-2 n-1 n]
							indices[numElems++] = numVerts - 2;
							indices[numElems++] = numVerts - 1;
							indices[numElems++] = numVerts;
						} else {
							// draw triangle [n-1 n-2 n]
							indices[numElems++] = numVerts - 1;
							indices[numElems++] = numVerts - 2;
							indices[numElems++] = numVerts;
						}
					} else {
						// draw triangle fan [0 n-1 n]
						indices[numElems++] = numVerts - (vertexState - 1);
						indices[numElems++] = numVerts - 1;
						indices[numElems++] = numVerts;
					}

					verts[numVerts] = m_verts[ptricmds[0]];
					textures[numVerts]
						= &ptexture[pskinref[pmesh->skinref]];
					if (flags & STUDIO_NF_CHROME) {
						// probably always equal 64 (see studiomdl.c for
						// details)
						coords[numVerts * 2 + 0] = s;
						coords[numVerts * 2 + 1] = t;
					} else if (flags & STUDIO_NF_UV_COORDS) {
						coords[numVerts * 2 + 0] = half_to_float(ptricmds[2]
						);
						coords[numVerts * 2 + 1] = half_to_float(ptricmds[3]
						);
					} else {
						coords[numVerts * 2 + 0] = ptricmds[2] * s;
						coords[numVerts * 2 + 1] = ptricmds[3] * t;
					}
					numVerts++;
				}
			}
		}
	}

	if (numTris != (numElems / 3)) {
		Developer(
			developer_level::error,
			"StudioConstructMesh: mismatch triangle count (%i should be %i)\n",
			(numElems / 3),
			numTris
		);
	}

	// member trace mode
	m_mesh.trace_mode = pModel->trace_mode;

	InitMeshBuild(
		pModel->absolutePathToMainModelFile.filename().generic_u8string(),
		numTris
	);

	if (simplifyModel) {
		// Begin model simplification

		// List of vertices
		std::vector<float3_array> vert{ verts.get(),
			                            verts.get() + numVerts };
		// List of triangles
		std::vector<triset> tris;
		// To which neighbor each vertex collapses
		std::vector<int> collapse_map;
		// Permutation list
		std::vector<std::size_t> permutation;

		// build the list of indices
		for (std::uint32_t i = 0; i < numElems; i += 3) {
			triset td;

			// fill the triangle
			td.v[0] = indices[i + 0];
			td.v[1] = indices[i + 1];
			td.v[2] = indices[i + 2];
			tris.emplace_back(td);
		}

		// do mesh simplification
		ProgressiveMesh(vert, tris, collapse_map, permutation);
		PermuteVertices(permutation, vert, tris);

		int verts_reduced;
		int tris_reduced = 0;

		// don't simplfy low-poly models too much
		if (numVerts <= 400) {
			verts_reduced = numVerts; // don't simplfy low-poly meshes!
		} else if (numVerts <= 600) {
			verts_reduced = int(numVerts * SIMPLIFICATION_FACTOR_LOW);
		} else if (numVerts <= 1500) {
			verts_reduced = int(numVerts * SIMPLIFICATION_FACTOR_MED);
		} else {
			verts_reduced = int(numVerts * SIMPLIFICATION_FACTOR_HIGH);
		}

		for (std::int32_t i = 0; i < tris.size(); i++) {
			int p0 = MapVertex(tris[i].v[0], verts_reduced, collapse_map);
			int p1 = MapVertex(tris[i].v[1], verts_reduced, collapse_map);
			int p2 = MapVertex(tris[i].v[2], verts_reduced, collapse_map);

			if (p0 == p1 || p1 == p2 || p2 == p0) {
				continue; // degenerate
			}

			// fill the triangle
			triangle[0].point = vert[p0];
			triangle[1].point = vert[p1];
			triangle[2].point = vert[p2];

			triangle[0].st[0] = coords[p0 * 2 + 0];
			triangle[0].st[1] = coords[p0 * 2 + 1];
			triangle[1].st[0] = coords[p1 * 2 + 0];
			triangle[1].st[1] = coords[p1 * 2 + 1];
			triangle[2].st[0] = coords[p2 * 2 + 0];
			triangle[2].st[1] = coords[p2 * 2 + 1];

			// add it to mesh
			AddMeshTriangle(triangle);
			tris_reduced++;
		}

		if (numVerts != verts_reduced) {
			Developer(
				developer_level::message,
				"Model %s simplified ( verts %i -> %i, tris %i -> %i )\n",
				pModel->absolutePathToMainModelFile.c_str(),
				numVerts,
				verts_reduced,
				numTris,
				tris_reduced
			);
		}
	} else {
		for (std::uint32_t i = 0; i < numElems; i += 3) {
			// fill the triangle
			triangle[0].point = verts[indices[i + 0]];
			triangle[1].point = verts[indices[i + 1]];
			triangle[2].point = verts[indices[i + 2]];

			triangle[0].st[0] = coords[indices[i + 0] * 2 + 0];
			triangle[0].st[1] = coords[indices[i + 0] * 2 + 1];
			triangle[1].st[0] = coords[indices[i + 1] * 2 + 0];
			triangle[1].st[1] = coords[indices[i + 1] * 2 + 1];
			triangle[2].st[0] = coords[indices[i + 2] * 2 + 0];
			triangle[2].st[1] = coords[indices[i + 2] * 2 + 1];

			// add it to mesh
			AddMeshTriangle(triangle, textures[indices[i]]);
		}
	}

	free(indices);
	free(textures);

	if (!FinishMeshBuild()) {
		Developer(
			developer_level::error,
			"StudioConstructMesh: failed to build mesh from %s\n",
			pModel->absolutePathToMainModelFile.c_str()
		);
		return false;
	}

	// g-cont. i'm leave this for debug
	Verbose(
		"%s: build time %g secs, size %zuB\n",
		(char const *) m_debugName.c_str(),
		timeCounter.get_total(),
		mesh_size
	);

	// done
	return true;
}

bool CMeshDesc ::AddMeshTriangle(
	mvert_t const triangle[3], mstudiotexture_t* texture
) {
	int i;

	if (m_iNumTris <= 0) {
		return false; // were not in a build mode!
	}

	if (m_mesh.numfacets >= m_iNumTris) {
		Developer(
			developer_level::error,
			"AddMeshTriangle: %s overflow (%i >= %i)\n",
			(char const *) m_debugName.c_str(),
			m_mesh.numfacets,
			m_iNumTris
		);
		return false;
	}

	// add triangle to bounds
	for (i = 0; i < 3; i++) {
		AddPointToBounds(triangle[i].point, m_mesh.mins, m_mesh.maxs);
	}

	mfacet_t* facet = &facets[m_mesh.numfacets];
	mplane_t mainplane;

	// calculate plane for this triangle
	PlaneFromPoints(triangle, &mainplane);

	if (ComparePlanes(&mainplane, float3_array{ 0, 0, 0 }, 0.0f)) {
		return false; // bad plane
	}

	std::array<mplane_t, max_mes_facets> planes;
	float3_array normal;
	int numplanes;
	float dist;

	facet->numplanes = numplanes = 0;
	facet->texture = texture;

	// add front plane
	SnapPlaneToGrid(&mainplane);

	planes[numplanes].normal = mainplane.normal;
	planes[numplanes].dist = mainplane.dist;
	numplanes++;

	// calculate mins & maxs
	ClearBounds(facet->mins, facet->maxs);

	for (i = 0; i < 3; i++) {
		AddPointToBounds(triangle[i].point, facet->mins, facet->maxs);
		facet->triangle[i] = triangle[i];
	}

	facet->edge1 = vector_subtract(
		facet->triangle[1].point, facet->triangle[0].point
	);
	facet->edge2 = vector_subtract(
		facet->triangle[2].point, facet->triangle[0].point
	);

	// add the axial planes
	for (int axis = 0; axis < 3; axis++) {
		for (int dir = -1; dir <= 1; dir += 2) {
			for (i = 0; i < numplanes; i++) {
				if (planes[i].normal[axis] == dir) {
					break;
				}
			}

			if (i == numplanes) {
				normal = {};
				normal[axis] = dir;
				if (dir == 1) {
					dist = facet->maxs[axis];
				} else {
					dist = -facet->mins[axis];
				}

				planes[numplanes].normal = normal;
				planes[numplanes].dist = dist;
				numplanes++;
			}
		}
	}

	// add the edge bevels
	for (i = 0; i < 3; i++) {
		int j = (i + 1) % 3;
		float3_array vec = vector_subtract(
			triangle[i].point, triangle[j].point
		);
		if (vector_length(vec) < 0.5f) {
			continue;
		}

		normalize_vector(vec);
		SnapVectorToGrid(vec);

		for (j = 0; j < 3; j++) {
			if (vec[j] == 1.0f || vec[j] == -1.0f) {
				break; // axial
			}
		}

		if (j != 3) {
			continue; // only test non-axial edges
		}

		// try the six possible slanted axials from this edge
		for (int axis = 0; axis < 3; axis++) {
			for (int dir = -1; dir <= 1; dir += 2) {
				// construct a plane
				float3_array vec2 = { 0.0f, 0.0f, 0.0f };
				vec2[axis] = dir;
				normal = cross_product(vec, vec2);

				if (vector_length(normal) < 0.5f) {
					continue;
				}

				normalize_vector(normal);
				dist = dot_product(triangle[i].point, normal);

				for (j = 0; j < numplanes; j++) {
					// if this plane has already been used, skip it
					if (ComparePlanes(&planes[j], normal, dist)) {
						break;
					}
				}

				if (j != numplanes) {
					continue;
				}

				// if all other points are behind this plane, it is a proper
				// edge bevel
				for (j = 0; j < 3; j++) {
					if (j != i) {
						float d = dot_product(triangle[j].point, normal)
							- dist;
						// point in front: this plane isn't part of the
						// outer hull
						if (d > 0.1f) {
							break;
						}
					}
				}

				if (j != 3) {
					continue;
				}

				// add this plane
				planes[numplanes].normal = normal;
				planes[numplanes].dist = dist;
				numplanes++;
			}
		}
	}

	facet->indices = (unsigned*) malloc(sizeof(unsigned) * numplanes);
	facet->numplanes = numplanes;

	for (i = 0; i < facet->numplanes; i++) {
		SnapPlaneToGrid(&planes[i]);
		CategorizePlane(&planes[i]);

		// add plane to global pool
		facet->indices[i] = AddPlaneToPool(&planes[i]);
	}

	if constexpr (AABB_OFFSET) {
		for (i = 0; i < 3; i++) {
			// spread the mins / maxs by a pixel
			facet->mins[i] -= 1.0f;
			facet->maxs[i] += 1.0f;
		}
	}
	// added
	m_mesh.numfacets++;
	m_iTotalPlanes += numplanes;

	return true;
}

void CMeshDesc ::RelinkFacet(mfacet_t* facet) {
	// find the first node that the facet box crosses
	areanode_t* node = &(*areanodes)[0];

	while (1) {
		if (node->axis == -1) {
			break;
		}
		if (facet->mins[node->axis] > node->dist) {
			node = node->children[0];
		} else if (facet->maxs[node->axis] < node->dist) {
			node = node->children[1];
		} else {
			break; // crosses the node
		}
	}

	// link it in
	InsertLinkBefore(&facet->area, &node->facets);
}

bool CMeshDesc ::FinishMeshBuild(void) {
	if (m_mesh.numfacets <= 0) {
		FreeMesh();
		Developer(
			developer_level::error,
			"FinishMeshBuild: failed to build triangle mesh (no sides)\n"
		);
		return false;
	}

	if constexpr (AABB_OFFSET) {
		for (std::size_t i = 0; i < 3; ++i) {
			// spread the mins / maxs by a pixel
			m_mesh.mins[i] -= 1.0f;
			m_mesh.maxs[i] += 1.0f;
		}
	}
	size_t memsize = (sizeof(mfacet_t) * m_mesh.numfacets)
		+ (sizeof(mplane_t) * m_mesh.numPlanes)
		+ (sizeof(unsigned) * m_iTotalPlanes);

	// create non-fragmented memory piece and move mesh
	m_mesh.buffer = std::make_unique_for_overwrite<std::byte[]>(memsize);
	byte* remainingBufferStorage = (byte*) m_mesh.buffer.get();
	byte* bufend = remainingBufferStorage + memsize;

	// setup pointers
	m_mesh.planes = (mplane_t*)
		remainingBufferStorage; // so we free mem with planes
	remainingBufferStorage += (sizeof(mplane_t) * m_mesh.numPlanes);
	m_mesh.facets = (mfacet_t*) remainingBufferStorage;
	remainingBufferStorage += (sizeof(mfacet_t) * m_mesh.numfacets);

	// setup mesh pointers
	for (std::size_t i = 0; i < m_mesh.numfacets; ++i) {
		m_mesh.facets[i].indices = (unsigned*) remainingBufferStorage;
		remainingBufferStorage += (sizeof(unsigned) * facets[i].numplanes);
	}

	if (remainingBufferStorage != bufend) {
		Developer(
			developer_level::error,
			"FinishMeshBuild: memory representation error! %p != %p\n",
			remainingBufferStorage,
			bufend
		);
	}

	// copy planes into mesh array (probably aligned block)
	for (std::size_t i = 0; i < m_mesh.numPlanes; ++i) {
		m_mesh.planes[i] = planepool[i].pl;
	}

	// copy planes into mesh array (probably aligned block)
	for (std::size_t i = 0; i < m_mesh.numfacets; ++i) {
		m_mesh.facets[i].mins = facets[i].mins;
		m_mesh.facets[i].maxs = facets[i].maxs;
		m_mesh.facets[i].edge1 = facets[i].edge1;
		m_mesh.facets[i].edge2 = facets[i].edge2;
		m_mesh.facets[i].area.next = m_mesh.facets[i].area.prev = nullptr;
		m_mesh.facets[i].numplanes = facets[i].numplanes;
		m_mesh.facets[i].texture = facets[i].texture;

		for (int j = 0; j < facets[i].numplanes; j++) {
			m_mesh.facets[i].indices[j] = facets[i].indices[j];
		}

		for (int k = 0; k < 3; k++) {
			m_mesh.facets[i].triangle[k] = facets[i].triangle[k];
		}
	}

	if (has_tree) {
		// create tree
		CreateAreaNode(0, m_mesh.mins, m_mesh.maxs);

		for (int i = 0; i < m_mesh.numfacets; i++) {
			RelinkFacet(&m_mesh.facets[i]);
		}
	}

	FreeMeshBuild();

	mesh_size = sizeof(m_mesh) + memsize;

	// Developer( developer_level::ALWAYS, "FinishMesh: %s %i k",
	// m_debugName, ( mesh_size / 1024 )); Developer(
	// developer_level::ALWAYS, " (planes reduced from %i to %i)",
	// m_iTotalPlanes, m_mesh.numplanes ); Developer(
	// developer_level::ALWAYS, "\n" );

	return true;
}

void CMeshDesc ::FreeMeshBuild(void) {
	// no reason to keep these arrays
	for (int i = 0; facets && i < m_mesh.numfacets; i++) {
		free(facets[i].indices);
	}

	facets.reset();
	planehash.reset();
	planepool.reset();
	m_iNumTris = 0;
	m_iTotalPlanes = 0;
}
