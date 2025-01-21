#pragma once

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <ranges>
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
	move_extra_from(with_extra_data<OtherExtraData>& otherWrapper
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
	move_extra_from(with_extra_data<OtherExtraData>& otherWrapper
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

) class usually_inplace_vector final {
  public:
	using value_type = Value;
	using reference = value_type&;
	using const_reference = value_type const &;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;
	using iterator = value_type*;
	using const_iterator = value_type const *;

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
			return headerBase.sizeOrLongHeaderMark != short_size(-1);
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

		constexpr const_iterator short_header_begin() const noexcept {
			return (const_iterator) shortHeader.storageArray.data();
		}

		constexpr iterator short_header_begin() noexcept {
			return (iterator) shortHeader.storageArray.data();
		}

		constexpr const_iterator long_header_begin() const noexcept {
			return longHeader.storageVector.data();
		}

		constexpr iterator long_header_begin() noexcept {
			return longHeader.storageVector.data();
		}

		constexpr const_iterator long_header_end() const noexcept {
			return long_header_begin() + long_header_size();
		}

		constexpr iterator long_header_end() noexcept {
			return long_header_begin() + long_header_size();
		}

		constexpr const_iterator short_header_end() const noexcept {
			return short_header_begin() + short_header_size();
		}

		constexpr iterator short_header_end() noexcept {
			return short_header_begin() + short_header_size();
		}

		constexpr std::span<value_type const>
		short_header_span() const noexcept {
			return { short_header_begin(), short_header_size() };
		}

		constexpr std::span<value_type> short_header_span() noexcept {
			return { short_header_begin(), short_header_size() };
		}

		constexpr std::span<value_type const>
		long_header_span() const noexcept {
			return { long_header_begin(), long_header_size() };
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

		constexpr const_iterator begin() const noexcept {
			if (stored_inplace()) [[likely]] {
				return short_header_begin();
			}
			return long_header_begin();
		}

		constexpr iterator begin() noexcept {
			if (stored_inplace()) [[likely]] {
				return short_header_begin();
			}
			return long_header_begin();
		}

		constexpr const_iterator end() const noexcept {
			if (stored_inplace()) [[likely]] {
				return short_header_end();
			}
			return long_header_end();
		}

		constexpr iterator end() noexcept {
			if (stored_inplace()) [[likely]] {
				return short_header_end();
			}
			return long_header_end();
		}

		constexpr std::span<value_type const> span() const noexcept {
			if (stored_inplace()) [[likely]] {
				return short_header_span();
			}
			return long_header_span();
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

					with_extra_data<short_extra_type> extraHolder;
					extraHolder.move_extra_from(longHeader);
					longHeader.~long_header_type();
					new (&shortHeader) short_header_type{};
					shortHeader.move_extra_from(extraHolder);
					shortHeader.sizeOrLongHeaderMark = oldValues.size();
					std::uninitialized_move(
						oldValues.begin(),
						oldValues.end(),
						short_header_begin()
					);
				} else {
					longHeader.storageVector.shrink_to_fit();
				}
			}
		}

		constexpr void reserve(std::size_t newCapacity) {
			if (newCapacity <= inplace_capacity) [[likely]] {
				return;
			}
			if (!stored_inplace()) {
				longHeader.storageVector.reserve(newCapacity);
				return;
			}
			// Move from inplace storage to external storage

			std::span<value_type> dataInOldLocation = short_header_span();
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
		}

		template <std::ranges::sized_range Range>
		constexpr void append_range(Range&& range) {
			auto inBegin = std::begin(range);
			auto inEnd = std::end(range);
			std::size_t inCount = std::ranges::distance(inBegin, inEnd);

			std::size_t const oldSize = size();
			std::size_t const newSize = oldSize + inCount;
			if (newSize <= inplace_capacity) [[likely]] {
				shortHeader.sizeOrLongHeaderMark = newSize;
				std::uninitialized_copy(
					inBegin, inEnd, short_header_begin() + oldSize
				);
				return;
			}
			reserve(newSize);
			// TODO: Use C++23's vector::insert_range instead when it's
			// available
			longHeader.storageVector.insert(
				longHeader.storageVector.end(), inBegin, inEnd
			);
		}

		template <class... Args>
		constexpr reference emplace_back(Args&&... args) {
			if (headerBase.sizeOrLongHeaderMark < inplace_capacity)
				[[likely]] {
				iterator locationOfNewObject = short_header_end();

				new (locationOfNewObject)
					value_type(std::forward<Args>(args)...);
				++headerBase.sizeOrLongHeaderMark;
				return *locationOfNewObject;
			}
			reserve(size() + 1);
			return longHeader.storageVector.emplace_back(
				std::forward<Args>(args)...
			);
		}

		constexpr void pop_back() noexcept {
			if (stored_inplace()) [[likely]] {
				std::size_t const oldSize = short_header_size();
				if (oldSize == 0) [[unlikely]] {
					return;
				}
				(short_header_begin() + oldSize)->~value_type();
				--shortHeader.sizeOrLongHeaderMark;
				return;
			}
			if (!longHeader.storageVector.empty()) [[likely]] {
				longHeader.storageVector.pop_back();
			}
		}

		constexpr void clear() noexcept {
			if (stored_inplace()) [[likely]] {
				std::ranges::destroy(short_header_span());
				shortHeader.sizeOrLongHeaderMark = 0;
				return;
			}
			longHeader.storageVector.clear();
		}

		constexpr void swap(header_union&& other) {
			//////////////////////////////////////////////
			// WE NEED SWAP
			// AND DESTRUCTIVE MOVE CONSTRUCTOR
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

	[[nodiscard]] constexpr bool empty() const noexcept {
		return size() == 0;
	}

	constexpr void reserve(std::size_t newCapacity) {
		return header.reserve(newCapacity);
	}

	constexpr std::size_t capacity() const noexcept {
		return header.capacity();
	}

	constexpr void shrink_to_fit() {
		return header.shrink_to_fit();
	}

	constexpr const_iterator cbegin() const noexcept {
		return header.begin();
	}

	constexpr const_iterator begin() const noexcept {
		return header.begin();
	}

	constexpr iterator begin() noexcept {
		return header.begin();
	}

	constexpr const_iterator cend() const noexcept {
		return header.end();
	}

	constexpr const_iterator end() const noexcept {
		return header.end();
	}

	constexpr iterator end() noexcept {
		return header.end();
	}

	constexpr std::span<value_type const> span() const noexcept {
		return header.span();
	}

	constexpr std::span<value_type> span() noexcept {
		return header.span();
	}

	template <std::ranges::sized_range Range>
	constexpr void append_range(Range&& range) {
		header.append_range(std::forward(range));
	}

	template <class... Args>
	constexpr reference emplace_back(Args&&... args) {
		return header.emplace_back(std::forward<Args>(args)...);
	}

	constexpr const_reference operator[](std::size_t index) const noexcept {
		return header.begin()[index];
	}

	constexpr reference operator[](std::size_t index) noexcept {
		return header.begin()[index];
	}

	constexpr const_reference front() const noexcept {
		return *header.begin();
	}

	constexpr reference front() noexcept {
		return *header.begin();
	}

	constexpr const_reference back() const noexcept {
		return *(header.end() - 1);
	}

	constexpr reference back() noexcept {
		return *(header.end() - 1);
	}

	constexpr void fill(const_reference value) noexcept {
		std::ranges::fill(span(), value);
	}

	constexpr void pop_back() noexcept {
		header.pop_back();
	}

	constexpr void clear() noexcept {
		header.clear();
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

	constexpr bool operator==(usually_inplace_vector& other
	) const noexcept {
		std::span<Value> valuesA = span();
		std::span<Value> valuesB = other.span();
		return std::ranges::equal(valuesA, valuesB);
	}

	constexpr bool operator!=(usually_inplace_vector& other
	) const noexcept {
		return !operator==(other);
	}

	constexpr auto operator<=>(usually_inplace_vector& other
	) const noexcept {
		std::span<Value> valuesA = span();
		std::span<Value> valuesB = other.span();
		return std::lexicographical_compare_three_way(
			valuesA.begin(), valuesB.end(), valuesB.begin(), valuesB.end()
		);
	}

	constexpr usually_inplace_vector() noexcept = default;

	constexpr ~usually_inplace_vector() noexcept = default;
};
