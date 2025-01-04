#pragma once

#include "util.h"

#include <algorithm>
#include <array>
#include <execution>
#include <memory>
#include <string>

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
        constexpr bool equals_constant(const char8_t (&cString)[StringSize]) const {
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
        constexpr bool starts_with_constant(const char8_t (&prefixCString)[PrefixSize]) const {
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

        constexpr bool ends_with_constant(std::u8string_view suffix) const {
#ifdef ENABLE_LOWERCASE_TEXTURE_NAMES
            return string_view().ends_with(suffix);
#else
            return string_view().ends_with(ascii_characters_to_lowercase_in_utf8_string(suffix));
#endif
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

        constexpr bool ends_with(std::u8string_view suffix) const {
#ifdef ENABLE_LOWERCASE_TEXTURE_NAMES
            return string_view().ends_with(ascii_characters_to_lowercase_in_utf8_string(suffix));
#else
            return string_view().ends_with(ascii_characters_to_lowercase_in_utf8_string(suffix));
#endif
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
        constexpr bool is_ordinary_texture_with_embedded_lightmap() const {
            return starts_with_constant(u8"__rad");
        }
        constexpr bool is_transparent_with_embedded_lightmap() const {
            return starts_with_constant(u8"{_rad");
        }
        constexpr bool is_water_with_embedded_lightmap() const {
            return starts_with_constant(u8"!_rad");
        }
        constexpr bool is_any_embedded_lightmap() const {
            return is_ordinary_texture_with_embedded_lightmap() ||
			is_transparent_with_embedded_lightmap() ||
			is_water_with_embedded_lightmap();
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
            return is_ordinary_bevel() || is_bevelbrush() || is_bevelhint();
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


        constexpr bool is_hidden() const {
            return ends_with_constant(u8"_hidden");
        }

        constexpr bool is_tiling_texture() const {
            return starts_with_constant(u8"-");
        }

        constexpr bool is_animated_texture() const {
            return starts_with_constant(u8"+");
        }


        constexpr bool is_water() const {
            return starts_with_constant(u8"!");
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
