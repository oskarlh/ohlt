#pragma once

#include "wad_texture_name.h"

#include <array>
#include <string_view>

struct wadinfo_t final {
	std::array<char8_t, 4> identification; // Should be WAD3
	std::int32_t numlumps;
	std::int32_t infotableofs;
};

constexpr bool has_wad_identification(wadinfo_t const & wadHeader) {
	return std::u8string_view{ wadHeader.identification } == u8"WAD3";
}

struct wad_lumpinfo final { // Lump info in WAD
	std::int32_t filepos;   // TODO: Should this be unsigned?
	std::int32_t disksize;  // TODO: This should probably be unsigned
	std::int32_t size;      // Uncompressed
	std::uint8_t type;
	std::uint8_t compression; // Unsupported

	// Unused padding
	std::uint8_t pad1;
	std::uint8_t pad2;

	wad_texture_name name;
};
