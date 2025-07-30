#pragma once
#include <optional>
#include <string_view>

// TODO: Rename to developer_logging_level
enum class developer_level {
	disabled,
	error,
	warning,
	message,
	fluff,
	spam,
	megaspam,
	max_level = megaspam
};

constexpr std::u8string_view developer_level_options{
	u8"disabled|error|warning|message|fluff|spam|megaspam"
};

constexpr std::size_t num_developer_levels
	= std::size_t(developer_level::max_level) + 1;

std::optional<developer_level>
developer_level_from_string(std::u8string_view nameOrIntegerString);

std::u8string_view name_of_developer_level(developer_level level) noexcept;
