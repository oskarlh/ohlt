#include "csg.h"
#include "hlcsg_settings.h"
#include "project_constants.h"
#include "utf8.h"

#include <string_view>
using namespace std::literals;

int g_nummapbrushes;
brush_t g_mapbrushes[MAX_MAP_BRUSHES];

int g_numbrushsides;
side_t g_brushsides[MAX_MAP_SIDES];

int g_numparsedentities;
int g_numparsedbrushes;

brush_t* CopyCurrentBrush(entity_t* entity, brush_t const * brush) {
	if (entity->firstbrush + entity->numbrushes != g_nummapbrushes) {
		Error("CopyCurrentBrush: internal error.");
	}
	brush_t* newb = &g_mapbrushes[g_nummapbrushes];
	g_nummapbrushes++;
	hlassume(g_nummapbrushes <= MAX_MAP_BRUSHES, assume_MAX_MAP_BRUSHES);
	*newb = *brush;
	newb->firstside = g_numbrushsides;
	g_numbrushsides += brush->numsides;
	hlassume(g_numbrushsides <= MAX_MAP_SIDES, assume_MAX_MAP_SIDES);
	memcpy(
		&g_brushsides[newb->firstside],
		&g_brushsides[brush->firstside],
		brush->numsides * sizeof(side_t)
	);
	newb->entitynum = entity - g_entities.data();
	newb->brushnum = entity->numbrushes;
	entity->numbrushes++;
	newb->hullshapes = brush->hullshapes;
	return newb;
}

void DeleteCurrentEntity(entity_t* entity) {
	if (entity != &g_entities[g_numentities - 1]) {
		Error("DeleteCurrentEntity: internal error.");
	}
	if (entity->firstbrush + entity->numbrushes != g_nummapbrushes) {
		Error("DeleteCurrentEntity: internal error.");
	}
	for (int i = entity->numbrushes - 1; i >= 0; i--) {
		brush_t* b = &g_mapbrushes[entity->firstbrush + i];
		if (b->firstside + b->numsides != g_numbrushsides) {
			Error(
				"DeleteCurrentEntity: internal error. (Entity %i, Brush %i)",
				b->originalentitynum,
				b->originalbrushnum
			);
		}
		std::fill_n(&g_brushsides[b->firstside], b->numsides, side_t{});
		g_numbrushsides -= b->numsides;
		b->hullshapes = {};
	}
	std::fill_n(
		&g_mapbrushes[entity->firstbrush], entity->numbrushes, brush_t{}
	);

	g_nummapbrushes -= entity->numbrushes;
	*entity = entity_t{};
	g_numentities--;
}

#define ScaleCorrection (1.0 / 128.0)

// =====================================================================================
//  CheckForInvisible
//      see if a brush is part of an invisible entity (KGP)
// =====================================================================================
static bool CheckForInvisible(entity_t* mapent) {
	return std::ranges::contains(g_invisible_items, get_classname(*mapent))
		|| std::ranges::contains(
			   g_invisible_items, value_for_key(mapent, u8"targetname")
		)
		|| std::ranges::contains(
			   g_invisible_items, value_for_key(mapent, u8"zhlt_invisible")
		);
}

// =====================================================================================
//  ParseBrush
//      parse a brush from script
// =====================================================================================
static void ParseBrush(entity_t* mapent) {
	brush_t* b;			 // Current brush
	int i, j;			 // Loop counters
	side_t* side;		 // Current side of the brush
	contents_t contents; // Contents type of the brush
	bool ok;
	bool nullify = CheckForInvisible(mapent
	); // If the current entity is part of an invis entity
	hlassume(g_nummapbrushes < MAX_MAP_BRUSHES, assume_MAX_MAP_BRUSHES);

	b = &g_mapbrushes[g_nummapbrushes]; // Get next brush slot
	g_nummapbrushes++; // Increment the global brush counter, we are adding
					   // a new brush
	*b = {};
	b->firstside = g_numbrushsides; // Set the first side of the brush to
									// current global side count20
	b->originalentitynum = g_numparsedentities; // Record original entity
												// number brush belongs to
	b->originalbrushnum = g_numparsedbrushes;	// Record original brush
												// number
	b->entitynum = g_numentities
		- 1; // Set brush entity number to last created entity
	b->brushnum = g_nummapbrushes - mapent->firstbrush
		- 1;	   // Calculate the brush number within the current entity.
	b->noclip = 0; // Initialize false for now

	if (IntForKey(mapent, u8"zhlt_noclip")) // If zhlt_noclip
	{
		b->noclip = 1;
	}
	b->cliphull = 0;
	b->bevel = false;
	{ // Validate func_detail values
		b->detaillevel = IntForKey(mapent, u8"zhlt_detaillevel");
		b->chopdown = IntForKey(mapent, u8"zhlt_chopdown");
		b->chopup = IntForKey(mapent, u8"zhlt_chopup");
		b->clipnodedetaillevel = IntForKey(
			mapent, u8"zhlt_clipnodedetaillevel"
		);
		b->coplanarpriority = IntForKey(mapent, u8"zhlt_coplanarpriority");
		bool wrong = false;

		if (b->detaillevel < 0) {
			wrong = true;
			b->detaillevel = 0;
		}
		if (b->chopdown < 0) {
			wrong = true;
			b->chopdown = 0;
		}
		if (b->chopup < 0) {
			wrong = true;
			b->chopup = 0;
		}
		if (b->clipnodedetaillevel < 0) {
			wrong = true;
			b->clipnodedetaillevel = 0;
		}
		if (wrong) {
			Warning(
				"Entity %i, Brush %i: incorrect settings for detail brush.",
				b->originalentitynum,
				b->originalbrushnum
			);
		}
	}
	for (std::size_t h = 0; h < NUM_HULLS; h++) // Loop through all hulls
	{
		std::array<char8_t, u8"zhlt_hull0"sv.length()> key{
			u8"zhlt_hull"
		}; // Key name for the hull shape.
		static_assert(NUM_HULLS <= 10);
		key.back() = u8'0' + h;
		std::u8string_view const value = value_for_key(
			mapent, std::u8string_view{ key.begin(), key.end() }
		);

		if (!value.empty()) {
			// If we have a value associated with the key from the entity
			// properties copy the value to brush's hull shape for this hull
			b->hullshapes[h] = value;
		}
	}
	mapent->numbrushes++;
	ok = GetToken(true);

	while (ok) // Loop through brush sides
	{
		if (g_token == u8"}"sv) // If we have reached the end of the brush
		{
			break;
		}

		hlassume(g_numbrushsides < MAX_MAP_SIDES, assume_MAX_MAP_SIDES);
		side = &g_brushsides[g_numbrushsides]; // Get next brush side from
											   // global array
		g_numbrushsides++;					   // Global brush side counter
		b->numsides++; // Number of sides for the current brush
		side->bevel = false;
		// read the three point plane definition

		for (i = 0; i < 3;
			 i++) // Read 3 point plane definition for brush side
		{
			if (i != 0) // If not the first point get next token
			{
				GetToken(true);
			}
			if (g_token != u8"("sv) // Token must be '('
			{
				Error(
					"Parsing Entity %i, Brush %i, Side %i: Expecting '(' got '%s'",
					b->originalentitynum,
					b->originalbrushnum,
					b->numsides,
					(char const *) g_token.c_str()
				);
			}
			for (j = 0; j < 3; j++) // Get three coords for the point
			{
				GetToken(false); // Get next token on same line
				side->planepts[i][j] = atof((char const *) g_token.c_str()
				); // Convert token to float and store in planepts
			}
			GetToken(false);

			if (g_token != u8")"sv) {
				Error(
					"Parsing Entity %i, Brush %i, Side %i: Expecting ')' got '%s'",
					b->originalentitynum,
					b->originalbrushnum,
					b->numsides,
					(char const *) g_token.c_str()
				);
			}
		}

		// Read the texturedef
		GetToken(false);
		std::optional<wad_texture_name> maybeTextureName{
			wad_texture_name::make_if_legal_name(g_token)
		};
		if (!maybeTextureName) {
			Error(
				"Parsing Entity %i, Brush %i, Side %i: Bad texture name '%s'",
				b->originalentitynum,
				b->originalbrushnum,
				b->numsides,
				(char const *) g_token.c_str()
			);
		}
		wad_texture_name textureName = maybeTextureName.value();

		{ // Check for tool textures on the brush
			if (textureName.is_noclip()) {
				textureName = wad_texture_name{ "null" };
				b->noclip = true;
			} else if (textureName.is_any_clip()) {
				b->cliphull
					|= (1 << NUM_HULLS); // arbitrary nonexistent hull
				std::optional<std::uint8_t> hullNumber
					= textureName.get_clip_hull_number();
				if (hullNumber) {
					b->cliphull |= (1 << hullNumber.value());
				} else if (textureName.is_any_clip_bevel()) {
					side->bevel = true;
					if (textureName.is_clip_bevel_brush()) {
						b->bevel = true;
					}
				}
				textureName = wad_texture_name{ "skip" };
			} else if (textureName.is_any_bevel(
					   )) // Including BEVEL, BEVELBRUSH, and BEVELHINT
			{
				textureName = wad_texture_name{ "null" };
				side->bevel = true;
				if (textureName.is_bevelbrush()) {
					b->bevel = true;
				}
			}
		}
		side->td.name = textureName;

		// texture U axis
		GetToken(false);
		if (g_token != u8"["sv) {
			hlassume(false, assume_MISSING_BRACKET_IN_TEXTUREDEF);
		}

		GetToken(false);
		side->td.vects.UAxis[0] = atof((char const *) g_token.c_str());
		GetToken(false);
		side->td.vects.UAxis[1] = atof((char const *) g_token.c_str());
		GetToken(false);
		side->td.vects.UAxis[2] = atof((char const *) g_token.c_str());
		GetToken(false);
		side->td.vects.shift[0] = atof((char const *) g_token.c_str());

		GetToken(false);
		if (g_token != u8"]"sv) {
			Error("missing ']' in texturedef (U)");
		}

		// texture V axis
		GetToken(false);
		if (g_token != u8"["sv) {
			Error("missing '[' in texturedef (V)");
		}

		GetToken(false);
		side->td.vects.VAxis[0] = atof((char const *) g_token.c_str());
		GetToken(false);
		side->td.vects.VAxis[1] = atof((char const *) g_token.c_str());
		GetToken(false);
		side->td.vects.VAxis[2] = atof((char const *) g_token.c_str());
		GetToken(false);
		side->td.vects.shift[1] = atof((char const *) g_token.c_str());

		GetToken(false);
		if (g_token != u8"]"sv) {
			Error("missing ']' in texturedef (V)");
		}

		// Texture rotation is implicit in U/V axes.
		GetToken(false);
		side->td.vects.rotate = 0;

		// texure scale
		GetToken(false);
		side->td.vects.scale[0] = atof((char const *) g_token.c_str());
		GetToken(false);
		side->td.vects.scale[1] = atof((char const *) g_token.c_str());

		ok = GetToken(true
		); // Done with line, this reads the first item from the next line
	};
	if (b->cliphull != 0) // has CLIP* texture
	{
		unsigned int mask_anyhull = 0;
		for (int h = 1; h < NUM_HULLS; h++) {
			mask_anyhull |= (1 << h);
		}
		if ((b->cliphull & mask_anyhull)
			== 0) // no CLIPHULL1 or CLIPHULL2 or CLIPHULL3 texture
		{
			b->cliphull |= mask_anyhull; // CLIP all hulls
		}
	}

	b->contents = contents = CheckBrushContents(b);
	for (j = 0; j < b->numsides; j++) {
		side = &g_brushsides[b->firstside + j];
		wad_texture_name const textureName{ side->td.name };
		if (textureName.is_any_content_type()
			|| (nullify && !textureName.is_any_bevel()
				&& !textureName.is_any_hint() && !textureName.is_origin()
				&& !textureName.is_skip() && !textureName.is_splitface()
				&& !textureName.is_bounding_box()
				&& !textureName.is_ordinary_sky())
			|| (side->td.name.is_aaatrigger() && g_nullifytrigger)) {
			side->td.name = wad_texture_name{ u8"null" };
		}
	}
	for (j = 0; j < b->numsides; j++) {
		// change to SKIP now that we have set brush content.
		side = &g_brushsides[b->firstside + j];
		if (side->td.name.is_splitface()) {
			side->td.name = wad_texture_name{ u8"skip" };
		}
	}

	//
	// origin brushes are removed, but they set
	// the rotation origin for the rest of the brushes
	// in the entity
	//

	if (contents == CONTENTS_ORIGIN) {
		if (has_key_value(mapent, u8"origin")) {
			Error(
				"Entity %i, Brush %i: Only one ORIGIN brush allowed.",
				b->originalentitynum,
				b->originalbrushnum
			);
		}
		char string[MAXTOKEN];
		double3_array origin;

		b->contents = CONTENTS_SOLID;
		CreateBrush(mapent->firstbrush + b->brushnum); // to get sizes
		b->contents = contents;

		for (i = 0; i < NUM_HULLS; i++) {
			b->hulls[i].faces.clear();
		}

		if (b->entitynum != 0) // Ignore for WORLD (code elsewhere enforces
							   // no ORIGIN in world message)
		{
			VectorAdd(
				b->hulls[0].bounds.mins, b->hulls[0].bounds.maxs, origin
			);
			VectorScale(origin, 0.5, origin);

			safe_snprintf(
				string,
				MAXTOKEN,
				"%i %i %i",
				(int) origin[0],
				(int) origin[1],
				(int) origin[2]
			);
			set_key_value(
				&g_entities[b->entitynum],
				u8"origin",
				(char8_t const *) string
			);
		}
	}
	if (has_key_value(&g_entities[b->entitynum], u8"zhlt_usemodel")) {
		memset(
			&g_brushsides[b->firstside], 0, b->numsides * sizeof(side_t)
		);
		g_numbrushsides -= b->numsides;
		*b = {};
		g_nummapbrushes--;
		mapent->numbrushes--;
		return;
	}
	if (key_value_is(
			&g_entities[b->entitynum], u8"classname", u8"info_hullshape"
		)) {
		// all brushes should be erased, but not now.
		return;
	}
	if (contents == CONTENTS_BOUNDINGBOX) {
		if (has_key_value(mapent, u8"zhlt_minsmaxs")) {
			Error(
				"Entity %i, Brush %i: Only one BoundingBox brush allowed.",
				b->originalentitynum,
				b->originalbrushnum
			);
		}
		std::u8string origin{ value_for_key(mapent, u8"origin") };
		if (!origin.empty()) {
			DeleteKey(mapent, u8"origin");
		}

		b->contents = CONTENTS_SOLID;
		CreateBrush(mapent->firstbrush + b->brushnum); // To get sizes
		b->contents = contents;

		for (i = 0; i < NUM_HULLS; i++) {
			b->hulls[i].faces.clear();
		}

		if (b->entitynum != 0) // Ignore for WORLD (code elsewhere enforces
							   // no ORIGIN in world message)
		{
			double3_array const mins{ b->hulls[0].bounds.mins };
			double3_array const maxs{ b->hulls[0].bounds.maxs };

			char string[MAXTOKEN];
			safe_snprintf(
				string,
				MAXTOKEN,
				"%.0f %.0f %.0f %.0f %.0f %.0f",
				mins[0],
				mins[1],
				mins[2],
				maxs[0],
				maxs[1],
				maxs[2]
			);
			set_key_value(
				&g_entities[b->entitynum],
				u8"zhlt_minsmaxs",
				(char8_t const *) string
			);
		}

		if (!origin.empty()) {
			set_key_value(mapent, u8"origin", origin);
		}
	}
	if (g_skyclip && b->contents == CONTENTS_SKY && !b->noclip) {
		brush_t* newb = CopyCurrentBrush(mapent, b);
		newb->contents = CONTENTS_SOLID;
		newb->cliphull = ~0;
		for (j = 0; j < newb->numsides; j++) {
			side = &g_brushsides[newb->firstside + j];
			side->td.name = wad_texture_name{ u8"null" };
		}
	}
	if (b->cliphull != 0 && b->contents == CONTENTS_TOEMPTY) {
		// check for mix of CLIP and normal texture
		bool mixed = false;
		for (j = 0; j < b->numsides; j++) {
			side = &g_brushsides[b->firstside + j];
			if (side->td.name.is_ordinary_null(
				)) { // this is not supposed to be a HINT brush, so remove
					 // all invisible faces from hull 0.
				side->td.name = wad_texture_name{ u8"skip" };
			}
			if (!side->td.name.is_skip()) {
				mixed = true;
			}
		}
		if (mixed) {
			brush_t* newb = CopyCurrentBrush(mapent, b);
			newb->cliphull = 0;
		}
		b->contents = CONTENTS_SOLID;
		for (j = 0; j < b->numsides; j++) {
			side = &g_brushsides[b->firstside + j];
			side->td.name = wad_texture_name{ u8"null" };
		}
	}
}

// =====================================================================================
//  ParseMapEntity
//      parse an entity from script
// =====================================================================================
bool ParseMapEntity() {
	bool all_clip = true;
	entity_t* mapent;

	g_numparsedbrushes = 0;
	if (!GetToken(true)) {
		return false;
	}

	int this_entity = g_numentities;

	if (g_token != u8"{") {
		Error(
			"Parsing Entity %i, expected '{' got '%s'",
			g_numparsedentities,
			(char const *) g_token.c_str()
		);
	}

	hlassume(g_numentities < MAX_MAP_ENTITIES, assume_MAX_MAP_ENTITIES);
	g_numentities++;

	mapent = &g_entities[this_entity];
	mapent->firstbrush = g_nummapbrushes;
	mapent->numbrushes = 0;
	mapent->keyValues.reserve(16);

	while (1) {
		if (!GetToken(true)) {
			Error("ParseEntity: EOF without closing brace");
		}

		if (g_token == u8"}"sv) { // end of our context
			break;
		}

		if (g_token == u8"{"sv) // must be a brush
		{
			ParseBrush(mapent);
			g_numparsedbrushes++;

		} else // else assume a key-value pair
		{
			entity_key_value kv{ parse_entity_key_value() };
			if (mapent->numbrushes > 0) {
				Warning("Error: ParseEntity: Keyvalue comes after brushes."
				);
			}
			set_key_value(mapent, std::move(kv));
		}
	}
	if (classname_is(mapent, u8"worldspawn")
		&& !key_value_is(mapent, u8"mapversion", u8"220")) {
		Error(
			"It looks like you are trying to compile a map made with a very old editor or one which outputs incompatible .map files. These compilers only support the Valve220 .map format.\n"
		);
	}
	{
		int i;
		for (i = 0; i < mapent->numbrushes; i++) {
			brush_t* brush = &g_mapbrushes[mapent->firstbrush + i];
			if (brush->cliphull == 0 && brush->contents != CONTENTS_ORIGIN
				&& brush->contents != CONTENTS_BOUNDINGBOX) {
				all_clip = false;
			}
		}
	}
	if (has_key_value(mapent, u8"zhlt_usemodel")) {
		if (!has_key_value(mapent, u8"origin")) {
			Warning(
				"Entity %i: 'zhlt_usemodel' requires the entity to have an origin brush.",
				g_numparsedentities
			);
		}
		mapent->numbrushes = 0;
	}
	if (!classname_is(
			mapent, u8"info_hullshape"
		)) // info_hullshape is not affected by '-scale'
	{
		bool ent_move_b = false, ent_scale_b = false, ent_gscale_b = false;
		double3_array ent_move = { 0, 0, 0 },
					  ent_scale_origin = { 0, 0, 0 };
		double ent_scale = 1, ent_gscale = 1;

		if (g_scalesize > 0) {
			ent_gscale_b = true;
			ent_gscale = g_scalesize;
		}
		double v[4] = { 0, 0, 0, 0 };
		if (has_key_value(mapent, u8"zhlt_transform")) {
			switch (sscanf(
				(char const *) value_for_key(mapent, u8"zhlt_transform")
					.data(),
				"%lf %lf %lf %lf",
				v,
				v + 1,
				v + 2,
				v + 3
			)) {
				case 1:
					ent_scale_b = true;
					ent_scale = v[0];
					break;
				case 3:
					ent_move_b = true;
					VectorCopy(v, ent_move);
					break;
				case 4:
					ent_scale_b = true;
					ent_scale = v[0];
					ent_move_b = true;
					VectorCopy(v + 1, ent_move);
					break;
				default:
					Warning(
						"bad value '%s' for key 'zhlt_transform'",
						(char const *)
							value_for_key(mapent, u8"zhlt_transform")
								.data()
					);
			}
			DeleteKey(mapent, u8"zhlt_transform");
		}
		ent_scale_origin = get_double3_for_key(*mapent, u8"origin");

		if (ent_move_b || ent_scale_b || ent_gscale_b) {
			// Scaling hack

			int ibrush, iside, ipoint;
			brush_t* brush;
			side_t* side;
			double* point;
			for (ibrush = 0, brush = g_mapbrushes + mapent->firstbrush;
				 ibrush < mapent->numbrushes;
				 ++ibrush, ++brush) {
				for (iside = 0, side = g_brushsides + brush->firstside;
					 iside < brush->numsides;
					 ++iside, ++side) {
					for (ipoint = 0; ipoint < 3; ++ipoint) {
						point = side->planepts[ipoint];
						if (ent_scale_b) {
							VectorSubtract(point, ent_scale_origin, point);
							VectorScale(point, ent_scale, point);
							VectorAdd(point, ent_scale_origin, point);
						}
						if (ent_move_b) {
							VectorAdd(point, ent_move, point);
						}
						if (ent_gscale_b) {
							VectorScale(point, ent_gscale, point);
						}
					}
					// note that  tex->vecs = td.vects.Axis / td.vects.scale
					//            tex->vecs[3] = vects.shift + Dot(origin,
					//            tex->vecs)
					//      and   texcoordinate = Dot(worldposition,
					//      tex->vecs) + tex->vecs[3]
					bool zeroscale = false;
					if (!side->td.vects.scale[0]) {
						side->td.vects.scale[0] = 1;
					}
					if (!side->td.vects.scale[1]) {
						side->td.vects.scale[1] = 1;
					}
					if (ent_scale_b) {
						double coord[2];
						if (fabs(side->td.vects.scale[0])
							> NORMAL_EPSILON) {
							coord[0] = DotProduct(
										   ent_scale_origin,
										   side->td.vects.UAxis
									   )
									/ side->td.vects.scale[0]
								+ side->td.vects.shift[0];
							side->td.vects.scale[0] *= ent_scale;
							if (fabs(side->td.vects.scale[0])
								> NORMAL_EPSILON) {
								side->td.vects.shift[0] = coord[0]
									- DotProduct(
										  ent_scale_origin,
										  side->td.vects.UAxis
									  ) / side->td.vects.scale[0];
							} else {
								zeroscale = true;
							}
						} else {
							zeroscale = true;
						}
						if (fabs(side->td.vects.scale[1])
							> NORMAL_EPSILON) {
							coord[1] = DotProduct(
										   ent_scale_origin,
										   side->td.vects.VAxis
									   )
									/ side->td.vects.scale[1]
								+ side->td.vects.shift[1];
							side->td.vects.scale[1] *= ent_scale;
							if (fabs(side->td.vects.scale[1])
								> NORMAL_EPSILON) {
								side->td.vects.shift[1] = coord[1]
									- DotProduct(
										  ent_scale_origin,
										  side->td.vects.VAxis
									  ) / side->td.vects.scale[1];
							} else {
								zeroscale = true;
							}
						} else {
							zeroscale = true;
						}
					}
					if (ent_move_b) {
						if (fabs(side->td.vects.scale[0])
							> NORMAL_EPSILON) {
							side->td.vects.shift[0]
								-= DotProduct(
									   ent_move, side->td.vects.UAxis
								   )
								/ side->td.vects.scale[0];
						} else {
							zeroscale = true;
						}
						if (fabs(side->td.vects.scale[1])
							> NORMAL_EPSILON) {
							side->td.vects.shift[1]
								-= DotProduct(
									   ent_move, side->td.vects.VAxis
								   )
								/ side->td.vects.scale[1];
						} else {
							zeroscale = true;
						}
					}
					if (ent_gscale_b) {
						side->td.vects.scale[0] *= ent_gscale;
						side->td.vects.scale[1] *= ent_gscale;
					}
					if (zeroscale) {
						Error(
							"Entity %i, Brush %i: invalid texture scale.\n",
							brush->originalentitynum,
							brush->originalbrushnum
						);
					}
				}
			}
			if (ent_gscale_b) {
				if (has_key_value(mapent, u8"origin")) {
					double3_array v;
					int origin[3];
					char8_t string[MAXTOKEN];
					int i;
					v = get_double3_for_key(*mapent, u8"origin");
					VectorScale(v, ent_gscale, v);
					for (i = 0; i < 3; ++i) {
						origin[i] = (int) (v[i] >= 0 ? v[i] + 0.5
													 : v[i] - 0.5);
					}
					safe_snprintf(
						(char*) string,
						MAXTOKEN,
						"%d %d %d",
						origin[0],
						origin[1],
						origin[2]
					);
					set_key_value(mapent, u8"origin", string);
				}
			}
			{
				std::array<double3_array, 2> b;
				if (sscanf(
						(char const *)
							ValueForKey(mapent, u8"zhlt_minsmaxs"),
						"%lf %lf %lf %lf %lf %lf",
						&b[0][0],
						&b[0][1],
						&b[0][2],
						&b[1][0],
						&b[1][1],
						&b[1][2]
					)
					== 6) {
					for (int i = 0; i < 2; i++) {
						double3_array& point = b[i];
						if (ent_scale_b) {
							point = vector_add(
								vector_scale(
									vector_subtract(
										point, ent_scale_origin
									),
									ent_scale
								),
								ent_scale_origin
							);
						}
						if (ent_move_b) {
							point = vector_add(point, ent_move);
						}
						if (ent_gscale_b) {
							point = vector_scale(point, ent_gscale);
						}
					}
					char string[MAXTOKEN];
					safe_snprintf(
						string,
						MAXTOKEN,
						"%.0f %.0f %.0f %.0f %.0f %.0f",
						b[0][0],
						b[0][1],
						b[0][2],
						b[1][0],
						b[1][1],
						b[1][2]
					);
					set_key_value(
						mapent, u8"zhlt_minsmaxs", (char8_t const *) string
					);
				}
			}
		}
	}

	CheckFatal();
	if (this_entity == 0) {
		// Let the map tell which version of the compiler it comes from, to
		// help tracing compiler bugs.
		set_key_value(mapent, u8"compiler", projectName);
	}

	if (key_value_is(mapent, u8"classname", u8"info_compile_parameters")) {
		GetParamsFromEnt(mapent);
	}

	mapent->origin = get_float3_for_key(*mapent, u8"origin");

	if (classname_is(mapent, u8"func_group")
		|| classname_is(mapent, u8"func_detail")) {
		// this is pretty gross, because the brushes are expected to be
		// in linear order for each entity
		int newbrushes;
		int worldbrushes;

		newbrushes = mapent->numbrushes;
		worldbrushes = g_entities[0].numbrushes;

		auto temp = std::make_unique_for_overwrite<brush_t[]>(newbrushes);
		std::copy(
			g_mapbrushes + mapent->firstbrush,
			g_mapbrushes + mapent->firstbrush + newbrushes,
			temp.get()
		);

		for (int i = 0; i < newbrushes; i++) {
			temp[i].entitynum = 0;
			temp[i].brushnum += worldbrushes;
		}

		// make space to move the brushes (overlapped copy)
		std::size_t numToMove = g_nummapbrushes - worldbrushes - newbrushes;
		std::copy_backward(
			g_mapbrushes + worldbrushes,
			g_mapbrushes + worldbrushes + numToMove,
			g_mapbrushes + worldbrushes + numToMove + newbrushes
		);

		// copy the new brushes down
		std::copy(
			temp.get(), temp.get() + newbrushes, g_mapbrushes + worldbrushes
		);

		// fix up indexes
		g_numentities--;
		g_entities[0].numbrushes += newbrushes;
		for (std::size_t i = 1; i < g_numentities; i++) {
			g_entities[i].firstbrush += newbrushes;
		}
		*mapent = entity_t{};
		return true;
	}

	if (classname_is(mapent, u8"info_hullshape")) {
		bool disabled;
		int defaulthulls;
		disabled = IntForKey(mapent, u8"disabled");
		std::u8string_view const id = value_for_key(mapent, u8"targetname");
		defaulthulls = IntForKey(mapent, u8"defaulthulls");
		CreateHullShape(this_entity, disabled, id, defaulthulls);
		DeleteCurrentEntity(mapent);
		return true;
	}
	if (fabs(mapent->origin[0]) > ENGINE_ENTITY_RANGE + ON_EPSILON
		|| fabs(mapent->origin[1]) > ENGINE_ENTITY_RANGE + ON_EPSILON
		|| fabs(mapent->origin[2]) > ENGINE_ENTITY_RANGE + ON_EPSILON) {
		std::u8string_view const classname{ get_classname(*mapent) };
		if (!classname.starts_with(u8"light")) {
			Warning(
				"Entity %i (classname \"%s\"): origin outside +/-%.0f: (%.0f,%.0f,%.0f)",
				g_numparsedentities,
				(char const *) classname.data(),
				(double) ENGINE_ENTITY_RANGE,
				mapent->origin[0],
				mapent->origin[1],
				mapent->origin[2]
			);
		}
	}
	return true;
}

// =====================================================================================
//  CountEngineEntities
// =====================================================================================
unsigned int CountEngineEntities() {
	unsigned int x;
	unsigned num_engine_entities = 0;
	entity_t* mapent = g_entities.data();

	// for each entity in the map
	for (x = 0; x < g_numentities; x++, mapent++) {
		std::u8string_view classname = get_classname(*mapent);

		// If it's a light_spot or light_env, dont include it as an engine
		// entity!
		if (classname == u8"light" || classname == u8"light_spot"
			|| classname == u8"light_environment") {
			// light_spots and light_enviroments don't have targetnames or
			// styles
			if (!has_key_value(mapent, u8"targetname")
				&& !IntForKey(mapent, u8"style")) {
				continue;
			}
		}

		num_engine_entities++;
	}

	return num_engine_entities;
}

// =====================================================================================
//  LoadMapFile
//      wrapper for LoadScriptFile
//      parse in script entities
// =====================================================================================
char const * ContentsToString(contents_t const type);

void LoadMapFile(
	hlcsg_settings const & settings, char const * const filename
) {
	unsigned num_engine_entities;

	LoadScriptFile(
		filename,
		settings.legacyMapEncoding,
		settings.forceLegacyMapEncoding
	);

	g_numentities = 0;

	g_numparsedentities = 0;
	while (ParseMapEntity()) {
		g_numparsedentities++;
	}

	// AJM debug
	/*
	for (int i = 0; i < g_numentities; i++)
	{
		Log("entity: %i - %i brushes - %s\n", i, g_entities[i].numbrushes,
	ValueForKey(&g_entities[i], "classname"));
	}
	Log("total entities: %i\ntotal brushes: %i\n\n", g_numentities,
	g_nummapbrushes);

	for (i = g_entities[0].firstbrush; i < g_entities[0].firstbrush +
	g_entities[0].numbrushes; i++)
	{
		Log("worldspawn brush %i: contents %s\n", i,
	ContentsToString((contents_t)g_mapbrushes[i].contents));
	}
	*/

	num_engine_entities = CountEngineEntities();

	hlassume(
		num_engine_entities < MAX_ENGINE_ENTITIES,
		assume_MAX_ENGINE_ENTITIES
	);

	CheckFatal();

	Verbose("Load map:%s\n", filename);
	Verbose("%5i brushes\n", g_nummapbrushes);
	Verbose("%5i map entities \n", g_numentities - num_engine_entities);
	Verbose("%5i engine entities\n", num_engine_entities);

	// AJM: added in
}
