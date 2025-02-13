#pragma once

#include "external_types/texinfo.h"

#include <cstddef>
#include <cstdint>
#include <limits>

// This file contains types and contants internal to the compilers
// (including compiler CLI arguments, config files, and zhlt_ entity
// key-values), not defined by file formats or game engine limitations

// These can be any size. They limit func_detail's maximum detail level and
// other such settings
using detail_level = std::uint16_t;
using coplanar_priority = std::int16_t;

// In theory, we could support any number of entities as long as enough are
// removed by the compilers (entities like light, info_texlights)
using entity_count = std::uint16_t;

// TODO: Move to CSG
using brush_count = std::uint16_t;
// TODO: Move to CSG
// Brush sides - side_t count/index
using side_count = std::uint32_t;

// TODO: Move to CSG
// TODO: Increase once we have dynamically-sized arrays for storing brushes
constexpr std::size_t MAX_MAP_BRUSHES = 32768;

// TODO: Move to CSG
// TODO: Increase once we have dynamically-sized arrays for storing brush
// sides
constexpr std::size_t MAX_MAP_SIDES = (MAX_MAP_BRUSHES * 6);

// TODO: Move to CSG
// The number of brushes an entity has, or an index starting at 0 for the
// first brush of every entity
using entity_local_brush_count = brush_count;

// TODO: Move to CSG
constexpr entity_local_brush_count max_brushes_per_entity
	= std::numeric_limits<entity_local_brush_count>::max();

// Because some faces will be discarded we can allow a larger
// texinfo limit in early stages of compilation, before we apply the
// FINAL_MAX_MAP_TEXINFO limit. The -1 is to accomodate no_texinfo(0xFFFF)
constexpr texinfo_count INITIAL_MAX_MAP_TEXINFO
	= std::numeric_limits<texinfo_count>::max() - 1;
