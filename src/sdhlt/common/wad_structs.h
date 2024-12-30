#pragma once

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
	using namespace std::literals;
	
	std::u8string_view id{wadHeader.identification};
    return id == u8"WAD2"sv || id == u8"WAD3"sv;
}

constexpr std::size_t MAXWADNAME = 16;
constexpr std::size_t wad_texture_name_max_length_without_last_null = 15;
constexpr std::size_t wad_texture_name_max_length_with_last_null = 1 + wad_texture_name_max_length_without_last_null;

class wad_texture_name final {
    private:
        std::array<char8_t, wad_texture_name_max_length_with_last_null> units = {};

        template<std::size_t StringSize>
        constexpr bool equals_constant(const char8_t (&chars)[StringSize]) const {
            static_assert(StringSize <= wad_texture_name_max_length_with_last_null);

            return std::equal(units.begin(), units.begin() + StringSize, chars);
        }

        constexpr bool starts_with_constant(std::u8string_view chars) const {
            return std::u8string_view(units.begin(), units.end()).starts_with(chars);
        }

    public:
        constexpr wad_texture_name() noexcept = default;
        constexpr wad_texture_name(const wad_texture_name&) noexcept = default;
        constexpr ~wad_texture_name() noexcept = default;
        constexpr wad_texture_name& operator=(const wad_texture_name&) noexcept = default;
        constexpr std::strong_ordering operator<=>(const wad_texture_name&) const noexcept = default;

        constexpr wad_texture_name(std::string_view str) {
            if(str.size() >= wad_texture_name_max_length_with_last_null) {
                throw std::runtime_error("wad_texture_name constructor given an overly long string");
            }

            std::ranges::copy(str, units.begin());
            make_ascii_characters_lowercase_in_utf8_string(units);
        }

        constexpr operator std::u8string_view() const noexcept {
            return std::u8string_view{units.begin(), size()};
        }

        constexpr std::size_t size() const noexcept {
            return std::ranges::count(units, u8'\0');
        }
    
        constexpr bool is_contentempty() const {
            return equals_constant(u8"contentempty");
        }
        constexpr bool is_contentsky() const {
            return equals_constant(u8"contentsky");
        }
        constexpr bool is_contentsolid() const {
            return equals_constant(u8"contentsolid");
        }
        constexpr bool is_contentwater() const {
            return equals_constant(u8"contentwater");
        }
        constexpr bool is_origin() const {
            return equals_constant(u8"origin");
        }
        constexpr bool is_sky() const {
            return equals_constant(u8"sky");
        }

        // See Version 33 in http://zhlt.info/version-history.html
        // for more info.
        constexpr bool is_ordinary_embedded_lightmap() const {
            return starts_with_constant(u8"__rad");
        }
        constexpr bool is_transparent_embedded_lightmap() const {
            return starts_with_constant(u8"{_rad");
        }
        constexpr bool is_any_embedded_lightmap() const {
            return is_ordinary_embedded_lightmap() || is_transparent_embedded_lightmap();
        }

        constexpr bool is_ordinary_bevel() const {
            return equals_constant(u8"bevel");
        }
        constexpr bool is_bevelbrush() const {
            return equals_constant(u8"bevelbrush");
        }
        constexpr bool is_bevelhint() const {
            return equals_constant(u8"bevelhint");
        }
        constexpr bool is_any_bevel() const {
            return starts_with_constant(u8"bevel");
        }


        constexpr bool is_solidhint() const {
            return equals_constant(u8"solidhint");
        }

        constexpr bool is_any_hint() const {
            return is_bevelhint() || is_solidhint();
        }


        constexpr bool is_transculent() const {
            return starts_with_constant(u8"@") || equals_constant(u8"transculent");
        }


        constexpr bool is_splitface() const {
            return equals_constant(u8"splitface");
        }

        constexpr bool is_hint() const {
            return equals_constant(u8"hint");
        }

        constexpr bool is_noclip() const {
            return equals_constant(u8"noclip");
        }

        constexpr bool is_null() const {
            return equals_constant(u8"null");
        }

        constexpr bool is_skip() const {
            return equals_constant(u8"skip");
        }

};


struct lumpinfo_t // Lump info in WAD
{
    std::int32_t filepos;
    std::int32_t disksize;
    std::int32_t size; // Uncompressed
    std::uint8_t type;
    std::uint8_t compression;
    std::uint8_t pad1, pad2;
    // Must be null terminated
    // Upper case
    std::array<char8_t, MAXWADNAME> name{};
};
