#pragma once

#include "util.h"

#include <algorithm>
#include <array>
#include <execution>
#include <memory>
#include <string>


constexpr std::size_t embedded_lightmap_texture_name_original_texinfo_index_starts_at = 5; // __radXXXXX
constexpr std::size_t embedded_lightmap_texture_name_original_texinfo_index_length = 5;

constexpr std::size_t wad_texture_name_max_length_without_last_null = 15;
constexpr std::size_t wad_texture_name_max_length_with_last_null = 1 + wad_texture_name_max_length_without_last_null;
// WAD texture names are case-insensitive.
// Usuaully stored in lower-case, though often presented in upper-case.
// They have 15 useable bytes, plus one that's always NULL.
// Unused bytes are also NULL.
class wad_texture_name final {
    private:
        std::array<char8_t, wad_texture_name_max_length_with_last_null> units = {};

        template<std::size_t StringSize>
        constexpr bool equals_constant(const char8_t (&cString)[StringSize]) const noexcept {
            static_assert(StringSize <= wad_texture_name_max_length_with_last_null);

#ifndef ENABLE_LOWERCASE_TEXTURE_NAMES
            return std::equal(
                units.begin(),
                units.begin() + StringSize,
                ascii_characters_to_uppercase_in_utf8_string(cString).begin()
            );
#endif

            return std::equal(
// Not supported by Apple Clang in XCode 16.2, but should be supported by Clang 17
#ifdef __cpp_lib_execution
                std::execution::unseq,
#endif
                units.begin(), units.begin() + StringSize, cString);
        }

        template<std::size_t PrefixSize>
        constexpr bool starts_with_constant(const char8_t (&prefixCString)[PrefixSize]) const noexcept {
            static_assert(PrefixSize <= wad_texture_name_max_length_with_last_null);

#ifndef ENABLE_LOWERCASE_TEXTURE_NAMES
            return std::equal(
                units.begin(),
                units.begin() + PrefixSize - 1,
                ascii_characters_to_uppercase_in_utf8_string(prefixCString).begin()
            );
#endif
            return std::equal(
// Not supported by Apple Clang in XCode 16.2, but should be supported by Clang 17
#ifdef __cpp_lib_execution
                std::execution::unseq,
#endif
                units.begin(), units.begin() + PrefixSize - 1, prefixCString);
        }

        constexpr bool ends_with_constant(std::u8string_view suffix) const noexcept {
#ifdef ENABLE_LOWERCASE_TEXTURE_NAMES
            return string_view().ends_with(suffix);
#else
            return string_view().ends_with(ascii_characters_to_lowercase_in_utf8_string(suffix));
#endif
        }


        constexpr std::span<const char8_t, embedded_lightmap_texture_name_original_texinfo_index_length> 
            original_texinfo_index_for_embedded_lightmap_as_string() const noexcept {
            return std::span<const char8_t, embedded_lightmap_texture_name_original_texinfo_index_length>(
                units.begin() + embedded_lightmap_texture_name_original_texinfo_index_starts_at,
                units.end() + embedded_lightmap_texture_name_original_texinfo_index_length
            );
        }

    public:
        constexpr wad_texture_name() noexcept = default;
        constexpr wad_texture_name(const wad_texture_name&) noexcept = default;
        constexpr ~wad_texture_name() noexcept = default;
        constexpr wad_texture_name& operator=(const wad_texture_name&) noexcept = default;
        constexpr bool operator==(const wad_texture_name&) const noexcept = default;
        constexpr bool operator!=(const wad_texture_name&) const noexcept = default;
        constexpr std::strong_ordering operator<=>(const wad_texture_name&) const noexcept = default;

        constexpr wad_texture_name(std::u8string_view str) {
            if(str.size() >= wad_texture_name_max_length_with_last_null) {
                throw std::runtime_error("wad_texture_name constructor given an overly long string");
            }
			if(str.contains(u8'\0')) {
                throw std::runtime_error("Texture name contains a NULL character");
			}

            std::ranges::copy(str, units.begin());
#ifdef ENABLE_LOWERCASE_TEXTURE_NAMES
            make_ascii_characters_lowercase_in_utf8_string(units);
#else
            make_ascii_characters_uppercase_in_utf8_string(units);
#endif
        }

        static constexpr std::optional<wad_texture_name> make_if_legal_name(std::u8string_view str) noexcept {
            if(str.size() >= wad_texture_name_max_length_with_last_null || str.contains(u8'\0')) {
                return std::nullopt;
            }

            wad_texture_name result;
            std::ranges::copy(str, result.units.begin());
            #ifdef ENABLE_LOWERCASE_TEXTURE_NAMES
                make_ascii_characters_lowercase_in_utf8_string(result.units);
            #else
                make_ascii_characters_uppercase_in_utf8_string(result.units);
            #endif
            return result;
        }

        // TODO: Delete
        constexpr wad_texture_name(std::string_view str): wad_texture_name(std::u8string_view{(const char8_t*) str.data(), str.length()}) {}

        constexpr std::u8string_view string_view() const noexcept {
            return std::u8string_view{units.begin(), length()};
        }

        constexpr operator std::u8string_view() const noexcept {
            return string_view();
        }

        // TODO: Delete when we have logging with UTF-8 types
        constexpr const char* c_str() const noexcept {
            return (const char*) string_view().data();
        }

        constexpr bool ends_with(std::u8string_view suffix) const noexcept {
            std::u8string_view nameStringView = string_view();
            if(suffix.length() > nameStringView.length()) {
                return false;
            }

            return std::ranges::equal(
                nameStringView.substr(nameStringView.length() - suffix.length()),
#ifdef ENABLE_LOWERCASE_TEXTURE_NAMES
                ascii_characters_to_lowercase_in_utf8_string_as_view(suffix)
#else
                ascii_characters_to_uppercase_in_utf8_string_as_view(suffix)
#endif
            );
        }

        constexpr bool starts_with(std::u8string_view string) const noexcept {
            if(string.length() >= wad_texture_name_max_length_with_last_null) [[unlikely]] {
                return false;
            }

            return std::ranges::equal(
#ifdef ENABLE_LOWERCASE_TEXTURE_NAMES
            ascii_characters_to_lowercase_in_utf8_string_as_view(string),
#else
            ascii_characters_to_uppercase_in_utf8_string_as_view(string),
#endif
                std::u8string_view(units.data(), string.length())
            );
        }

        constexpr bool operator==(std::u8string_view string) const noexcept {
            return starts_with(string) && units[string.length()] == u8'\0';
        }


        // This should be called after bytes read from a file
        // are reinterpreted as a wad_texture_name object.
        // It returns true if the terminatation is correct or can be
        // corrected (in case there is a NULL byte followed by junk)
		// and if the name is valid UTF-8.
        constexpr bool validate_and_normalize() noexcept {
            bool reachedNulls = false;
            for (char8_t& c : units) {
                const bool isNull = c == u8'\0';
                reachedNulls = isNull || reachedNulls;
                if (reachedNulls && !isNull) [[unlikely]] {
                    // In case the software that created the WAD file
                    // left junk after the first null terminator
                    c = u8'\0';
                }
            }

			// In case there are upper-case characters
#ifdef ENABLE_LOWERCASE_TEXTURE_NAMES
            make_ascii_characters_lowercase_in_utf8_string(units);
#else
            make_ascii_characters_uppercase_in_utf8_string(units);
#endif
            return validate_utf8(units) && reachedNulls;
        }

        constexpr std::size_t length() const noexcept {
            return 16 - std::count(
// Not supported by Apple Clang in XCode 16.2, but should be supported by Clang 17
#ifdef __cpp_lib_execution
                std::execution::unseq,
#endif
                units.begin(), units.end(), u8'\0');
        }
    
        constexpr bool is_contentempty() const noexcept {
            return equals_constant(u8"contentempty");
        }
        constexpr bool is_contentsky() const noexcept {
            return equals_constant(u8"contentsky");
        }
        constexpr bool is_contentsolid() const noexcept {
            return equals_constant(u8"contentsolid");
        }
        constexpr bool is_contentwater() const noexcept {
            return equals_constant(u8"contentwater");
        }
        constexpr bool is_any_content_type() const noexcept {
            return starts_with_constant(u8"cont") &&
                (is_contentempty() || is_contentsky() || is_contentsolid() || is_contentwater())
            ;
        }

        constexpr bool is_aaatrigger() const noexcept {
            return equals_constant(u8"aaatrigger");
        }
        constexpr bool is_origin() const noexcept {
            return equals_constant(u8"origin");
        }
        constexpr bool is_bounding_box() const noexcept {
            // Similar to ORIGIN, but used to define a bounding box for an entity.
            // Useful if you have a lot of complex, off-grid geometry and need a
            // bounding box which is snapped to the grid
            return equals_constant(u8"boundingbox");
        }
        // The env_sky texture is used with the Spirit of Half-Life env_sky entity for 3D skyboxes
        constexpr bool is_env_sky() const noexcept {
            return equals_constant(u8"env_sky");
        }
        constexpr bool is_ordinary_sky() const noexcept {
            return equals_constant(u8"sky");
        }

        // See Version 33 in http://zhlt.info/version-history.html
        // for more info.
        constexpr bool is_ordinary_texture_with_embedded_lightmap() const noexcept {
            return starts_with_constant(u8"__rad");
        }
        constexpr bool is_transparent_with_embedded_lightmap() const noexcept {
            return starts_with_constant(u8"{_rad");
        }
        constexpr bool is_water_with_embedded_lightmap() const noexcept {
            return starts_with_constant(u8"!_rad");
        }
        constexpr bool is_any_embedded_lightmap() const noexcept {
            return is_ordinary_texture_with_embedded_lightmap() ||
			is_transparent_with_embedded_lightmap() ||
			is_water_with_embedded_lightmap();
        }
        // Returns nullopt if it's not an embedded lightmap texture
        constexpr std::optional<std::uint32_t> original_texinfo_index_for_embedded_lightmap() const noexcept {
            if(is_any_embedded_lightmap()) {
                std::uint32_t result = 0;
                for(char8_t c : original_texinfo_index_for_embedded_lightmap_as_string()) {
                    result *= 10;
                    result += c - u8'0';
                }
                return result;
            }
            return std::nullopt;
        }

        constexpr bool is_ordinary_bevel() const noexcept {
            return equals_constant(u8"bevel");
        }
        constexpr bool is_bevelbrush() const noexcept {
            return equals_constant(u8"bevelbrush");
        }
        constexpr bool is_bevel_hint() const noexcept {
            return equals_constant(u8"bevelhint");
        }
        constexpr bool is_any_bevel() const noexcept {
            return is_ordinary_bevel() || is_bevelbrush() || is_bevel_hint();
        }
        constexpr bool is_solid_hint() const noexcept {
            return equals_constant(u8"solidhint");
        }
        constexpr bool is_ordinary_hint() const noexcept {
            return equals_constant(u8"hint");
        }
        constexpr bool is_any_hint() const noexcept {
            return is_ordinary_hint() || is_bevel_hint() || is_solid_hint();
        }
        constexpr bool marks_discardable_faces() const noexcept {
            return is_bevel_hint() || is_solid_hint();
        }

        constexpr bool is_transculent() const noexcept {
            return starts_with_constant(u8"@") || equals_constant(u8"transculent");
        }

        constexpr bool is_splitface() const noexcept {
            return equals_constant(u8"splitface");
        }

        constexpr bool is_noclip() const noexcept {
            return equals_constant(u8"noclip");
        }

        constexpr bool is_null() const noexcept {
            return equals_constant(u8"null");
        }

        constexpr bool is_skip() const noexcept {
            return equals_constant(u8"skip");
        }

        constexpr bool is_transparent_or_decal() const noexcept {
            return starts_with_constant(u8"{");
        }

        constexpr bool is_hidden() const noexcept {
            return ends_with_constant(u8"_hidden");
        }

        // Random tiling textures
        constexpr bool is_tile() const noexcept {
            return starts_with_constant(u8"-") && units[1] != '\0';
        }

        // Animated textures
        constexpr bool is_animation_frame() const noexcept {
            return starts_with_constant(u8"+") && ((units[1] >= u8'0' && units[1] <= u8'9') || 

#ifdef ENABLE_LOWERCASE_TEXTURE_NAMES
            (units[1] >= u8'a' && units[1] <= u8'j')
#else
            (units[1] >= u8'A' && units[1] <= u8'J')
#endif
            );
        }
        
        constexpr std::optional<std::pair<std::uint8_t, bool>> get_animation_frame_or_tile_number() const noexcept {
            if(is_animation_frame() || is_tile()) {
#ifdef ENABLE_LOWERCASE_TEXTURE_NAMES
                const bool alternateAnimation = units[1] >= 'a';
                std::uint8_t frameNumber = units[1] - (alternateAnimation ? u8'a' : u8'0');
#else
                const bool alternateAnimation = units[1] >= 'A';
                std::uint8_t frameNumber = units[1] - (alternateAnimation ? u8'A' : u8'0');
#endif
                return std::make_pair(frameNumber, alternateAnimation);
            }
            return std::nullopt;
        }

        // Note! Only meaningful if is_animation_frame() or is_tile() is true
        constexpr void set_animation_frame_or_tile_number(std::uint8_t frameNumber, bool alternateAnimation) noexcept {
            if((is_animation_frame() || is_tile()) && frameNumber < 10) {

#ifdef ENABLE_LOWERCASE_TEXTURE_NAMES
                units[1] = frameNumber + (alternateAnimation ? u8'a' : u8'0');
#else
                units[1] = frameNumber + (alternateAnimation ? u8'A' : u8'0');
#endif
            }
        }

        constexpr bool is_water_with_current_0() const noexcept {
            return starts_with_constant(u8"!cur_0");
        }
        constexpr bool is_water_with_current_90() const noexcept {
            return starts_with_constant(u8"!cur_90");
        }
        constexpr bool is_water_with_current_180() const noexcept {
            return starts_with_constant(u8"!cur_180");
        }
        constexpr bool is_water_with_current_270() const noexcept {
            return starts_with_constant(u8"!cur_270");
        }
        constexpr bool is_water_with_current_down() const noexcept {
            return starts_with_constant(u8"!cur_dwn");
        }
        constexpr bool is_water_with_current_up() const noexcept {
            return starts_with_constant(u8"!cur_up");
        }
        constexpr bool is_water_with_current() const noexcept {
            return (
                    starts_with_constant(u8"!cur") &&
                    (
                        is_water_with_current_0() ||
                        is_water_with_current_90() ||
                        is_water_with_current_180() ||
                        is_water_with_current_270() ||
                        is_water_with_current_down() ||
                        is_water_with_current_up()
                    )
                );
        }

        constexpr bool is_lava() const noexcept {
            return starts_with_constant(u8"!lava") || starts_with_constant(u8"*lava");
        }
        constexpr bool is_slime() const noexcept {
            return starts_with_constant(u8"!slime") || starts_with_constant(u8"*slime");;
        }
        constexpr bool is_any_liquid() const noexcept {
            return (
                    starts_with_constant(u8"!") ||
                    starts_with_constant(u8"*") ||
                    starts_with_constant(u8"laser") ||
                    starts_with_constant(u8"water")
                );
        }
        constexpr bool is_water() const noexcept {
            return is_any_liquid() && !is_lava() && !is_slime();
        }

        constexpr bool has_minlight() const noexcept {
            return starts_with_constant(u8"%") && units[1] >= u8'0' && units[1] <= u8'9';
        }
        constexpr std::optional<std::uint8_t> get_minlight() const noexcept {
            if (has_minlight()) {
                std::uint8_t minlight = 0;
                for(char8_t c : std::span(&units[1], &units[4])) {
                    if(c < u8'0' || c > u8'9') {
                        break;
                    }
                    minlight *= 10;
                    minlight += c;
                }
                return minlight;
            }
            return std::nullopt;
        }



        // References:
        // https://twhl.info/wiki/page/Texture
        // https://twhl.info/wiki/page/tool_textures
        // Quake's W_CleanupName
};

constexpr bool operator==(std::u8string_view string, wad_texture_name name) noexcept {
    return name == string;
}

// TODO: Delete these when we're using UTF-8
constexpr bool operator==(std::string_view string, wad_texture_name name) noexcept {
    return name == std::u8string_view((const char8_t*) string.data(), string.length());
}
constexpr bool operator==(wad_texture_name name, std::string_view string) noexcept {
    return name == std::u8string_view((const char8_t*) string.data(), string.length());
}
