#pragma once

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

using brush_count = std::uint16_t;

// Brush sides - side_t count/index
using side_count = std::uint32_t;

// TODO: Increase once we have dynamically-sized arrays for storing brushes
constexpr std::size_t MAX_MAP_BRUSHES = 32768;

// TODO: Increase once we have dynamically-sized arrays for storing brush
// sides
constexpr std::size_t MAX_MAP_SIDES = (MAX_MAP_BRUSHES * 6);

// The number of brushes an entity has, or an index starting at 0 for the
// first brush of every entity
using entity_local_brush_count = brush_count;

constexpr entity_local_brush_count max_brushes_per_entity
	= std::numeric_limits<entity_local_brush_count>::max();
