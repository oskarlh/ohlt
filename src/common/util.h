#pragma once

#include <bit>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <variant>

template <class... Ts>
struct overload final : Ts... {
	using Ts::operator()...;
};
template <class... Ts>
overload(Ts...) -> overload<Ts...>;

template <class var_t, class... Func>
auto visit_with(var_t&& variant, Func&&... funcs) {
	return std::visit(overload{ funcs... }, variant);
}

template <std::unsigned_integral Uint>
[[nodiscard]] constexpr bool
is_bit_set(Uint bitSet, std::size_t numberOfBitToCheck) noexcept {
	if (numberOfBitToCheck >= std::numeric_limits<Uint>::digits) {
		return false;
	}
	return (bitSet & (Uint(1) << numberOfBitToCheck)) != 0;
}

class indicies_of_set_bits_iterator final {
  private:
	std::uintmax_t bitSet{};
	int countrResultSum{};

  public:
	using value_type = std::size_t;

	constexpr indicies_of_set_bits_iterator& operator++() noexcept {
		bitSet &= ~std::uintmax_t(1);
		int const shiftsToSkipToNextOne = std::countr_zero(bitSet)
			% std::numeric_limits<std::uintmax_t>::digits;
		countrResultSum += shiftsToSkipToNextOne;
		bitSet >>= shiftsToSkipToNextOne;
		return *this;
	}

	constexpr std::ptrdiff_t
	operator-(indicies_of_set_bits_iterator const & other) const noexcept {
		return std::popcount(other.bitSet) - std::popcount(bitSet);
	}

	constexpr indicies_of_set_bits_iterator() noexcept = default;
	constexpr indicies_of_set_bits_iterator(indicies_of_set_bits_iterator const &)
		noexcept
		= default;

	constexpr indicies_of_set_bits_iterator(std::uintmax_t bits) noexcept {
		int const shiftsToSkipToFirstOne = std::countr_zero(bits)
			% std::numeric_limits<std::uintmax_t>::digits;
		countrResultSum = shiftsToSkipToFirstOne;
		bitSet = bits >> shiftsToSkipToFirstOne;
	}

	constexpr value_type operator*() const noexcept {
		return std::size_t(countrResultSum);
	}

	constexpr bool operator!=(indicies_of_set_bits_iterator const & other
	) const noexcept {
		return bitSet != other.bitSet;
	}

	constexpr std::strong_ordering
	operator<=>(indicies_of_set_bits_iterator const & other
	) const noexcept {
		return countrResultSum <=> other.countrResultSum;
	}
};

class indicies_of_set_bits_view final
	: public std::ranges::view_interface<indicies_of_set_bits_view> {
  private:
	indicies_of_set_bits_iterator beginning;

  public:
	constexpr indicies_of_set_bits_view() noexcept = default;

	constexpr indicies_of_set_bits_view(std::uintmax_t bits) noexcept :
		beginning(bits) { }

	constexpr indicies_of_set_bits_iterator begin() const noexcept {
		return beginning;
	}

	constexpr indicies_of_set_bits_iterator end() const noexcept {
		return indicies_of_set_bits_iterator{};
	}
};

// TODO: Make this a range view instead
[[nodiscard]] constexpr indicies_of_set_bits_view
indicies_of_set_bits(std::uintmax_t bitSet) noexcept {
	return indicies_of_set_bits_view{ bitSet };
}

// TODO: Remove this when we no longer need it. It's probably only useful
// with old C functions like qsort
constexpr int ordering_as_int(std::strong_ordering cmp) noexcept {
	return (cmp < 0) ? -1 : ((cmp == 0) ? 0 : 1);
}

constexpr int ordering_as_int(std::weak_ordering cmp) noexcept {
	return (cmp < 0) ? -1 : ((cmp == 0) ? 0 : 1);
}

constexpr int ordering_as_int(std::partial_ordering cmp) noexcept {
	return (cmp < 0) ? -1 : ((cmp == 0) ? 0 : 1);
}
