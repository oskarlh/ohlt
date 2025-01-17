#include "legacy_character_encodings.h"

#include "utf8.h"

#include <array>
#include <cstdint>
#include <string>

using namespace std::literals;

static std::array<std::u8string_view, num_legacy_encodings> const
	encoding_code_names
	= {
		  // All lower-case, for the sake of case-insensitive comparisons
		  u8"windows-1251"sv,
		  u8"windows-1252"sv
	  };

static std::array<std::u8string_view, num_legacy_encodings> const
	encoding_human_names
	= { u8"Windows 1251 (Cyrillic)"sv, u8"Windows 1252 (Western)"sv };

std::optional<legacy_encoding>
legacy_encoding_by_code_name(std::u8string_view name) {
	std::u8string lowercaseName
		= ascii_characters_to_lowercase_in_utf8_string(name);
	for (std::size_t i = 0; i != encoding_code_names.size(); ++i) {
		if (lowercaseName == encoding_code_names[i]) {
			return (legacy_encoding) i;
		}
	}

	return std::nullopt;
}

std::u8string_view code_name_of_legacy_encoding(legacy_encoding encoding
) noexcept {
	return encoding_code_names[(std::size_t) encoding];
}

std::u8string_view human_name_of_legacy_encoding(legacy_encoding encoding
) noexcept {
	return encoding_human_names[(std::size_t) encoding];
}

static std::array<char16_t, 0xBF - 0x80 + 1> const
	windows_1251_unicode_equivalents_for_0x80_to_0xbf_inclusive
	= { 0x0402, // 0x80 in windows-1251 equals the code point U+0402,
		0x0403, // 0x81 in windows-1251 equals the code point U+0403,
		0x201A, // and so on...
		0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409,
		0x2039, 0x040A, 0x040C, 0x040B, 0x040F, 0x0452, 0x2018, 0x2019,
		0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x0098, 0x2122, 0x0459,
		0x203A, 0x045A, 0x045C, 0x045B, 0x045F, 0x00A0, 0x040E, 0x045E,
		0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7, 0x0401, 0x00A9, 0x0404,
		0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407, 0x00B0, 0x00B1, 0x0406,
		0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7, 0x0451, 0x2116, 0x0454,
		0x00BB, 0x0458, 0x0405, 0x0455, 0x0457 };

static std::array<char16_t, 0x9F - 0x80 + 1> const
	windows_1252_unicode_equivalents_for_0x80_to_0x9f_inclusive
	= { 0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
		0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
		0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
		0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178 };

struct conversion_data {
	char16_t const * table;
	char16_t add_to_characters_past_the_table;
	unsigned char last_character_to_use_table_for;
};

constexpr unsigned char first_non_ascii_character = 0x80;

static std::array<conversion_data, num_legacy_encodings> const
	conversion_data_by_encoding
	= {
		  conversion_data{
						  windows_1251_unicode_equivalents_for_0x80_to_0xbf_inclusive
				  .data(),
						  0x0410 - 0xC0,
						  windows_1251_unicode_equivalents_for_0x80_to_0xbf_inclusive
					  .size()
				  + first_non_ascii_character,
						  },
		  conversion_data{
						  windows_1252_unicode_equivalents_for_0x80_to_0x9f_inclusive
				  .data(),
						  0,										windows_1252_unicode_equivalents_for_0x80_to_0x9f_inclusive
					  .size()
				  + first_non_ascii_character,
						  }
};

std::u8string
legacy_encoding_to_utf8(std::string_view input, legacy_encoding encoding) {
	std::u8string output;
	output.reserve(input.length() * 3);

	conversion_data const & conversionData
		= conversion_data_by_encoding[(std::size_t) encoding];

	for (char const w : input) {
		unsigned char const c = (unsigned char) w;
		if (c < first_non_ascii_character) {
			output += (char8_t) c;
		} else {
			// Find the equivalent code point
			char16_t codePoint = c
				+ conversionData.add_to_characters_past_the_table;
			if (c <= conversionData.last_character_to_use_table_for) {
				std::size_t const index = std::size_t(c) - 0x80;
				codePoint = conversionData.table[index];
			}

			// Encode the code point
			// All of the code points in the tables are between U+0080 and
			// U+FFFF so they always need 2 or 3 UTF-8 code units
			if (codePoint < 0x800) {
				output += 0b1100'0000 | (codePoint >> 6);
				output += 0b1000'0000 | (codePoint & 0b0011'1111);
			} else {
				output += 0b1110'0000 | (codePoint >> 12);
				output += 0b1000'0000 | ((codePoint >> 6) & 0b0011'1111);
				output += 0b1000'0000 | (codePoint & 0b0011'1111);
			}
		}
	}

	output.shrink_to_fit();
	return output;
}
