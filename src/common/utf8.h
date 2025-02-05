#pragma once

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <ranges>
#include <string>
#include <utility>

constexpr bool is_ascii_code_unit(char codeUnit) noexcept {
	return ((unsigned char) codeUnit) <= 0x7F;
}

namespace utf8_internal {
	bool validate_utf8_continuing_code_units(
		unsigned char const *& it,
		unsigned char const * end,
		unsigned char firstCodeUnitInCodePoint
	) noexcept;
} // namespace utf8_internal

template <std::ranges::contiguous_range Range>
inline bool validate_utf8(Range&& string) noexcept {
	unsigned char const * it = (unsigned char const *) std::ranges::data(
		string
	);
	unsigned char const * end = (unsigned char const *) std::to_address(
		std::ranges::end(string)
	);

	// Skip past ASCII code units, eight at a time
	bool allAscii = true;
	for (unsigned char const * eightsEnd = it
			 + (std::size_t(end - it) / 8) * 8;
		 allAscii && it != eightsEnd;
		 it += 8) {
		using eight_units = char8_t[8];
		static_assert(
			std::numeric_limits<unsigned char>::digits * 8
			== std::numeric_limits<std::uint64_t>::digits
		);
		std::uint64_t const eightUnitsAsInt64
			= std::bit_cast<std::uint64_t>((eight_units const *) it);

		allAscii = (eightUnitsAsInt64 & 0x8080'8080'8080'8080) == 0;
	}

	while (it != end) {
		unsigned char const firstCodeUnitInCodePoint = *it;
		++it;
		if (!is_ascii_code_unit(firstCodeUnitInCodePoint)) [[unlikely]] {
			if (!utf8_internal::validate_utf8_continuing_code_units(
					it, end, firstCodeUnitInCodePoint
				)) [[unlikely]] {
				return false;
			}
		}
	};
	return true;
}

constexpr std::u8string_view ascii_whitespace{ u8" \f\n\r\t\v" };

constexpr bool is_ascii_whitespace(char8_t c) noexcept {
	return ascii_whitespace.contains(c);
}

[[nodiscard]] constexpr char8_t ascii_character_to_lowercase(char8_t c
) noexcept {
	char8_t const add = (c >= u8'A' && c <= u8'Z') ? (u8'a' - u8'A')
												   : u8'\0';
	return c + add;
}

[[nodiscard]] constexpr char8_t ascii_character_to_uppercase(char8_t c
) noexcept {
	char8_t const subtract = (c >= u8'a' && c <= u8'z') ? (u8'a' - u8'A')
														: u8'\0';
	return c - subtract;
}

[[nodiscard]] constexpr auto
ascii_characters_to_lowercase_in_utf8_string_as_view(
	std::u8string_view input
) noexcept {
	return std::ranges::transform_view(input, ascii_character_to_lowercase);
}

[[nodiscard]] constexpr auto
ascii_characters_to_uppercase_in_utf8_string_as_view(
	std::u8string_view input
) noexcept {
	return std::ranges::transform_view(input, ascii_character_to_uppercase);
}

[[nodiscard]] constexpr std::u8string
ascii_characters_to_lowercase_in_utf8_string(std::u8string_view input
) noexcept {
	auto lowercaseView
		= ascii_characters_to_lowercase_in_utf8_string_as_view(input);
	return std::u8string{ lowercaseView.begin(), lowercaseView.end() };
}

[[nodiscard]] constexpr std::u8string
ascii_characters_to_uppercase_in_utf8_string(std::u8string_view input
) noexcept {
	auto uppercaseView
		= ascii_characters_to_uppercase_in_utf8_string_as_view(input);
	return std::u8string{ uppercaseView.begin(), uppercaseView.end() };
}

template <class U8String>
constexpr void
make_ascii_characters_lowercase_in_utf8_string(U8String& input) noexcept {
	for (char8_t& c : input) {
		c = ascii_character_to_lowercase(c);
	}
}

template <class U8String>
constexpr void
make_ascii_characters_uppercase_in_utf8_string(U8String& input) noexcept {
	for (char8_t& c : input) {
		c = ascii_character_to_uppercase(c);
	}
}

constexpr bool strings_equal_with_ascii_case_insensitivity(
	std::u8string_view a, std::u8string_view b
) noexcept {
	return std::ranges::equal(
		ascii_characters_to_lowercase_in_utf8_string_as_view(a),
		ascii_characters_to_lowercase_in_utf8_string_as_view(b)
	);
}

// TODO: Delete these three when we're using UTF-8 types in enough places
constexpr bool strings_equal_with_ascii_case_insensitivity(
	std::u8string_view a, std::string_view b
) noexcept {
	return strings_equal_with_ascii_case_insensitivity(
		a,
		std::u8string_view(
			(char8_t const *) b.begin(), (char8_t const *) b.end()
		)
	);
}

constexpr bool strings_equal_with_ascii_case_insensitivity(
	std::string_view a, std::u8string_view b
) noexcept {
	return strings_equal_with_ascii_case_insensitivity(b, a);
}

constexpr bool strings_equal_with_ascii_case_insensitivity(
	std::string_view a, std::string_view b
) noexcept {
	return strings_equal_with_ascii_case_insensitivity(
		std::u8string_view(
			(char8_t const *) a.begin(), (char8_t const *) a.end()
		),
		b
	);
}

template <class Number, class Char = char8_t>
struct parse_number_result final {
	std::optional<Number> number;
	std::basic_string_view<Char> remainingText;
};

template <class Number = std::int32_t>
constexpr parse_number_result<Number, char>
parse_number(std::string_view str) noexcept {
	Number number;
	std::from_chars_result const fromCharsResult{
		std::from_chars(str.begin(), str.end(), number)
	};

	if (fromCharsResult.ec == std::errc{}) [[likely]] {
		return { .number = number,
				 .remainingText = { fromCharsResult.ptr, str.end() } };
	}
	return { .number = std::nullopt, .remainingText = str };
}

template <std::floating_point Number>
constexpr parse_number_result<Number, char>
parse_number(std::string_view str) noexcept {
	// TODO: Once we no longer care about Clang < 20 support, we can use
	// std::from_chars for floating-point numbers too
	constexpr std::string_view floatChars{ "+-.eE0123456789" };

	std::string zeroTerminatedCopy{
		str.begin(), std::min(str.size(), str.find_first_not_of(floatChars))
	};

	char* rest = zeroTerminatedCopy.data();
	Number const number{ std::strtod(zeroTerminatedCopy.data(), &rest) };
	std::size_t const numCharsParsed = rest - zeroTerminatedCopy.data();
	if (numCharsParsed == 0) [[unlikely]] {
		return { .number = std::nullopt, .remainingText = {} };
	}
	return { .number = number,
			 .remainingText = str.substr(numCharsParsed) };
}

template <class Number = std::int32_t>
constexpr parse_number_result<Number, char8_t>
parse_number(std::u8string_view str) noexcept {
	auto const res = parse_number<Number>(std::string_view{
		(char const *) str.begin(), (char const *) str.end() });
	return { .number = res.number,
			 .remainingText = { (char8_t const *) res.remainingText.begin(),
								res.remainingText.length() } };
}

template <class Number = std::int32_t>
constexpr std::optional<Number>
string_to_number(std::string_view str, Number errorValue = 0) noexcept {
	return parse_number(str).number;
}

template <class Number = std::int32_t>
constexpr Number
string_to_number(std::u8string_view str, Number errorValue = 0) noexcept {
	return parse_number(str).number;
}
