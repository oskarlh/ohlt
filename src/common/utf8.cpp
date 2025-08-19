#include "utf8.h"

#include <bit>

namespace utf8_internal {
	bool validate_utf8_continuing_code_units(
		unsigned char const *& it,
		unsigned char const * end,
		unsigned char firstCodeUnitInCodePoint
	) noexcept {
		std::size_t codeUnitsToCheck
			= std::countl_one(firstCodeUnitInCodePoint) - 1;
		std::size_t const unitsRemaining = end - it;

		bool invalid = codeUnitsToCheck == 0 || codeUnitsToCheck > 3;
		bool earlyEnd = unitsRemaining < codeUnitsToCheck;
		if (invalid || earlyEnd) [[unlikely]] {
			return false;
		}
		do {
			unsigned char const continuingCodeUnit = *it;
			if (continuingCodeUnit < 0b1000'0000
			    || continuingCodeUnit > 0b1011'1111) [[unlikely]] {
				return false;
			}
			++it;
		} while (--codeUnitsToCheck);
		return true;
	}
} // namespace utf8_internal

// Case-insensitive substring matching
bool a_contains_b_ignoring_ascii_character_case_differences(
	std::u8string_view string, std::u8string_view substring
) {
	std::u8string string_lowercase
		= ascii_characters_to_lowercase_in_utf8_string(string);
	std::u8string substring_lowercase
		= ascii_characters_to_lowercase_in_utf8_string(substring);
	return string_lowercase.contains(substring_lowercase);
}

// Case-insensitive prefix testing
bool a_starts_with_b_ignoring_ascii_character_case_differences(
	std::u8string_view string, std::u8string_view prefix
) {
	std::u8string string_lowercase
		= ascii_characters_to_lowercase_in_utf8_string(string);
	std::u8string prefix_lowercase
		= ascii_characters_to_lowercase_in_utf8_string(prefix);
	return string_lowercase.starts_with(prefix_lowercase);
}
