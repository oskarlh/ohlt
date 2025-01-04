#pragma once

#include "util.h"
#include "wad_texture_name.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>

struct wadinfo_t
{
    std::array<char8_t, 4> identification; // should be WAD2/WAD3
    std::int32_t numlumps;
    std::int32_t infotableofs;
};

constexpr bool has_wad_identification(const wadinfo_t& wadHeader) {
	std::u8string_view id{wadHeader.identification};
    return id == u8"WAD2" || id == u8"WAD3";
}


constexpr std::size_t MAXWADNAME = 16;


struct lumpinfo_t // Lump info in WAD
{
    std::int32_t filepos;
    std::int32_t disksize;
    std::int32_t size; // Uncompressed
    std::uint8_t type;
    std::uint8_t compression;
    std::uint8_t pad1, pad2;

    wad_texture_name name{};
};
