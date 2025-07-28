#include "developer_level.h"

#include "numeric_string_conversions.h"
#include "utf8.h"

#include <array>
#include <utility>

// All lower-case, for the sake of case-insensitive comparisons
using namespace std::literals;
constexpr std::array<std::u8string_view, num_developer_levels> const
	developer_level_names{ u8"disabled"sv, u8"error"sv, u8"warning"sv,
						   u8"message"sv,  u8"fluff"sv, u8"spam"sv,
						   u8"megaspam"sv };

std::u8string_view name_of_developer_level(developer_level level) noexcept {
	return developer_level_names[std::to_underlying(level)];
}

static std::optional<developer_level>
developer_level_from_name(std::u8string_view name) {
	std::u8string lowercaseName
		= ascii_characters_to_lowercase_in_utf8_string(name);
	for (std::size_t i = 0; i != developer_level_names.size(); ++i) {
		if (lowercaseName == developer_level_names[i]) {
			return developer_level(i);
		}
	}
	return std::nullopt;
}

static std::optional<developer_level>
developer_level_from_integer_string(std::u8string_view intString) noexcept {
	return clamp_signed_integer_from_string(
			   intString,
			   std::to_underlying(developer_level::disabled),
			   std::to_underlying(developer_level::max_level)
	)
		.transform([](std::int64_t integerForm) {
			return developer_level(integerForm);
		});
}

std::optional<developer_level>
developer_level_from_string(std::u8string_view nameOrIntegerString) {
	return developer_level_from_name(nameOrIntegerString)
		.or_else([&nameOrIntegerString]() {
			return developer_level_from_integer_string(nameOrIntegerString);
		});
}
