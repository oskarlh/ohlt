#pragma once

#include <optional>
#include <string>

enum class legacy_encoding {
	windows_1251 = 0,
	windows_1252,
};
constexpr std::size_t num_legacy_encodings = std::size_t(legacy_encoding::windows_1252) + 1;

std::optional<legacy_encoding> legacy_encoding_by_code_name(std::u8string_view name);
std::u8string_view name_of_legacy_encoding(legacy_encoding encoding) noexcept;
std::u8string_view keyword_for_legacy_encoding(legacy_encoding encoding) noexcept;

std::u8string legacy_encoding_to_utf8(std::string_view input, legacy_encoding encoding);

//constexpr legacy_encoding default_legacy_encoding = legacy_encoding::windows_1252;
