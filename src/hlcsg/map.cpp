#include "csg.h"
#include "hlcsg_settings.h"
#include "map_entity_parser.h"
#include "project_constants.h"
#include "utf8.h"

#include <string_view>
using namespace std::literals;

int g_nummapbrushes;
csg_brush g_mapbrushes[MAX_MAP_BRUSHES];

int g_numbrushsides;
side_t g_brushsides[MAX_MAP_SIDES];

csg_brush& copy_last_brush_with_sides(csg_brush* lastBrush) {
	csg_brush& newBrush = lastBrush[1];
	hlassume(g_nummapbrushes <= MAX_MAP_BRUSHES, assume_MAX_MAP_BRUSHES);
	hlassume(g_numbrushsides <= MAX_MAP_SIDES, assume_MAX_MAP_SIDES);
	newBrush = *lastBrush;
	newBrush.firstSide += newBrush.numSides;
	std::copy_n(
		&g_brushsides[lastBrush->firstSide],
		newBrush.numSides,
		&g_brushsides[newBrush.firstSide]
	);
	++newBrush.brushnum;
	return newBrush;
}

constexpr float ScaleCorrection = (1.0f / 128.0f);

// =====================================================================================
//  CheckForInvisible
//      see if an entity will always be invisible
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

static std::array<std::u8string_view, NUM_HULLS> const zhltHullKeys{
	u8"zhlt_hull0", u8"zhlt_hull1", u8"zhlt_hull2", u8"zhlt_hull3"
};

struct add_parsed_entity_result final {
	bool entityAdded{};
	entity_local_brush_count brushesAdded{};
	side_count sidesAdded{};
};

add_parsed_entity_result add_parsed_entity(
	parsed_entity& parsedEntity,
	entity_count entityNumber,
	brush_count firstBrushNumber,
	side_count firstSideNumber
) {
	bool all_clip = true;
	csg_entity& mapent{ g_entities[entityNumber] };
	mapent.firstBrush = firstBrushNumber;
	mapent.keyValues = parsedEntity.keyValues;

	side_count sidesAdded = 0;

	bool const isWorldspawn = entityNumber == 0;
	bool const useOtherModel = has_key_value(&mapent, u8"zhlt_usemodel");
	bool const isInfoHullshape = classname_is(&mapent, u8"info_hullshape");

	// Check if the entity is always invisible
	bool const nullify = CheckForInvisible(&mapent);

	// func_detail values
	detail_level detailLevel{};
	detail_level chopDown{};
	detail_level chopUp{};
	detail_level clipNodeDetailLevel{};
	coplanar_priority coplanarPriority{};

	// See the entry in hlt.fgd for info about func_group. For simplicity's
	// sake, we treat it the same as func_detail
	bool const isDetailBrush = classname_is(&mapent, u8"func_detail")
		|| classname_is(&mapent, u8"func_group");
	if (isDetailBrush) {
		detailLevel = numeric_key_value<detail_level>(
						  mapent, u8"zhlt_detaillevel"
		)
						  .value_or(0);
		chopDown = numeric_key_value<detail_level>(
					   mapent, u8"zhlt_chopdown"
		)
					   .value_or(0);
		chopUp = numeric_key_value<detail_level>(mapent, u8"zhlt_chopup")
					 .value_or(0);
		clipNodeDetailLevel = numeric_key_value<detail_level>(
								  mapent, u8"zhlt_clipnodedetaillevel"
		)
								  .value_or(0);
		coplanarPriority = numeric_key_value<coplanar_priority>(
							   mapent, u8"zhlt_coplanarpriority"
		)
							   .value_or(0);
	}

	// Add brushes
	for (parsed_brush const & parsedBrush : parsedEntity.brushes) {
		// Get next brush slot
		csg_brush& b = g_mapbrushes[firstBrushNumber + mapent.numbrushes];
		b = {};

		//  Set the first side of the brush to current global side count
		b.firstSide = firstSideNumber + sidesAdded;

		b.originalentitynum = parsedEntity.entityNumber;
		b.originalbrushnum = parsedBrush.entityLocalBrushNumber;

		b.entitynum = entityNumber;
		b.brushnum = mapent.numbrushes;

		++mapent.numbrushes;

		b.noclip = bool_key_value(mapent, u8"zhlt_noclip");

		// func_detail values
		b.detailLevel = detailLevel;
		b.chopDown = chopDown;
		b.chopUp = chopUp;
		b.clipNodeDetailLevel = clipNodeDetailLevel;
		b.coplanarPriority = coplanarPriority;

		for (std::size_t hullNumber = 0; hullNumber < NUM_HULLS;
			 ++hullNumber) {
			// Key value for the hull shape
			std::u8string_view const value = value_for_key(
				&mapent, zhltHullKeys[hullNumber]
			);

			if (!value.empty()) {
				// If we have a value associated with the key from the
				// entity properties, copy the value to brush's hull
				// shape for this hull
				b.hullshapes[hullNumber] = value;
			}
		}

		// Loop through brush sides
		for (parsed_side const & parsedSide : parsedBrush.sides) {
			// Current side of the brush
			side_t& side{ g_brushsides[b.firstSide + b.numSides] };
			side = {};
			++b.numSides;

			wad_texture_name textureName{ parsedSide.textureName };

			// Check for tool textures on the brush
			if (textureName.is_noclip()) {
				textureName = wad_texture_name{ "null" };
				b.noclip = true;
			} else if (textureName.is_any_clip()) {
				// Arbitrary nonexistent hull
				// TODO: IS THIS "Arbitrary nonexistent hull" needed?
				// Probably not! Try deleting this |= assignment and see
				// if it changes anything for maps that use clip
				// textures
				b.cliphull |= (1 << NUM_HULLS);
				std::optional<std::uint8_t> hullNumber
					= textureName.get_clip_hull_number();
				if (hullNumber) {
					b.cliphull |= (1 << hullNumber.value());
				} else {
					constexpr cliphull_bitmask anyHullBut0Mask
						= ((1 << NUM_HULLS) - 1) - 1;

					b.cliphull |= anyHullBut0Mask; // CLIP all hulls
					if (textureName.is_any_clip_bevel()) {
						side.bevel = true;
						if (textureName.is_clip_bevel_brush()) {
							b.bevel = true;
						}
					}
				}
				textureName = wad_texture_name{ "skip" };
			} else if (textureName.is_any_bevel()) {
				textureName = wad_texture_name{ "null" };
				side.bevel = true;
				if (textureName.is_bevelbrush()) {
					b.bevel = true;
				}
			}
			side.td.name = textureName;
			side.td.vects.UAxis = parsedSide.uAxis;
			side.td.vects.VAxis = parsedSide.vAxis;
			side.td.vects.shift = parsedSide.shift;
			side.td.vects.scale = parsedSide.textureScale;
			side.planepts = parsedSide.planePoints;
		}

		b.contents = CheckBrushContents(&b);

		for (std::size_t j = 0; j < b.numSides; ++j) {
			side_t& side = g_brushsides[b.firstSide + j];
			wad_texture_name const textureName{ side.td.name };
			if (textureName.is_any_content_type()
				|| (nullify && !textureName.is_any_bevel()
					&& !textureName.is_any_hint()
					&& !textureName.is_origin() && !textureName.is_skip()
					&& !textureName.is_splitface()
					&& !textureName.is_bounding_box()
					&& !textureName.is_ordinary_sky())
				|| (side.td.name.is_aaatrigger() && g_nullifytrigger)) {
				side.td.name = wad_texture_name{ u8"null" };
			}
		}

		for (std::size_t j = 0; j < b.numSides; ++j) {
			// Change to SKIP now that we have set brush content
			side_t& side = g_brushsides[b.firstSide + j];
			if (side.td.name.is_splitface()) {
				side.td.name = wad_texture_name{ u8"skip" };
			}
		}

		// Origin brushes are removed, but they set the rotation origin
		// for the rest of the brushes in the entity

		if (b.contents == contents_t::ORIGIN) {
			if (has_key_value(&mapent, u8"origin")) {
				Error(
					"Entity %i, Brush %i: Only one ORIGIN brush allowed, either the entity has multiple or the \"origin\" key-value is set (remove it or the ORIGIN brush).",
					b.originalentitynum,
					b.originalbrushnum
				);
			}

			// Get sizes. This is an ugly method that wastes work
			contents_t const contents = b.contents;
			b.contents = contents_t::SOLID;
			create_brush(b, mapent);
			b.contents = contents;
			for (brushhull_t& brushHull : b.hulls) {
				brushHull.faces.clear();
			}

			if (!isWorldspawn) { // Code elsewhere enforces no ORIGIN in
								 // world message

				double3_array const origin = midpoint_between(
					b.hulls[0].bounds.mins, b.hulls[0].bounds.maxs
				);

				char string[4096];
				safe_snprintf(
					string,
					4096,
					"%i %i %i",
					(int) origin[0],
					(int) origin[1],
					(int) origin[2]
				);
				set_key_value(
					&mapent, u8"origin", (char8_t const *) string
				);
			}
		}

		// zhlt_usemodel doesn't need brushes (other than the ORIGIN)
		if (useOtherModel) {
			// TODO: Is this fill really necessary?
			std::fill_n(&g_brushsides[b.firstSide], b.numSides, side_t{});
			// TODO: Is this = {} really necessary?
			b = {};
			--mapent.numbrushes;
			continue;
		}

		if (isInfoHullshape) {
			continue;
		}
		if (b.contents == contents_t::BOUNDINGBOX) {
			if (has_key_value(&mapent, u8"zhlt_minsmaxs")) {
				Error(
					"Entity %i, Brush %i: Only one BOUNDINGBOX brush allowed.",
					b.originalentitynum,
					b.originalbrushnum
				);
			}
			std::u8string origin{ value_for_key(&mapent, u8"origin") };
			if (!origin.empty()) {
				DeleteKey(&mapent, u8"origin");
			}

			// Get sizes. This is an ugly method that wastes work
			contents_t const contents = b.contents;
			b.contents = contents_t::SOLID;
			create_brush(b, mapent);
			b.contents = contents;
			for (brushhull_t& brushHull : b.hulls) {
				brushHull.faces.clear();
			}

			if (!isWorldspawn) { // Code elsewhere enforces no ORIGIN in
								 // world message
				double3_array const mins{ b.hulls[0].bounds.mins };
				double3_array const maxs{ b.hulls[0].bounds.maxs };

				char string[4096];
				safe_snprintf(
					string,
					4096,
					"%.0f %.0f %.0f %.0f %.0f %.0f",
					mins[0],
					mins[1],
					mins[2],
					maxs[0],
					maxs[1],
					maxs[2]
				);
				set_key_value(
					&mapent, u8"zhlt_minsmaxs", (char8_t const *) string
				);
			}

			if (!origin.empty()) {
				set_key_value(&mapent, u8"origin", origin);
			}
		}
		if (g_skyclip && b.contents == contents_t::SKY && !b.noclip) {
			csg_brush& newBrush = copy_last_brush_with_sides(&b);
			++mapent.numbrushes;
			sidesAdded += newBrush.numSides;

			newBrush.contents = contents_t::SOLID;
			newBrush.cliphull = ~0;
			for (std::size_t j = 0; j < newBrush.numSides; ++j) {
				side_t& side = g_brushsides[newBrush.firstSide + j];
				side.td.name = wad_texture_name{ u8"null" };
			}
		}
		if (b.cliphull != 0 && b.contents == contents_t::TOEMPTY) {
			// Check for mix of CLIP and normal texture
			bool mixed = false;
			for (std::size_t j = 0; j < b.numSides; ++j) {
				side_t& side = g_brushsides[b.firstSide + j];
				if (side.td.name.is_ordinary_null(
					)) { // this is not supposed to be a HINT brush, so
						 // remove all invisible faces from hull 0.
					side.td.name = wad_texture_name{ u8"skip" };
				}
				if (!side.td.name.is_skip()) {
					mixed = true;
				}
			}
			if (mixed) {
				csg_brush& newBrush = copy_last_brush_with_sides(&b);
				++mapent.numbrushes;
				sidesAdded += newBrush.numSides;
				newBrush.cliphull = 0;
			}
			b.contents = contents_t::SOLID;
			for (std::size_t j = 0; j < b.numSides; ++j) {
				side_t& side = g_brushsides[b.firstSide + j];
				side.td.name = wad_texture_name{ u8"null" };
			}
		}

		sidesAdded += b.numSides;
	}

	for (brush_count i = 0; i < mapent.numbrushes; ++i) {
		csg_brush const & brush = g_mapbrushes[mapent.firstBrush + i];
		if (brush.cliphull == 0 && brush.contents != contents_t::ORIGIN
			&& brush.contents != contents_t::BOUNDINGBOX) {
			all_clip = false;
		}
	}

	if (useOtherModel && !has_key_value(&mapent, u8"origin")) {
		Warning(
			"Entity %i: 'zhlt_usemodel' requires the entity to have an origin brush.",
			parsedEntity.entityNumber
		);
	}

	if (!isInfoHullshape) // info_hullshape is not affected by '-scale'
	{
		bool ent_move_b = false, ent_scale_b = false, ent_gscale_b = false;
		double3_array ent_move{};
		double3_array ent_scale_origin;
		double ent_scale = 1;
		double ent_gscale = 1;

		if (g_scalesize > 0) {
			ent_gscale_b = true;
			ent_gscale = g_scalesize;
		}
		std::array<double, 4> v{};
		if (has_key_value(&mapent, u8"zhlt_transform")) {
			switch (sscanf(
				(char const *) value_for_key(&mapent, u8"zhlt_transform")
					.data(),
				"%lf %lf %lf %lf",
				&v[0],
				&v[1],
				&v[2],
				&v[3]
			)) {
				case 1:
					ent_scale_b = true;
					ent_scale = v[0];
					break;
				case 3:
					ent_move_b = true;
					std::copy_n(v.begin(), 3, ent_move.begin());
					break;
				case 4:
					ent_scale_b = true;
					ent_scale = v[0];
					ent_move_b = true;
					std::copy_n(v.begin() + 1, 3, ent_move.begin());
					break;
				default:
					Warning(
						"bad value '%s' for key 'zhlt_transform'",
						(char const *)
							value_for_key(&mapent, u8"zhlt_transform")
								.data()
					);
			}
			DeleteKey(&mapent, u8"zhlt_transform");
		}
		ent_scale_origin = get_double3_for_key(mapent, u8"origin");

		if (ent_move_b || ent_scale_b || ent_gscale_b) {
			// Scaling hack

			int ibrush, iside, ipoint;
			csg_brush* brush;
			side_t* side;
			for (ibrush = 0, brush = g_mapbrushes + mapent.firstBrush;
				 ibrush < mapent.numbrushes;
				 ++ibrush, ++brush) {
				for (iside = 0, side = g_brushsides + brush->firstSide;
					 iside < brush->numSides;
					 ++iside, ++side) {
					for (ipoint = 0; ipoint < 3; ++ipoint) {
						double3_array& point = side->planepts[ipoint];
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
					// note that  tex->vecs = td.vects.Axis /
					// td.vects.scale
					//            tex->vecs[3] = vects.shift +
					//            Dot(origin, tex->vecs)
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
							coord[0] = dot_product(
										   ent_scale_origin,
										   side->td.vects.UAxis
									   )
									/ side->td.vects.scale[0]
								+ side->td.vects.shift[0];
							side->td.vects.scale[0] *= ent_scale;
							if (fabs(side->td.vects.scale[0])
								> NORMAL_EPSILON) {
								side->td.vects.shift[0] = coord[0]
									- dot_product(
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
							coord[1] = dot_product(
										   ent_scale_origin,
										   side->td.vects.VAxis
									   )
									/ side->td.vects.scale[1]
								+ side->td.vects.shift[1];
							side->td.vects.scale[1] *= ent_scale;
							if (fabs(side->td.vects.scale[1])
								> NORMAL_EPSILON) {
								side->td.vects.shift[1] = coord[1]
									- dot_product(
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
								-= dot_product(
									   ent_move, side->td.vects.UAxis
								   )
								/ side->td.vects.scale[0];
						} else {
							zeroscale = true;
						}
						if (fabs(side->td.vects.scale[1])
							> NORMAL_EPSILON) {
							side->td.vects.shift[1]
								-= dot_product(
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
				if (has_key_value(&mapent, u8"origin")) {
					std::array<std::int32_t, 3> origin;
					char8_t string[4096];
					double3_array const originScaled = vector_scale(
						get_double3_for_key(mapent, u8"origin"), ent_gscale
					);

					for (std::size_t i = 0; i < 3; ++i) {
						origin[i] = (std::int32_t
						) std::round(originScaled[i]);
					}

					safe_snprintf(
						(char*) string,
						4096,
						"%d %d %d",
						origin[0],
						origin[1],
						origin[2]
					);
					set_key_value(&mapent, u8"origin", string);
				}
			}
			{
				std::array<double3_array, 2> b;
				if (sscanf(
						(char const *)
							value_for_key(&mapent, u8"zhlt_minsmaxs")
								.data(),
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
					char string[4096];
					safe_snprintf(
						string,
						4096,
						"%.0f %.0f %.0f %.0f %.0f %.0f",
						b[0][0],
						b[0][1],
						b[0][2],
						b[1][0],
						b[1][1],
						b[1][2]
					);
					set_key_value(
						&mapent, u8"zhlt_minsmaxs", (char8_t const *) string
					);
				}
			}
		}
	}

	mapent.origin = get_float3_for_key(mapent, u8"origin");

	// TODO: Move out of this function - this doesn't need to be checked for
	// very entity
	if (isWorldspawn) {
		// Store the compiler name - it will be interesting to see how many
		// maps people will make using this compiler
		set_key_value(&mapent, u8"compiler", projectName);
	}

	if (classname_is(&mapent, u8"info_compile_parameters")) {
		GetParamsFromEnt(&mapent);
	}

	if (isDetailBrush) {
		// This is pretty gross, because the brushes are expected to be
		// in linear order for each entity

		brush_count newbrushes = mapent.numbrushes;
		brush_count worldbrushes = g_entities[0].numbrushes;

		auto temp = std::make_unique_for_overwrite<csg_brush[]>(newbrushes);
		// TODO: Move instead of copying
		std::copy_n(
			g_mapbrushes + mapent.firstBrush, newbrushes, temp.get()
		);

		for (brush_count i = 0; i < newbrushes; ++i) {
			temp[i].entitynum = 0;
			temp[i].brushnum += worldbrushes;
		}

		// Make space to move the brushes (overlapped copy)
		// TODO: Move instead of copying
		std::copy_backward(
			g_mapbrushes + worldbrushes,
			g_mapbrushes + firstBrushNumber,
			g_mapbrushes + firstBrushNumber + newbrushes
		);

		// copy the new brushes down
		// TODO: Move instead of copying
		std::copy_n(temp.get(), newbrushes, g_mapbrushes + worldbrushes);

		// Fix up indexes
		g_entities[0].numbrushes += newbrushes;
		for (std::size_t i = 1; i < entityNumber; i++) {
			g_entities[i].firstBrush += newbrushes;
		}
		// TODO: Is this `mapent = {};` actually necessary?
		mapent = {};

		return { .entityAdded = false,
				 .brushesAdded = newbrushes,
				 .sidesAdded = sidesAdded };
	}

	if (isInfoHullshape) {
		bool const disabled = bool_key_value(mapent, u8"disabled");
		std::u8string_view const id = value_for_key(
			&mapent, u8"targetname"
		);
		int defaulthulls = IntForKey(&mapent, u8"defaulthulls");
		CreateHullShape(entityNumber, disabled, id, defaulthulls);
		//	DeleteCurrentEntity(&mapent);
		return { .entityAdded = false, .brushesAdded = 0, .sidesAdded = 0 };
	}
	if (fabs(mapent.origin[0]) > ENGINE_ENTITY_RANGE + ON_EPSILON
		|| fabs(mapent.origin[1]) > ENGINE_ENTITY_RANGE + ON_EPSILON
		|| fabs(mapent.origin[2]) > ENGINE_ENTITY_RANGE + ON_EPSILON) {
		std::u8string_view const classname{ get_classname(mapent) };
		if (!classname.starts_with(u8"light")) {
			Warning(
				"Entity %i (classname \"%s\"): origin outside +/-%.0f: (%.0f,%.0f,%.0f)",
				parsedEntity.entityNumber,
				(char const *) classname.data(),
				(double) ENGINE_ENTITY_RANGE,
				mapent.origin[0],
				mapent.origin[1],
				mapent.origin[2]
			);
		}
	}

	return { .entityAdded = true,
			 .brushesAdded = mapent.numbrushes,
			 .sidesAdded = sidesAdded };
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

		// If it's a light_spot or light_env, don't include it as an
		// engine entity!
		if (classname == u8"light" || classname == u8"light_spot"
			|| classname == u8"light_environment") {
			// light_spots and light_enviroments don't have targetnames
			// or styles
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
//      parse in script entities
// =====================================================================================

void LoadMapFile(
	hlcsg_settings const & settings, char const * const filename
) {
	g_numentities = 0;

	std::optional<std::u8string> maybeMapFileContents = read_utf8_file(
		filename,
		true,
		settings.legacyMapEncoding,
		settings.forceLegacyMapEncoding
	);
	if (!maybeMapFileContents) {
		Error("Failed to load %s", filename);
	}

	map_entity_parser parser{ maybeMapFileContents.value() };
	parse_entity_outcome parseOutcome;
	parsed_entity parsedEntity;
	while ((parseOutcome = parser.parse_entity(parsedEntity))
		   == parse_entity_outcome::entity_parsed) {
		add_parsed_entity_result const result = add_parsed_entity(
			parsedEntity, g_numentities, g_nummapbrushes, g_numbrushsides
		);

		g_numentities += result.entityAdded;
		g_nummapbrushes += result.brushesAdded;
		g_numbrushsides += result.sidesAdded;
	}

	if (parseOutcome == parse_entity_outcome::bad_input) {
		Error(
			"MAP parsing error near %s",
			(char const *) parser.remaining_input().substr(0, 80).data()
		);
	} else if (parseOutcome
			   == parse_entity_outcome::not_valve220_map_format) {
		Error(
			"It looks like you are trying to compile a map made with a very old editor or one which outputs incompatible .map files. The compiler supports only the Valve220 .map format. Try opening the .map in Hammer Editor 3.5, J.A.C.K., or another modern editor, and exporting the .map again.\n"
		);
	}

	entity_count num_engine_entities = CountEngineEntities();

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
