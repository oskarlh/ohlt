#pragma once

#include <cstdint>
#include <optional>
#include <string>

std::optional<std::int64_t> clamp_signed_integer_from_string(
	std::u8string_view valueString, std::int64_t min, std::int64_t max
) noexcept;

std::optional<std::uint64_t> clamp_unsigned_integer_from_string(
	std::u8string_view valueString, std::uint64_t min, std::uint64_t max
) noexcept;
