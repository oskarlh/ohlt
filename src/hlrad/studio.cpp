#include "filelib.h"
#include "meshtrace.h"
#include "qrad.h"
#include "utf8.h"

constexpr std::size_t MAX_MODELS = 10000;

static std::vector<model_t> models;

static void LoadStudioModel(
	std::u8string_view modelname,
	float3_array const & origin,
	float3_array const & angles,
	float3_array const & scale,
	int body,
	int skin,
	trace_method trace_mode
) {
	if (models.size() >= MAX_MODELS) {
		Developer(
			developer_level::error, "LoadStudioModel: MAX_MODELS exceeded\n"
		);
		return;
	}
	model_t* m = &models.emplace_back();
	snprintf(
		m->name,
		sizeof(m->name),
		"%s%s",
		g_Wadpath,
		(char const *) modelname.data()
	);
	FlipSlashes(m->name);

	auto [readSuccessfully, fileSize, fileContents] = read_binary_file(
		m->name
	);
	if (!readSuccessfully) {
		Warning("LoadStudioModel: couldn't load %s\n", m->name);
		return;
	}
	m->extradata = fileContents.release();

	studiohdr_t* phdr = (studiohdr_t*) m->extradata;

	// well the textures place in separate file (very stupid case)
	if (phdr->numtextures == 0) {
		char texname[128], texpath[128];
		byte* moddata;
		studiohdr_t *thdr, *newhdr;
		safe_strncpy(texname, (char const *) modelname.data(), 128);
		StripExtension(texname);

		snprintf(texpath, sizeof(texpath), "%s%sT.mdl", g_Wadpath, texname);
		FlipSlashes(texpath);

		auto [readTFileSuccessfully, tFileSize, texdata] = read_binary_file(
			texpath
		);
		if (!readTFileSuccessfully) {
			Error(
				"LoadStudioModel: couldn't load T.mdl file %s\n", texpath
			);
			return;
		}
		moddata = (byte*) m->extradata;
		phdr = (studiohdr_t*) moddata;

		thdr = (studiohdr_t*) texdata.get();

		// merge textures with main model buffer
		m->extradata = malloc(
			phdr->length + thdr->length - sizeof(studiohdr_t)
		); // we don't need two headers
		memcpy(m->extradata, moddata, phdr->length);
		memcpy(
			(byte*) m->extradata + phdr->length,
			texdata.get() + sizeof(studiohdr_t),
			thdr->length - sizeof(studiohdr_t)
		);

		// merge header
		newhdr = (studiohdr_t*) m->extradata;

		newhdr->numskinfamilies = thdr->numskinfamilies;
		newhdr->numtextures = thdr->numtextures;
		newhdr->numskinref = thdr->numskinref;
		newhdr->textureindex = phdr->length;
		newhdr->skinindex = newhdr->textureindex
			+ (newhdr->numtextures * sizeof(mstudiotexture_t));
		newhdr->texturedataindex = newhdr->skinindex
			+ (newhdr->numskinfamilies * newhdr->numskinref * sizeof(short)
			);
		newhdr->length = phdr->length + thdr->length - sizeof(studiohdr_t);

		// and finally merge datapointers for textures
		for (int i = 0; i < newhdr->numtextures; i++) {
			mstudiotexture_t* ptexture
				= (mstudiotexture_t*) (((byte*) newhdr)
									   + newhdr->textureindex);
			ptexture[i].index += (phdr->length - sizeof(studiohdr_t));
			//			printf( "Texture %i [%s]\n", i, ptexture[i].name );
			// now we can replace offsets with real pointers
			//			ptexture[i].pixels = (byte *)newhdr +
			// ptexture[i].index;
		}

		free(moddata);
	}

	m->origin = origin;
	m->angles = angles;
	m->scale = scale;

	m->trace_mode = trace_mode;

	m->body = body;
	m->skin = skin;

	m->mesh.StudioConstructMesh(m);
}

// =====================================================================================
//  LoadStudioModels
// =====================================================================================
void LoadStudioModels() {
	models.clear();

	if (!g_studioshadow) {
		return;
	}

	for (int i = 0; i < g_numentities; i++) {
		std::u8string_view model;
		float3_array origin, angles;

		entity_t* e = &g_entities[i];

		if (key_value_is(e, u8"classname", u8"env_static")) {
			int spawnflags = IntForKey(e, u8"spawnflags");
			if (spawnflags & 4) {
				continue; // shadow disabled
			}

			model = value_for_key(e, u8"model");

			if (model.empty()) {
				Developer(
					developer_level::warning,
					"env_static has empty model field\n"
				);
				continue;
			}
		} else if (IntForKey(e, u8"zhlt_studioshadow")) {
			model = value_for_key(e, u8"model");

			if (model.empty()) {
				continue;
			}
		} else {
			continue;
		}

		origin = get_float3_for_key(*e, u8"origin");
		angles = get_float3_for_key(*e, u8"angles");

		angles[0] = -angles[0]; // Stupid quake bug workaround
		trace_method trace_mode
			= trace_method::shadow_normal; // default mode

		// make sure what field is present
		if (has_key_value(e, u8"zhlt_shadowmode")) {
			// TODO: Check if it's a valid number
			trace_mode = trace_method(IntForKey(e, u8"zhlt_shadowmode"));
		}

		int body = IntForKey(e, u8"body");
		int skin = IntForKey(e, u8"skin");

		float scale = float_for_key(*e, u8"scale");

		float3_array xform = get_float3_for_key(*e, u8"xform");

		if (vectors_almost_same(xform, float3_array{})) {
			xform.fill(scale);
		}

		// check xform values
		if (xform[0] < 0.01f) {
			xform[0] = 1.0f;
		}
		if (xform[1] < 0.01f) {
			xform[1] = 1.0f;
		}
		if (xform[2] < 0.01f) {
			xform[2] = 1.0f;
		}
		if (xform[0] > 16.0f) {
			xform[0] = 16.0f;
		}
		if (xform[1] > 16.0f) {
			xform[1] = 16.0f;
		}
		if (xform[2] > 16.0f) {
			xform[2] = 16.0f;
		}

		LoadStudioModel(
			model, origin, angles, xform, body, skin, trace_mode
		);
	}

	Log("%zu opaque studio models\n", models.size());
}

void FreeStudioModels() {
	for (int i = 0; i < models.size(); i++) {
		model_t& m = models[i];

		// first, delete the mesh
		m.mesh.FreeMesh();

		// unload the model
		free(m.extradata);
	}

	models.clear();
}

static void MoveBounds(
	float3_array const & start,
	float3_array const & mins,
	float3_array const & maxs,
	float3_array const & end,
	float3_array& outmins,
	float3_array& outmaxs
) {
	for (int i = 0; i < 3; i++) {
		if (end[i] > start[i]) {
			outmins[i] = start[i] + mins[i] - 1.0f;
			outmaxs[i] = end[i] + maxs[i] + 1.0f;
		} else {
			outmins[i] = end[i] + mins[i] - 1.0f;
			outmaxs[i] = start[i] + maxs[i] + 1.0f;
		}
	}
}

bool TestSegmentAgainstStudioList(
	float3_array const & p1, float3_array const & p2
) {
	if (models.empty()) {
		return false; // Easy out
	}

	float3_array trace_mins, trace_maxs;

	MoveBounds(
		p1, float3_array{}, float3_array{}, p2, trace_mins, trace_maxs
	);

	for (std::size_t i = 0; i < models.size(); ++i) {
		model_t* m = &models[i];

		mmesh_t* pMesh = m->mesh.GetMesh();
		areanode_t* pHeadNode = m->mesh.GetHeadNode();

		if (!pMesh || !m->mesh.Intersect(trace_mins, trace_maxs)) {
			continue; // bad model or not intersect with trace
		}

		TraceMesh trm; // a name like Doom3 :-)

		trm.SetTraceModExtradata(m->extradata);
		trm.SetTraceMesh(pMesh, pHeadNode);
		trm.SetupTrace(p1, float3_array{}, float3_array{}, p2);

		if (trm.DoTrace()) {
			return true; // we hit studio model
		}
	}

	return false;
}
