#include "numeric_string_conversions.h"

#include <algorithm>
#include <charconv>

std::optional<std::int64_t> clamp_signed_integer_from_string(
	std::u8string_view valueString, std::int64_t min, std::int64_t max
) noexcept {
	std::int64_t parsed;
	std::errc error = std::from_chars(
						  (char const *) valueString.begin(),
						  (char const *) valueString.end(),
						  parsed
	)
						  .ec;

	if (error == std::errc::result_out_of_range) [[unlikely]] {
		parsed = valueString.starts_with(u8'-')
			? std::numeric_limits<std::int64_t>::min()
			: std::numeric_limits<std::int64_t>::max();
	} else if (error != std::errc{}) {
		return std::nullopt;
	}

	return std::clamp(parsed, min, max);
}

std::optional<std::uint64_t> clamp_unsigned_integer_from_string(
	std::u8string_view valueString, std::uint64_t min, std::uint64_t max
) noexcept {
	std::uint64_t parsed;
	std::errc error = std::from_chars(
						  (char const *) valueString.begin(),
						  (char const *) valueString.end(),
						  parsed
	)
						  .ec;

	bool const failedBecauseOfNegativeNumber = error
			== std::errc::invalid_argument
		&& valueString >= u8"-0" && valueString <= u8"-9";

	if (failedBecauseOfNegativeNumber) [[unlikely]] {
		parsed = 0;
	} else if (error == std::errc::result_out_of_range) [[unlikely]] {
		parsed = std::numeric_limits<std::uint64_t>::max();
	} else if (error != std::errc{}) {
		return std::nullopt;
	}
	return std::clamp(parsed, min, max);
}
