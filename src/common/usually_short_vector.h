#pragma once

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <span>
#include <vector>

template <class Type>
constexpr bool is_void = std::is_same_v<Type, void>;

struct void_dummy_type { };

template <class Type>
using replace_void_with_dummy
	= std::conditional_t<std::is_same_v<Type, void>, void_dummy_type, Type>;

template <std::size_t NumElements>
using short_size_for_num_elements = std::conditional_t<
	(NumElements <= std::numeric_limits<std::uint8_t>::max() - 1),
	std::uint8_t,
	std::conditional_t<
		(NumElements <= std::numeric_limits<std::uint16_t>::max() - 1),
		std::uint16_t,
		std::size_t>>;

template <class ShortSize>
struct header_base {
	ShortSize sizeOrLongHeaderMark; // -1 == this is a long_header
};

template <class ExtraData>
class with_extra_data {
  private:
	ExtraData extraData;

	constexpr with_extra_data(ExtraData&& ed) noexcept : extraData(ed) {};

  public:
	constexpr with_extra_data() noexcept = default;
	constexpr with_extra_data(with_extra_data const & otherExtraData
	) noexcept
		= default;
	constexpr with_extra_data(with_extra_data&& otherExtraData) noexcept
		= default;

	template <class OtherExtraData>
	constexpr void
	move_extra_from(with_extra_data<OtherExtraData>&& otherWrapper
	) noexcept {
		extraData = std::move(otherWrapper.extraData);
	}

	constexpr ExtraData& extra_data_ref() noexcept {
		return extraData;
	}

	constexpr ExtraData const & extra_data_ref() const noexcept {
		return extraData;
	}
};

template <class ExtraData>
requires(is_void<ExtraData>) struct with_extra_data<ExtraData> {
	constexpr with_extra_data() = default;
	constexpr with_extra_data(with_extra_data const & otherExtraData
	) = default;
	constexpr with_extra_data(with_extra_data&& otherExtraData) = default;

	template <class OtherExtraData>
	constexpr void
	move_extra_from(with_extra_data<OtherExtraData>&& otherWrapper
	) noexcept { }

	constexpr void_dummy_type extra_data_ref() const noexcept {
		return {};
	}
};

template <class Value, class ShortSize, class LongExtraData>
struct long_header final : header_base<ShortSize>,
						   with_extra_data<LongExtraData> {
	using header_base<ShortSize>::sizeOrLongHeaderMark;

	std::vector<Value> storageVector{};

	constexpr long_header() noexcept {
		sizeOrLongHeaderMark = -1;
	}
};

template <
	class Value,
	class ShortSize,
	class ShortExtraData,
	std::size_t InplaceCapacity>
struct short_header final : header_base<ShortSize>,
							with_extra_data<ShortExtraData> {
	using header_base<ShortSize>::sizeOrLongHeaderMark;

	alignas(Value
	) std::array<std::byte, InplaceCapacity * sizeof(Value)> storageArray;

	constexpr short_header() = default;
	short_header(short_header const & other) = delete;			  // TODO?
	short_header(short_header&& other) = delete;				  // TODO?
	short_header& operator=(short_header const & other) = delete; // TODO?
	short_header& operator=(short_header&& other) = delete;		  // TODO?

	constexpr ~short_header() {
		std::ranges::destroy(
			(Value*) storageArray.begin(),
			((Value*) storageArray.begin()) + sizeOrLongHeaderMark
		);
	}
};

template <
	class Value,
	// Recommendation: Experiment with different InplaceCapacity so you use
	// one that leads to as little wasted overhead as possible. Sometimes if
	// it's a low value, you can increase it without increasing the size of
	// the container.
	std::size_t InplaceCapacity,
	// Optional extra data. Just here to fit small data in the space between
	// values and size info, which could otherwise be wasted for padding due
	// to alignment requirements. `ShortExtraData` will be used when
	// `capacity() <= InplaceCapacity`, otherwise `LongExtraData`
	class ShortExtraData = void,
	class LongExtraData = ShortExtraData>
requires(
	(is_void<ShortExtraData> == is_void<LongExtraData>)
	&& (is_void<ShortExtraData>
		|| (std::is_nothrow_destructible_v<ShortExtraData>
			&& std::is_nothrow_move_assignable_v<ShortExtraData>
			&& std::is_nothrow_move_constructible_v<ShortExtraData>
			&& std::is_nothrow_default_constructible_v<ShortExtraData>
			&& std::
				is_nothrow_constructible_v<ShortExtraData, LongExtraData &&>
			&& std::is_nothrow_destructible_v<LongExtraData>
			&& std::is_nothrow_move_assignable_v<LongExtraData>
			&& std::is_nothrow_move_constructible_v<LongExtraData>
			&& std::is_nothrow_default_constructible_v<LongExtraData>
			&& std::is_nothrow_constructible_v<
				LongExtraData,
				ShortExtraData &&>) )
	&& std::is_nothrow_destructible_v<Value>
	&& std::is_nothrow_move_assignable_v<Value>
	&& std::is_nothrow_move_constructible_v<Value>

) class usually_short_vector final {
  public:
	using value_type = Value;
	using short_extra_type = ShortExtraData;
	using long_extra_type = LongExtraData;
	static constexpr bool stores_extra_data = !is_void<ShortExtraData>;
	static constexpr bool stores_one_kind_of_extra_data
		= std::is_same_v<ShortExtraData, LongExtraData>;
	static constexpr std::size_t inplace_capacity = InplaceCapacity;

  private:
	using short_size = short_size_for_num_elements<InplaceCapacity>;
	using header_base_type = header_base<short_size>;
	using short_header_type = short_header<
		value_type,
		short_size,
		short_extra_type,
		inplace_capacity>;
	using long_header_type
		= long_header<value_type, short_size, short_extra_type>;

	using short_extra_or_dummy_type
		= replace_void_with_dummy<short_extra_type>;
	using long_extra_or_dummy_type
		= replace_void_with_dummy<long_extra_type>;

	union header_union {
		header_base_type headerBase;
		short_header_type shortHeader;
		long_header_type longHeader;

		constexpr bool stored_inplace() const noexcept {
			return headerBase.sizeOrLongHeaderMark != -1;
		}

		constexpr header_union() noexcept : shortHeader{} { }

		constexpr std::size_t short_header_size() const noexcept {
			return shortHeader.sizeOrLongHeaderMark;
		}

		constexpr std::size_t long_header_size() const noexcept {
			return longHeader.storageVector.size();
		}

		constexpr std::size_t long_header_capacity() const noexcept {
			return longHeader.storageVector.capacity();
		}

		constexpr Value* short_header_begin() noexcept {
			return (Value*) shortHeader.storageArray.begin();
		}

		constexpr Value* long_header_begin() noexcept {
			return longHeader.storageVector.data();
		}

		constexpr std::span<value_type> short_header_span() noexcept {
			return { short_header_begin(), short_header_size() };
		}

		constexpr std::span<value_type> long_header_span() noexcept {
			return { long_header_begin(), long_header_size() };
		}

		constexpr std::size_t capacity() const noexcept {
			if (stored_inplace()) [[likely]] {
				return inplace_capacity;
			}
			return long_header_capacity();
		}

		constexpr std::size_t size() const noexcept {
			if (stored_inplace()) [[likely]] {
				return short_header_size();
			}
			return long_header_size();
		}

		constexpr std::span<value_type> span() noexcept {
			if (stored_inplace()) [[likely]] {
				return short_header_span();
			}
			return long_header_span();
		}

		constexpr std::variant<
			std::reference_wrapper<short_extra_or_dummy_type>,
			std::reference_wrapper<long_extra_or_dummy_type>>
		extra_ref() noexcept requires(stores_extra_data) {
			if (stored_inplace()) [[likely]] {
				return { std::in_place_index<0>,
						 shortHeader.extraDataRef() };
			}
			return { std::in_place_index<1>, longHeader.extraDataRef() };
		}

		constexpr std::variant<
			std::reference_wrapper<short_extra_or_dummy_type const>,
			std::reference_wrapper<long_extra_or_dummy_type const>>
		extra_ref() const noexcept requires(stores_extra_data) {
			if (stored_inplace()) [[likely]] {
				return { std::in_place_index<0>,
						 shortHeader.extraDataRef() };
			}
			return { std::in_place_index<1>, longHeader.extraDataRef() };
		}

		constexpr short_extra_or_dummy_type& extra() noexcept
			requires(stores_extra_data && stores_one_kind_of_extra_data) {
			if (stored_inplace()) [[likely]] {
				return shortHeader.extra_data_ref();
			}
			return longHeader.extra_data_ref();
		}

		constexpr short_extra_or_dummy_type const & extra() const noexcept
			requires(stores_extra_data && stores_one_kind_of_extra_data) {
			if (stored_inplace()) [[likely]] {
				return shortHeader.extra_data_ref();
			}
			return longHeader.extra_data_ref();
		}

		constexpr void shrink_to_fit() {
			if (!stored_inplace()) [[unlikely]] {
				bool const willFitInplace = long_header_size()
					<= inplace_capacity;
				if (willFitInplace) {
					std::vector<value_type> oldValues{
						std::move(longHeader.storageVector)
					};

					short_header_type newHeader;
					newHeader.move_extra_from(longHeader);
					longHeader.~long_header_type();
					new (&shortHeader)
						short_header_type{ std::move(newHeader) };

					shortHeader.sizeOrLongHeaderMark = oldValues.size();
					std::ranges::move(oldValues, short_header_begin());
				} else {
					longHeader.storageVector.shrink_to_fit();
				}
			}
		}

		constexpr void reserve(std::size_t newCapacity) noexcept {
			if (newCapacity <= inplace_capacity) [[likely]] {
				return;
			}
			if (stored_inplace()) {
				std::span<value_type> dataInOldLocation = short_header_span(
				);
				long_header_type newHeader{};
				newHeader.storageVector.reserve(newCapacity);
				newHeader.storageVector.insert(
					newHeader.storageVector.end(),
					std::make_move_iterator(dataInOldLocation.begin()),
					std::make_move_iterator(dataInOldLocation.end())
				);
				std::destroy(
					dataInOldLocation.begin(), dataInOldLocation.end()
				);
				newHeader.move_extra_from(shortHeader);
				shortHeader.~short_header_type();
				new (&longHeader) long_header_type{ std::move(newHeader) };
			} else {
				longHeader.storageVector.reserve(newCapacity);
			}
		}

		constexpr void swap(header_union&& other) {
			//////////////////////////////////////////////
			// WE NEED SWAP
			// AND DESTRUCTIVE MOVE CONSTRUCTOR
			// AND RESIZE
			// AND INSERT
			//
		}

		constexpr header_union(header_union& other) { }

		constexpr ~header_union() noexcept {
			if (stored_inplace()) [[likely]] {
				shortHeader.~short_header_type();
			} else {
				longHeader.~long_header_type();
			}
		}
	};

	header_union header{};

  public:
	constexpr bool stored_inplace() const noexcept {
		return header.stored_inplace();
	}

	constexpr std::size_t size() const noexcept {
		return header.size();
	}

	constexpr bool empty() const noexcept {
		return size() == 0;
	}

	constexpr std::size_t capacity() const noexcept {
		return header.capacity();
	}

	constexpr std::size_t shrink_to_fit() {
		return header.shrink_to_fit();
	}

	constexpr std::span<value_type> span() noexcept {
		return header.span();
	}

	constexpr auto extra_ref() const noexcept requires(stores_extra_data) {
		return header.extra_ref();
	}

	constexpr auto extra_ref() noexcept requires(stores_extra_data) {
		return header.extra_ref();
	}

	constexpr short_extra_or_dummy_type const & extra() const noexcept
		requires(stores_extra_data && stores_one_kind_of_extra_data) {
		return header.extra();
	}

	constexpr short_extra_or_dummy_type& extra() noexcept
		requires(stores_extra_data && stores_one_kind_of_extra_data) {
		return header.extra();
	}

	constexpr bool operator==(usually_short_vector& other) const noexcept {
		std::span<Value> valuesA = span();
		std::span<Value> valuesB = other.span();
		return std::ranges::equal(valuesA, valuesB);
	}

	constexpr bool operator!=(usually_short_vector& other) const noexcept {
		return !operator==(other);
	}

	constexpr auto operator<=>(usually_short_vector& other) const noexcept {
		std::span<Value> valuesA = span();
		std::span<Value> valuesB = other.span();
		return std::lexicographical_compare_three_way(
			valuesA.begin(), valuesB.end(), valuesB.begin(), valuesB.end()
		);
	}

	constexpr usually_short_vector() noexcept = default;

	constexpr ~usually_short_vector() noexcept = default;
};
