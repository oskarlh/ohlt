#include "filelib.h"
#include "hlrad.h"
#include "log.h"
#include "meshtrace.h"

constexpr std::size_t MAX_MODELS = 10000;

static std::vector<model_t> models;

static void LoadStudioModel(
	std::filesystem::path
		relativePathToModel, // Example:
                             // "models/tjb_christmas/hohoho_machinegun.mdl"
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
	// TODO:!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! Look in valve folder,
	// _downloads, _addons folders and such
	m->absolutePathToMainModelFile = { g_Wadpath / relativePathToModel };

	/// TODO: fileContents may be misaligned!!!!! read_binary_file should
	/// probably return data that's aligned to std::max_align_t
	auto [readSuccessfully, fileSize, fileContents] = read_binary_file(
		m->absolutePathToMainModelFile
	);
	if (!readSuccessfully) {
		Warning(
			"LoadStudioModel: couldn't load %s\n",
			m->absolutePathToMainModelFile.c_str()
		);
		return;
	}
	m->extradata = std::move(fileContents);

	studiohdr_t* phdr = (studiohdr_t*) m->extradata.get();

	// well the textures place in separate file (very stupid case)
	if (phdr->numtextures == 0) {
		std::filesystem::path absolutePathToModelTextureFile{
			m->absolutePathToMainModelFile
		};
		absolutePathToModelTextureFile.replace_extension(
			std::filesystem::path{}
		);
		absolutePathToModelTextureFile += std::filesystem::path{
			"T.mdl", std::filesystem::path::generic_format
		};
		// absolutePathToModelTextureFile looks like this example:
		// "/games/Half-Life/mymod/models/tjb_christmas/hohoho_machinegunT.mdl"

		auto [readTFileSuccessfully, tFileSize, texdata] = read_binary_file(
			absolutePathToModelTextureFile
		);
		if (!readTFileSuccessfully) {
			Error(
				"LoadStudioModel: couldn't load T.mdl file %s\n",
				absolutePathToModelTextureFile.c_str()
			);
			return;
		}

		studiohdr_t* thdr = (studiohdr_t*) texdata.get();

		std::size_t const oldLength = phdr->length;
		std::size_t const newLength = oldLength + thdr->length
			- sizeof(studiohdr_t
		    ); // The -sizeof is because we don't need two headers

		// Merge textures with main model buffer
		std::unique_ptr<std::byte[]> newBuffer{
			std::make_unique_for_overwrite<std::byte[]>(newLength)
		};
		memcpy(newBuffer.get(), m->extradata.get(), oldLength);
		memcpy(
			&newBuffer[oldLength],
			texdata.get() + sizeof(studiohdr_t),
			thdr->length - sizeof(studiohdr_t)
		);
		m->extradata = std::move(newBuffer);

		// merge header
		phdr = (studiohdr_t*) m->extradata.get();

		phdr->numskinfamilies = thdr->numskinfamilies;
		phdr->numtextures = thdr->numtextures;
		phdr->numskinref = thdr->numskinref;
		phdr->textureindex = oldLength;
		phdr->skinindex = phdr->textureindex
			+ (phdr->numtextures * sizeof(mstudiotexture_t));
		phdr->texturedataindex = phdr->skinindex
			+ (phdr->numskinfamilies * phdr->numskinref * sizeof(short));
		phdr->length = oldLength + thdr->length - sizeof(studiohdr_t);

		// and finally merge datapointers for textures
		for (int i = 0; i < phdr->numtextures; i++) {
			mstudiotexture_t* ptexture
				= (mstudiotexture_t*) (((byte*) phdr) + phdr->textureindex);
			ptexture[i].index += (oldLength - sizeof(studiohdr_t));
			//			printf( "Texture %i [%s]\n", i, ptexture[i].name );
			// now we can replace offsets with real pointers
			//			ptexture[i].pixels = (byte *)newhdr +
			// ptexture[i].index;
		}
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
		std::filesystem::path relativePathToModel;
		float3_array origin, angles;

		entity_t* e = &g_entities[i];

		if (key_value_is(e, u8"classname", u8"env_static")) {
			int spawnflags = IntForKey(e, u8"spawnflags");
			if (spawnflags & 4) {
				continue; // shadow disabled
			}

			relativePathToModel = parse_relative_file_path(
				value_for_key(e, u8"model")
			);

			if (relativePathToModel.empty()) {
				Developer(
					developer_level::warning,
					"env_static has empty model field\n"
				);
				continue;
			}
		} else if (IntForKey(e, u8"zhlt_studioshadow")) {
			relativePathToModel = parse_relative_file_path(
				value_for_key(e, u8"model")
			);

			if (relativePathToModel.empty()) {
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

		// Check xform values
		for (float& xformElement : xform) {
			if (xformElement < 0.01f) {
				xformElement = 1.0f;
			}
			xformElement = std::min(16.0f, xformElement);
		}

		LoadStudioModel(
			relativePathToModel,
			origin,
			angles,
			xform,
			body,
			skin,
			trace_mode
		);
	}

	Log("%zu opaque studio models\n", models.size());
}

void FreeStudioModels() {
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

		trm.SetTraceModExtradata(m->extradata.get());
		trm.SetTraceMesh(pMesh, pHeadNode);
		trm.SetupTrace(p1, float3_array{}, float3_array{}, p2);

		if (trm.DoTrace()) {
			return true; // we hit studio model
		}
	}

	return false;
}
