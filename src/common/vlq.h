#pragma once
// TODO: Delete this code if it's no longer needed

#include <bit>
#include <concepts>
#include <limits>
#include <optional>
#include <string>

// VLQ is a variable-length integer encoding
// See https://en.wikipedia.org/wiki/Variable-length_quantity
template <std::unsigned_integral UInt, std::unsigned_integral Octet>
struct unsigned_vlq_decoding_result final {
	UInt result;
	Octet const * endOfVlq;
};

template <std::unsigned_integral UInt>
constexpr std::size_t
octets_required_for_unsigned_vlq_encoding_of(UInt integer) noexcept {
	return std::max(1, (std::bit_width(integer) + 6) / 7);
}

// Precondition: `octets` points to
// `octets_required_for_unsigned_vlq_encoding_of(integer)` bytes or more.
// Returns: the end of the encoded VLQ
template <std::unsigned_integral UInt, std::unsigned_integral Octet>
constexpr Octet* encode_unsigned_vlq(UInt integer, Octet* octets) noexcept {
	std::size_t shift = octets_required_for_unsigned_vlq_encoding_of(integer
						)
		* 7;

	do {
		shift -= 7;
		*octets = std::uint8_t(integer >> (shift)) | 0b1000'0000;
		++octets;
	} while (shift != 0);
	*(octets - 1) &= 0b0111'1111;
	return octets;
}

template <std::unsigned_integral UInt, std::unsigned_integral Octet>
constexpr unsigned_vlq_decoding_result<UInt, Octet>
decode_unsigned_vlq_unchecked(Octet const * octets) noexcept {
	UInt result{};
	Octet const * nextOctet = octets;
	Octet octet;
	do {
		result <<= 7;
		octet = *nextOctet;
		result += octet & 0b0111'1111;
		++nextOctet;
	} while (octet >= 0b1000'0000);
	return { result, nextOctet };
}

// This version returns nullopt if the string terminates prematurely
// or if the integer can't fit in UInt
template <std::unsigned_integral UInt, std::unsigned_integral Octet>
constexpr std::optional<unsigned_vlq_decoding_result<UInt, Octet>>
decode_unsigned_vlq_unchecked(std::basic_string_view<Octet> octets
) noexcept {
	UInt result{};
	Octet const * nextOctet = octets.begin();
	Octet octet;
	do {
		if (nextOctet == octets.end()) [[unlikely]] {
			// Premature end
			return std::nullopt;
		}
		if (result >= (UInt(1) << (std::numeric_limits<UInt>::digits - 7)))
			[[unlikely]] {
			// Integer too large to fit in Octet
			return std::nullopt;
		}

		result <<= 7;
		octet = *nextOctet;
		result += octet & 0b0111'1111;
		++nextOctet;
	} while (octet >= 0b1000'0000);
	return { result, nextOctet };
}
