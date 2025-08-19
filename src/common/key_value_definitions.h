
#pragma once

#include "entity_key_value.h"

#include <array>
#include <charconv>
#include <cmath>
#include <concepts>
#include <string_view>

template <std::floating_point FP>
constexpr std::size_t chars_needed_for_negative_smallest_fp_in_text =
#ifdef __cpp_lib_constexpr_cmath
	std::ceil(std::abs(std::log10(std::numeric_limits<FP>::denorm_min())))
#else
	999 // TODO: Remove once compilers support constexpr ceil/abs/log10
#endif
	+ 3; // +3 for "-0."

template <std::floating_point FP>
constexpr std::size_t chars_needed_for_negative_largest_fp_in_text =
#ifdef __cpp_lib_constexpr_cmath
	std::ceil(std::log10(std::numeric_limits<double>::max()))
#else
	999 // TODO: Remove once compilers support constexpr ceil/abs/log10
#endif
	+ 1; // +1 for "-"

template <std::floating_point FP>
constexpr std::size_t max_chars_needed_for_fp_in_text = std::
	max(chars_needed_for_negative_smallest_fp_in_text<FP>,
        chars_needed_for_negative_largest_fp_in_text<FP>);

struct bool_key_value_def {
	std::u8string_view keyName;
	bool const defaultValue{ false };

	void set(entity_key_value& kv, bool newValue) const {
		if (newValue) {
			kv.set_value(u8"1");
		} else {
			kv.remove();
		}
	}

	bool get(std::u8string_view str) const noexcept {
		return str != u8"" && str != u8"0";
	}
};

template <std::integral Int>
struct integer_key_value_def {
	using value_type = Int;

	std::u8string_view keyName;
	Int defaultValue{};

	void set(entity_key_value& kv, Int newValue) const {
		std::array<
			char8_t,
			std::numeric_limits<Int>::digits10 + 2
			// +1 for "-" and +1 because digits10 only includes
		    // digits that can have any value (so the '2' in 255 for
		    // std::uint8_t isn't included)
			>
			chars;

		std::to_chars_result r = std::to_chars(
			(char*) chars.begin(), (char*) chars.end(), newValue
		);

		if (r.ec != std::errc{}) [[unlikely]] {
			// This should be impossible, as chars should always be
			// large enough
			return;
		}
		std::u8string_view asString(chars.begin(), r.ptr);
		kv.set_value(asString);
	}

	Int get(std::u8string_view str) const noexcept {
		if (str.empty()) {
			return defaultValue;
		}

		Int num;
		std::from_chars_result const fromCharsResult{ std::from_chars(
			(char const *) str.begin(), (char const *) str.end(), num
		) };

		if (fromCharsResult.ec == std::errc::result_out_of_range)
			[[unlikely]] {
			// When the integer type is unsigned, negative number result in
			// std::errc::invalid_argument, not
			// std::errc::result_out_of_range
			bool const negative = std::is_signed_v<Int>
				&& str.starts_with(u8'-');

			return negative ? std::numeric_limits<Int>::min()
							: std::numeric_limits<Int>::max();
		}
		if (fromCharsResult.ec == std::errc::invalid_argument)
			[[unlikely]] {
			bool const unexpectedNegative = std::is_unsigned_v<Int>
				&& str.starts_with(u8'-');

			return unexpectedNegative ? 0 : defaultValue;
		}

		return num;
	}
};

template <std::floating_point FP>
FP to_finite(FP value) {
	if (std::isfinite(value)) [[likely]] {
		return value;
	}

	if (value == std::numeric_limits<FP>::infinity()) {
		return std::numeric_limits<FP>::max();
	}
	if (value == -std::numeric_limits<FP>::infinity()) {
		return std::numeric_limits<FP>::denorm_min();
	}
	// value is NaN
	return 0;
}

template <std::floating_point FP>
struct fp_key_value_def {
	using value_type = FP;
	std::u8string_view keyName;
	FP defaultValue{};

	void set(entity_key_value& kv, FP newValue) const {
		std::array<char8_t, max_chars_needed_for_fp_in_text<FP>> chars;

		std::to_chars_result r = std::to_chars(
			(char*) chars.begin(),
			(char*) chars.end(),
			to_finite(newValue),
			std::chars_format::fixed
		);

		if (r.ec != std::errc{}) [[unlikely]] {
			// This should be impossible, as chars should always be
			// large enough
			return;
		}
		std::u8string_view asString(chars.begin(), r.ptr);
		kv.set_value(asString);
	}

	FP get(std::u8string_view str) const noexcept {
		if (str.empty()) {
			return defaultValue;
		}

		FP num;
		std::from_chars_result const fromCharsResult{ std::from_chars(
			(char const *) str.begin(), (char const *) str.end(), num
		) };

		if (fromCharsResult.ec == std::errc::result_out_of_range)
			[[unlikely]] {
			bool const negative = str.starts_with(u8'-');

			return negative ? -std::numeric_limits<FP>::max()
							: std::numeric_limits<FP>::max();
		}
		if (fromCharsResult.ec == std::errc::invalid_argument)
			[[unlikely]] {
			return defaultValue;
		}
		return num;
	}

	template <std::floating_point OtherFP>
	fp_key_value_def<OtherFP> as() const noexcept {
		return { .keyName = keyName,
			     .defaultValue = OtherFP(defaultValue) };
	}
};

template <std::floating_point FP>
struct vec3_key_value_def {
	using value_type = std::array<FP, 3>;
	std::u8string_view keyName;
	std::array<FP, 3> defaultValue{};

	void
	set(entity_key_value& kv, std::array<FP, 3> const & newValue) const {
		std::array<
			char8_t,
			max_chars_needed_for_fp_in_text<FP> * 3
				+ 3 // 2 spaces between, and 1 extra at the end for simpler
		            // logic
			>
			chars;

		char8_t* ptr = chars.begin();
		for (FP v : newValue) {
			std::to_chars_result r = std::to_chars(
				(char*) ptr,
				(char*) chars.end(),
				to_finite(v),
				std::chars_format::fixed
			);
			ptr = (char8_t*) r.ptr;
			if (r.ec != std::errc{} || r == chars.end()) [[unlikely]] {
				// This should be impossible, as chars should always be
				// large enough
				return;
			}
			*ptr++ = u8' ';
		}
		std::u8string_view asString(chars.begin(), ptr - 1);
		kv.set_value(asString);
	}

	std::array<FP, 3> get(std::u8string_view str) const noexcept {
		if (str.empty()) {
			return defaultValue;
		}

		std::u8string_view remaining{ str };
		std::array<FP, 3> vecResult{};
		for (FP& element : vecResult) {
			std::from_chars_result const fromCharsResult{ std::from_chars(
				(char const *) str.begin(),
				(char const *) str.end(),
				element
			) };

			if (fromCharsResult.ec == std::errc::result_out_of_range)
				[[unlikely]] {
				bool const negative = str.starts_with(u8'-');

				element = negative ? -std::numeric_limits<FP>::max()
								   : std::numeric_limits<FP>::max();
			} else if (fromCharsResult.ec == std::errc::invalid_argument)
				[[unlikely]] {
				return defaultValue;
			}

			// The game allows short vectors like "1" and "1 2", so we do
			// too. The elements that are left out default to 0
			if (!remaining.starts_with(u8' ')) {
				break;
			}
			remaining.remove_prefix(1);
		}

		return vecResult;
	}

	template <std::floating_point OtherFP>
	vec3_key_value_def<OtherFP> as() const noexcept {
		return { .keyName = keyName,
			     .defaultValue = to_vec3<OtherFP>(defaultValue) };
	}
};

struct string_key_value_def {
	using value_type = std::u8string_view;
	std::u8string_view keyName;
	std::u8string_view defaultValue{};

	void set(entity_key_value& kv, std::u8string_view newValue) const {
		kv.set_value(newValue);
	}

	std::u8string_view get(std::u8string_view str) const noexcept {
		if (str.empty()) {
			return defaultValue;
		}
		return str;
	}
};
