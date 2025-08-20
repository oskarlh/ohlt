#pragma once

#include "utf8.h"

#include <string_view>

[[nodiscard]] constexpr std::u8string_view
skip_ascii_whitespace(std::u8string_view str) noexcept {
	std::size_t const skipTo{ str.find_first_not_of(ascii_whitespace) };
	if (skipTo == std::u8string_view::npos) {
		return {};
	}

	return str.substr(skipTo);
}

// Skips past comments starting with // or # or ;
constexpr void skip_whitespace_and_comments(std::u8string_view& str
) noexcept {
	bool foundComment{};
	do {
		str = skip_ascii_whitespace(str);

		foundComment = str.starts_with(u8"//") || str.starts_with(u8"#")
			|| str.starts_with(u8";");

		if (foundComment) [[unlikely]] {
			std::size_t const endOfLine = str.find(u8'\n');
			str = str.substr(endOfLine + 1);
			bool const isEndOfFile = endOfLine == std::u8string_view::npos;
			if (isEndOfFile) {
				str = {};
			}
		}
	} while (foundComment);
}

constexpr std::u8string_view
extract_current_line_then_skip_whitespace_and_comments(
	std::u8string_view& remainingInput
) noexcept {
	std::size_t const eol{ remainingInput.find(u8'\n') };
	std::u8string_view lineContents = remainingInput.substr(0, eol);
	if (eol == std::u8string_view::npos) {
		remainingInput = {};
	}
	remainingInput.remove_prefix(eol + 1);
	skip_whitespace_and_comments(remainingInput);
	return lineContents;
}

constexpr std::u8string_view next_word(std::u8string_view& text) {
	skip_whitespace_and_comments(text);
	std::u8string_view const word = text.substr(
		0, text.find_first_not_of(ascii_whitespace)
	);
	text = text.substr(word.length());
	return word;
}
