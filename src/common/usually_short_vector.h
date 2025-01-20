#pragma once

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <span>
#include <vector>

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

template <class Value, class ShortSize, class LongExtraData>
struct long_header : header_base<ShortSize> {
	using header_base<ShortSize>::sizeOrLongHeaderMark;

	constexpr long_header() {
		sizeOrLongHeaderMark = -1;
	}

	LongExtraData longExtraData;
	std::vector<Value> storageVector;
};

template <class Value, class ShortSize, std::same_as<void> LongExtraData>
// Version without longExtraData
struct long_header<Value, ShortSize, LongExtraData>
	: header_base<ShortSize> {
	using header_base<ShortSize>::sizeOrLongHeaderMark;

	constexpr long_header() {
		sizeOrLongHeaderMark = -1;
	}

	std::vector<Value> storageVector;
};

template <
	class Value,
	class ShortSize,
	class ShortExtraData,
	std::size_t InplaceCapacity>
struct short_header : header_base<ShortSize> {
	using header_base<ShortSize>::sizeOrLongHeaderMark;

	ShortExtraData shortExtraData;
	alignas(Value
	) std::array<std::byte, InplaceCapacity * sizeof(Value)> storageArray;
};

template <
	class Value,
	class ShortSize,
	std::same_as<void> ShortExtraData,
	std::size_t InplaceCapacity>
struct short_header<Value, ShortSize, ShortExtraData, InplaceCapacity>
	: header_base<ShortSize> {
	using header_base<ShortSize>::sizeOrLongHeaderMark;

	alignas(Value
	) std::array<std::byte, InplaceCapacity * sizeof(Value)> storageArray;
};

template <class Type>
constexpr bool is_void = std::is_same_v<Type, void>;

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

) class usually_short_vector {
  public:
	using value_type = Value;
	using short_extra_type = ShortExtraData;
	using long_extra_type = LongExtraData;
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

		constexpr std::span<value_type> short_header_span() noexcept {
			return { (Value*) shortHeader.storageArray.begin(),
					 shortHeader.sizeOrLongHeaderMark };
		}

		constexpr std::span<value_type> long_header_span() noexcept {
			return { (Value*) longHeader.storageVector.data(),
					 longHeader.storageVector.size() };
		}

		constexpr std::size_t capacity() const noexcept {
			if (stored_inplace()) [[likely]] {
				return inplace_capacity;
			}
			return long_header_capacity();
		}

		constexpr void shrink_to_fit() {
			if (!stored_inplace()) [[unlikely]] {
				return longHeader.storageVector.shrink_to_fit();
				/// ////// / // //  TODO:
				// If size() is <= inplace_capacity, just move things
				// inplace!!!!!!
			}
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

		constexpr header_union(header_union& other) { }

		constexpr ~header_union() noexcept {
			if (stored_inplace()) [[likely]] {
				std::ranges::destroy(short_header_span());
				shortHeader.~short_header();
			} else {
				std::ranges::destroy(long_header_span());
				longHeader.~long_header();
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

#if 0
constexpr bool
can_fit_values_in_compact_storage(std::size_t maxValuesStoredInplace) {
	// Can `MaxValuesStoredInplace` be stored in `sizeIfStoredInplace`,
	// with 1 to spare for indicating external storage?
	return std::size_t(maxValuesStoredInplace)
		<= std::size_t(std::numeric_limits<std::uint8_t>::max()) - 1;
}

constexpr void
delete_aligned_bytes(std::byte* data, std::size_t alignment) noexcept {
	::operator delete[](data, std::align_val_t(alignment));
}
template <
	std::size_t SizeofValue,
	std::size_t AlignofValue,
	std::size_t MaxValuesStoredInplace>
requires(can_fit_values_in_compact_storage(MaxValuesStoredInplace))
class compact_usually_short_vector_storage_base {
  private:
	struct values_byte_pointer_and_object_count {
		std::byte* firstByteOfValues;
		std::size_t numValues;
	};

	struct const_values_byte_pointer_and_object_count {
		std::byte const * firstByteOfValues;
		std::size_t numValues;
	};

	struct long_header {
		// We for `long_header` to be aligned with the array of values,
		// so that the position of the array of values will always be
		// `&longHeader + 1`
		alignas(std::max(alignof(std::size_t), AlignofValue)
		) std::size_t size;
		std::size_t capacity;

		long_header(long_header const &) = delete;
		long_header& operator=(long_header const &) = delete;

		constexpr std::byte const * value_bytes_start() const noexcept {
			return (std::byte const *) (this + 1);
		}

		constexpr std::byte* value_bytes_start() noexcept {
			return (std::byte*) (this + 1);
		}

		constexpr values_byte_pointer_and_object_count values() noexcept {
			return { .firstByteOfValues = value_bytes_start(),
					 .numValues = size };
		}

		constexpr const_values_byte_pointer_and_object_count
		values() const noexcept {
			return { .firstByteOfValues = value_bytes_start(),
					 .numValues = size };
		}
	};

	using pointer_to_long_header = long_header*;
	constexpr std::size_t
		extra_padding_for_guaranteeing_alignment_of_pointer_to_long_header
		= alignof(pointer_to_long_header) <= AlignofValue
		? 0
		: alignof(pointer_to_long_header) - AlignofValue;

	constexpr std::size_t total_storage_byte_size_for_long_header_pointer
		= sizeof(pointer_to_long_header)
		+ extra_padding_for_guaranteeing_alignment_of_pointer_to_long_header;

	constexpr std::uint8_t inplace_storage_byte_size = std::max(
		MaxValuesStoredInplace * SizeofValue,
		total_storage_byte_size_for_long_header_pointer
	);

  public:
	// We might actually use a bigger inplace capacity than requested
	// through MaxValuesStoredInplace, if SizeofValue *
	// MaxValuesStoredInplace is less than the number of bytes we always
	// need for a pointer to external storage
	constexpr std::uint8_t inplace_capacity = std::min(
		inplace_storage_byte_size / SizeofValue,
		max_max_size_for_compact_storage
	);

  private:
	constexpr std::uint8_t not_stored_inplace = -1;

	std::uint8_t sizeIfStoredInplace{ 0 };
	alignas(AlignofValue
	) std::array<std::byte, inplace_storage_byte_size> inplaceStorage;

	constexpr bool is_inplace() const noexcept {
		return sizeIfStoredInplace != not_stored_inplace;
	}

	constexpr long_header const * const *
	get_pointer_to_pointer_to_long_header() const noexcept {
		// TODO: Use std::align here instead, when a future C++ standard
		// has made it constexpr

		if constexpr (extra_padding_for_guaranteeing_alignment_of_pointer_to_long_header
					  == 0) {
			return (long_header const *) inplaceStorage.data();
		}

		std::byte const * ptr = inplaceStorage.data();
		std::size_t const alignmentDifference = std::uintptr_t(ptr)
			% AlignofValue;
		std::byte const * ptrRealigned = ptr + AlignofValue
			- alignmentDifference;
		bool const needsRealignment = alignmentDifference != 1;
		return (long_header const * const *) (needsRealignment
												  ? ptrRealigned
												  : ptr);
	}

	constexpr long_header**
	get_pointer_to_pointer_to_long_header() noexcept {
		return const_cast<long_header**>(
			((compact_usually_short_vector_storage_base const *) this)
				->get_pointer_to_pointer_to_long_header()
		);
	}

	// Precondition: `is_inplace() == false`
	constexpr long_header const & get_long_header() const noexcept {
		return **get_pointer_to_pointer_to_long_header();
	}

	// Precondition: `is_inplace() == false`
	constexpr long_header& get_long_header() noexcept {
		return **get_pointer_to_pointer_to_long_header();
	}

	// Precondition: `is_inplace() == true`
	constexpr std::byte const * inplace_value_bytes_start() const noexcept {
		return inplaceStorage.data();
	}

	// Precondition: `is_inplace() == true`
	constexpr std::byte* inplace_value_bytes_start() noexcept {
		return inplaceStorage.data();
	}

	// Precondition: `is_inplace() == true`
	constexpr values_byte_pointer_and_object_count
	inplace_values() noexcept {
		return { .firstByteOfValues = inplace_value_bytes_start(),
				 .numValues = size };
	}

	// Precondition: `is_inplace() == true`
	constexpr const_values_byte_pointer_and_object_count
	inplace_values() const noexcept {
		return { .firstByteOfValues = inplace_value_bytes_start(),
				 .numValues = size };
	}

  protected:
	constexpr std::byte const * value_bytes_start() const noexcept {
		if (is_inplace()) [[likely]] {
			return inplace_value_bytes_start();
		}
		return get_long_header().value_bytes_start();
	}

	constexpr std::byte* value_bytes_start() noexcept {
		if (is_inplace()) [[likely]] {
			return inplace_value_bytes_start();
		}
		return get_long_header().value_bytes_start();
	}

	constexpr values_byte_pointer_and_object_count values() noexcept {
		if (is_inplace()) [[likely]] {
			return inplace_values();
		}
		return get_long_header().values();
	}

	constexpr const_values_byte_pointer_and_object_count
	values() const noexcept {
		if (is_inplace()) [[likely]] {
			return inplace_values();
		}
		return get_long_header().values();
	}

	// Precondition: `newCapacity >= capacity()`
	constexpr struct {
		bool noChange{ true };

		bool wasInPlace{};
		values_byte_pointer_and_object_count valuesInOldLocation;
		long_header* newLongHeaderPointer{};
		long_header** ptrPtrLongHeader{};

	} start_to_increase_capacity(std::size_t newCapacity) {
		// TODO: Check that (sizeof(long_header) + newCapacity *
		// SizeofValue) is not too big for std::size_t

		bool const willBeInplace = newCapacity <= inplace_capacity;

		if (willBeInplace) [[likely]] {
			return {};
		}

		pointer_to_long_header** ptrPtrLongHeader{
			get_pointer_to_pointer_to_long_header()
		};
		values_byte_pointer_and_object_count valuesInOldLocation{ values(
		) };
		bool const currentlyInplace = is_inplace();
		if (!currentlyInplace) {
			if (newCapacity == (*ptrPtrLongHeader)->capacity) [[unlikely]] {
				return {};
			}
			sizeIfStoredInplace = not_stored_inplace;
		}
		std::byte* newBuffer = new (std::align_val_t(alignof(long_header)))
			std::byte[sizeof(long_header) + newCapacity * SizeofValue];
		long_header* newLongHeaderPointer = (new (newBuffer) long_header);

		newLongHeaderPointer->size = valuesInOldLocation.numObjects;
		newLongHeaderPointer->capacity = newCapacity;
		return {
			.noChange = false, .wasInPlace = currentlyInplace,
			.valuesInOldLocation = valuesInOldLocation,
			.newLongHeaderPointer = newLongHeaderPointer,
			.ptrPtrLongHeader = ptrPtrLongHeader
		}
	}

	// Precondition: `newCapacity <= capacity()`
	constexpr struct {
		bool noChange{ true };

		long_header* newLongHeaderPointer{};
		long_header* firstValueInNewLocation;
		long_header** ptrPtrLongHeader{};

	} start_to_decrease_capacity(std::size_t newCapacity) {
		// TODO: Check that (sizeof(long_header) + newCapacity *
		// SizeofValue) is not too big for std::size_t

		if (is_inplace()) [[likely]] {
			return {};
		}

		bool const willBeInplace = newCapacity <= inplace_capacity;

		pointer_to_long_header** ptrPtrLongHeader{
			get_pointer_to_pointer_to_long_header()
		};
		long_header const * oldLongHeader& = **ptrPtrLongHeader;
		if (newCapacity == oldLongHeader.capacity) [[unlikely]] {
			return {};
		}
		long_header* newLongHeaderPointer{};
		std::byte* firstValueInNewLocation;
		if (willBeInplace) {
			sizeIfStoredInplace = oldLongHeader.size;
			firstValueInNewLocation = inplaceStorage.data();
		} else {
			sizeIfStoredInplace = not_stored_inplace;

			std::byte* newBuffer = new (std::align_val_t(alignof(long_header
			))) std::byte[sizeof(long_header) + newCapacity * SizeofValue];
			newLongHeaderPointer = (new (newBuffer) long_header);
			newLongHeaderPointer->size = oldLongHeader.size;
			newLongHeaderPointer->capacity = newCapacity;
			firstValueInNewLocation
				= newLongHeaderPointer->value_bytes_start();
		}

		return {
			.noChange = false, .newLongHeaderPointer = newLongHeaderPointer,
			.firstValueInNewLocation = firstValueInNewLocation,
			.ptrPtrLongHeader = ptrPtrLongHeader
		}
	}

	// Precondition: `newSize <= size()`
	// Returns: Values that need to be destroyed
	constexpr [[nodiscard]] values_byte_pointer_and_object_count
	start_to_shrink_size(std::size_t newSize) {
		if (is_inplace()) [[likely]] {
			values_byte_pointer_and_object_count const allValuesBefore
				= inplace_values();
			sizeIfStoredInplace = newSize;
			return { .firstByteOfValues = allValuesBefore.firstByteOfValues
						 + newSize * SizeofValue,
					 .numValues = allValuesBefore.oldSize - newSize };
		}
		long_header& header{ get_long_header() };
		std::size_t oldSize = header.size;
		header.size = newSize;
		return { .firstByteOfValues = allValuesBefore.firstByteOfValues
					 + newSize * SizeofValue,
				 .numValues = allValuesBefore.oldSize - newSize };
	}

	// Precondition: `newSize >= size() && newSize >= capacity()`
	// Returns: Objects to initialize
	constexpr [[nodiscard]] values_byte_pointer_and_object_count
	start_to_grow_size(std::size_t newSize) {
		if (is_inplace()) [[likely]] {
			values_byte_pointer_and_object_count const allValuesBefore
				= inplace_values();
			sizeIfStoredInplace = newSize;

			return { .firstByteOfValues = allValuesBefore.firstByteOfValues
						 + allValuesBefore.numValues * SizeofValue,
					 .numValues = newSize - allValuesBefore.numValues };
		}
		long_header& header{ get_long_header() };
		values_byte_pointer_and_object_count const oldSpan = header.values(
		);
		header.size = newSize;
		return { .firstByteOfValues = allValuesBefore.firstByteOfValues
					 + allValuesBefore.numValues * SizeofValue,
				 .numValues = newSize - allValuesBefore.numValues };
	}

  public:
	constexpr std::size_t capacity() const noexcept {
		if (is_inplace()) [[likely]] {
			return inplace_capacity;
		}
		return get_long_header().capacity;
	}

	constexpr std::size_t size() const noexcept {
		if (is_inplace()) [[likely]] {
			return sizeIfStoredInplace;
		}
		return get_long_header().size;
	}

	// Precondition: `newSize >= size() && newSize >= capacity()`
	// Returns: Values that need to be initialized (for example through
	// placement new)
	template <class Value>
	constexpr [[nodiscard]] std::span<Value> grow_size(std::size_t newSize
	) {
		values_byte_pointer_and_object_count const valuesToInitialize
			= start_to_grow_size();
		return { (Value*) valuesToInitialize.firstByteOfValues,
				 valuesToInitialize.numValues };
	}

	// Precondition: `newSize <= size()`
	template <Value>
	constexpr void shrink_size(std::size_t newSize) {
		values_byte_pointer_and_object_count const valuesToDestroy
			= start_to_shrink_size(newSize);

		std::destroy_n(
			(Value*) valuesToDestroy.firstByteOfValues,
			valuesToDestroy.numValues
		);
	}

	struct {
		bool noChange{ true };

		bool wasInPlace{};
		values_byte_pointer_and_object_count valuesInOldLocation;
		long_header* newLongHeaderPointer{};
		long_header** ptrPtrLongHeader{};

	}

	// Precondition: `newCapacity <= capacity()`
	template <class Value>
	constexpr void decrease_capacity(std::size_t newCapacity) {
		auto const changeInfo = start_to_decrease_capacity(newCapacity);
		if (changeInfo.noChange) [[likely]] {
			return;
		}

		pointer_to_long_header** ptrPtrLongHeader
			= changeInfo.ptrPtrLongHeader;
		long_header& oldLongHeader = **ptrPtrLongHeader;
		auto oldValues = oldLongHeader.values();

		std::uninitialized_move_n(
			(Value*) oldValues.firstByteOfValues,
			oldValues.numValues(Value*) changeInfo.firstValueInNewLocation,
		);
		std::destroy_n(
			(Value*) oldValues.firstByteOfValues, oldValues.numValues
		);

		delete_aligned_bytes(
			(std::byte*) &oldLongHeader, alignof(long_header)
		);

		if (changeInfo.newLongHeaderPointer) {
			*changeInfo.ptrPtrLongHeader = changeInfo.newLongHeaderPointer;
		}
	}

	// Precondition: `newCapacity >= capacity()`
	template <class Value>
	constexpr void increase_capacity(std::size_t newCapacity) {
		auto const changeInfo = start_to_increase_capacity(newCapacity);
		if (changeInfo.noChange) [[likely]] {
			return;
		}

		Value* firstValueInOldLocation
			= (Value*) changeInfo.valuesInOldLocation.firstByteOfValues;
		Value* firstValueInNewLocation
			= (Value*) changeInfo.firstValueInNewLocation;
		std::size_t numValues = changeInfo.valuesInOldLocation.numValues;

		std::uninitialized_move_n(
			firstValueInOldLocation, numValues, firstValueInNewLocation
		);
		std::destroy_n(firstValueInOldLocation, numValues);

		if (changeInfo.newLongHeaderPointer) {
			if (changeInfo.wasInPlace) {
				delete_aligned_bytes(
					(std::byte*) *changeInfo.ptrPtrLongHeader,
					alignof(long_header)
				);
			}
			*changeInfo.ptrPtrLongHeader = changeInfo.newLongHeaderPointer;
		}
	}

	// Precondition: `newCapacity >= size()`
	template <Value>
	constexpr void change_capacity(std::size_t newCapacity) {
		auto const changeInfo = start_to_change_capacity(newCapacity);
		if (changeInfo.noChange) [[likely]] {
			return;
		}

		std::uninitialized_move_n(
			(Value*) changeInfo.valuesInOldLocation.firstByteOfValues,
			changeInfo.numElementsToMoveOver.valuesInOldLocation.numValues,
			(Value*) changeInfo.firstValueInNewLocation
		);
		std::destroy_n(
			(Value*) changeInfo.valuesInOldLocation.firstByteOfValues,
			changeInfo.numElementsToMoveOver.valuesInOldLocation.numValues
		);

		if (changeInfo.newLongHeaderPointer != nullptr) {
			*changeInfo.ptrPtrLongHeader = changeInfo.newLongHeaderPointer;
		}

		if (changeInfo.oldLongHeaderPtr != nullptr) {
			delete_aligned_bytes(
				changeInfo.oldLongHeaderPtr, alignof(long_header)
			);
		}
	}
};

template <class Value, std::size_t MaxValuesStoredInplace>
requires(can_fit_values_in_compact_storage(MaxValuesStoredInplace))
class usually_short_vector : compact_usually_short_vector_storage_base<
								 sizeof(Value),
								 alignof(Value),
								 MaxValuesStoredInplace>
								 final {
  public:
	constexpr ~usually_short_vector() noexcept {
		shrink_size<Value>(0);
		change_capacity<Value>(0);
	}

	// Precondition: `newSize >= size() && newSize >= capacity()`
	// Returns: Values that need to be initialized (for example through
	// placement new)
	template <class Value>
	constexpr [[nodiscard]] std::span<Value> grow_size(std::size_t newSize
	) {
		values_byte_pointer_and_object_count const valuesToInitialize
			= start_to_grow_size();
		return { (Value*) valuesToInitialize.firstByteOfValues,
				 valuesToInitialize.numValues };
	}

	// Precondition: `newSize <= size()`
	template <Value>
	constexpr void shrink_size(std::size_t newSize) {
		values_byte_pointer_and_object_count const valuesToDestroy
			= start_to_shrink_size(newSize);

		std::destroy_n(
			(Value*) valuesToDestroy.firstByteOfValues,
			valuesToDestroy.numValues
		);
	}

	struct {
		bool noChange{ true };

		bool wasInPlace{};
		values_byte_pointer_and_object_count valuesInOldLocation;
		long_header* newLongHeaderPointer{};
		long_header** ptrPtrLongHeader{};

	}

	// Precondition: `newCapacity <= capacity()`
	constexpr void
	decrease_capacity(std::size_t newCapacity) {
		auto const changeInfo = start_to_decrease_capacity(newCapacity);
		if (changeInfo.noChange) [[likely]] {
			return;
		}

		pointer_to_long_header** ptrPtrLongHeader
			= changeInfo.ptrPtrLongHeader;
		long_header& oldLongHeader = **ptrPtrLongHeader;
		auto oldValues = oldLongHeader.values();

		std::uninitialized_move_n(
			(Value*) oldValues.firstByteOfValues,
			oldValues.numValues(Value*) changeInfo.firstValueInNewLocation,
		);
		std::destroy_n(
			(Value*) oldValues.firstByteOfValues, oldValues.numValues
		);

		delete_aligned_bytes(
			(std::byte*) &oldLongHeader, alignof(long_header)
		);

		if (changeInfo.newLongHeaderPointer) {
			*changeInfo.ptrPtrLongHeader = changeInfo.newLongHeaderPointer;
		}
	}

	// Precondition: `newCapacity >= capacity()`
	constexpr void increase_capacity(std::size_t newCapacity) {
		auto const changeInfo = start_to_increase_capacity(newCapacity);
		if (changeInfo.noChange) [[likely]] {
			return;
		}

		Value* firstValueInOldLocation
			= (Value*) changeInfo.valuesInOldLocation.firstByteOfValues;
		Value* firstValueInNewLocation
			= (Value*) changeInfo.firstValueInNewLocation;
		std::size_t numValues = changeInfo.valuesInOldLocation.numValues;

		std::uninitialized_move_n(
			firstValueInOldLocation, numValues, firstValueInNewLocation
		);
		std::destroy_n(firstValueInOldLocation, numValues);

		if (changeInfo.newLongHeaderPointer) {
			if (changeInfo.wasInPlace) {
				delete_aligned_bytes(
					(std::byte*) *changeInfo.ptrPtrLongHeader,
					alignof(long_header)
				);
			}
			*changeInfo.ptrPtrLongHeader = changeInfo.newLongHeaderPointer;
		}
	}

	// Precondition: `newCapacity >= size()`
	constexpr void change_capacity(std::size_t newCapacity) {
		auto const changeInfo = start_to_change_capacity(newCapacity);
		if (changeInfo.noChange) [[likely]] {
			return;
		}

		std::uninitialized_move_n(
			(Value*) changeInfo.valuesInOldLocation.firstByteOfValues,
			changeInfo.numElementsToMoveOver.valuesInOldLocation.numValues,
			(Value*) changeInfo.firstValueInNewLocation
		);
		std::destroy_n(
			(Value*) changeInfo.valuesInOldLocation.firstByteOfValues,
			changeInfo.numElementsToMoveOver.valuesInOldLocation.numValues
		);

		if (changeInfo.newLongHeaderPointer != nullptr) {
			*changeInfo.ptrPtrLongHeader = changeInfo.newLongHeaderPointer;
		}

		if (changeInfo.oldLongHeaderPtr != nullptr) {
			delete_aligned_bytes(
				changeInfo.oldLongHeaderPtr, alignof(long_header)
			);
		}
	}

	constexpr Value const * begin() const noexcept {
		return (Value*) value_bytes_start();
	}

	constexpr Value* begin() noexcept {
		return (Value*) value_bytes_start();
	}

	constexpr Value const * end() const noexcept {
		const_values_byte_pointer_and_object_count bpaoc = values();
		return ((Value*) bpaoc.firstByteOfValues) + bpaoc.numObjects;
	}

	constexpr Value* end() noexcept {
		values_byte_pointer_and_object_count bpaoc = values();
		return ((Value*) bpaoc.firstByteOfValues) + bpaoc.numObjects;
	}

	constexpr std::span<Value const> span() const noexcept {
		const_values_byte_pointer_and_object_count bpaoc = values();
		return { (Value*) bpaoc.firstByteOfValues, bpaoc.numObjects };
	}

	constexpr std::span<Value> span() noexcept {
		values_byte_pointer_and_object_count bpaoc = values();
		return { (Value*) bpaoc.firstByteOfValues, bpaoc.numObjects };
	}

	constexpr void clear() noexcept {
		shrink_size<Value>(0);
	}

	constexpr void shrink_to_fit() {
		change_capacity<Value>(size());
	}

	constexpr void swap(usually_short_vector_storage& other) {
		bool aInplace = is_inplace();
		bool bInplace = is_inplace();

		if (!aInplace && bInplace) [[unlikely]] {
			swap(other, *this);
			return;
		}

		if (aInplace && bInplace) [[likely]] {
			std::span<Value> aSpan = a.span();
			std::span<Value> bSpan = b.span();
			if (aSpan.size() > bSpan) {
				using std::swap;
				swap(aSpan, bSpan);
			}
			for (std::size_t i = 0; i != aSpan.size(); ++i) {
				using std::swap;
				swap(aSpan[i], bSpan[i]);
			}

			// First we simply swap up to min(a.size(), b.size()).
			// Then we uninitialized_move() from larger to smaller, and
			// destroy the old locations.
			// Then we swap the sizes.
		} else if (!aInplace && !bInplace) {
			// First we swap the pointers - while making sure they are
			// aligned. Then we swap the sizes
		} else { // aInplace is true here, and bInplace is false
				 // First we get b.pointer
				 // Then we move all the values from a to the
				 // uninitialized storage in b. Then we destroy a.size()
				 // values in
				 // a.inplaceStorage. Then we assign the pointer to a -
				 // in the correct aligned position. Then we swap the
				 // sizes
		}
	}

	//	WE SHOULD ALSO HAVE A DESTRUCTIVE MOVE FUNCTION !

	//	AND A COPY FUNCTION
	//	? ?

	//	  We may be able to support copying between different inspace
	//		  storage sizes
	//	? ?
};

#endif
