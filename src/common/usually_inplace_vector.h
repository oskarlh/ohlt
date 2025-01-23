#pragma once

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <vector>

template <std::size_t NumElements>
using inplace_size_type_for_num_elements = std::conditional_t<
	(NumElements <= std::numeric_limits<std::uint8_t>::max() - 1),
	std::uint8_t,
	std::conditional_t<
		(NumElements <= std::numeric_limits<std::uint16_t>::max() - 1),
		std::uint16_t,
		std::size_t>>;

template <class InplaceSizeType>
struct common_to_both_storage_types {
	using inplace_size_type = InplaceSizeType;

	inplace_size_type sizeOrExternalStorageMark; // -1 means this is an
												 // external_storage object
};

template <class Value, class InplaceSizeType>
struct external_storage final {
	using inplace_size_type = InplaceSizeType;

	inplace_size_type sizeOrExternalStorageMark
		= -1; // Always -1 for external_storage

	using value_type = Value;

	std::size_t elementCount = 0;
	std::byte* bytes = nullptr;
	std::size_t elementCapacity = 0;

	constexpr void swap(external_storage& other) noexcept {
		using std::swap;
		swap(elementCount, other.elementCount);
		swap(bytes, other.bytes);
		swap(elementCapacity, other.elementCapacity);
	}

	constexpr external_storage() noexcept {};
	constexpr external_storage(std::size_t initialCapacity) :
		bytes(new(std::align_val_t(alignof(value_type)))
				  std::byte[initialCapacity * sizeof(value_type)]),
		elementCapacity(initialCapacity) {};

	constexpr std::size_t size() const noexcept {
		return elementCount;
	}

	constexpr std::size_t capacity() const noexcept {
		return elementCapacity;
	}

	constexpr value_type const * begin() const noexcept {
		return (value_type const *) bytes;
	}

	constexpr value_type* begin() noexcept {
		return (value_type*) bytes;
	}

	constexpr value_type const * end() const noexcept {
		return begin() + size();
	}

	constexpr value_type* end() noexcept {
		return begin() + size();
	}

	constexpr std::span<value_type const> span() const noexcept {
		return { begin(), size() };
	}

	constexpr std::span<value_type> span() noexcept {
		return { begin(), size() };
	}

	constexpr void reserve(std::size_t newCapacity) {
		if (newCapacity <= elementCapacity) [[unlikely]] {
			return;
		}
		external_storage withHigherCapacity{ newCapacity };
		withHigherCapacity.append_range(span() | std::views::as_rvalue);
		swap(withHigherCapacity);
	}

	constexpr void shrink_to_fit() {
		if (size() != capacity()) {
			external_storage withLowerCapacity{ size() };
			withLowerCapacity.append_range(span() | std::views::as_rvalue);
			swap(withLowerCapacity);
		}
	}

	constexpr external_storage(external_storage&& other) noexcept {
		swap(other);
	}

	constexpr ~external_storage() {
		std::destroy_n(begin(), size());
		::operator delete[](begin(), std::align_val_t(alignof(value_type)));
	}

	constexpr void append_range(std::ranges::sized_range auto&& range) {
		auto rangeBegin = std::ranges::begin(range);
		std::size_t rangeSize = std::ranges::size(range);
		reserve(
			size() + rangeSize
		); // TODO: Increase it more to avoid many small allocations
		std::uninitialized_copy_n(rangeBegin, rangeSize, end());
		elementCount += rangeSize;
	}

	template <class... Args>
	constexpr value_type& emplace_back(Args&&... args) {
		reserve(
			size() + 1
		); // TODO: Increase it more to avoid many small allocations
		value_type* locationOfNewObject = end();
		new (locationOfNewObject) value_type(std::forward<Args>(args)...);
		++elementCount;
		return *locationOfNewObject;
	}

	constexpr external_storage(external_storage const & other) {
		append_range(other.span());
	}

	constexpr void reduce_size_to(std::size_t newSize) noexcept {
		if (newSize < size()) [[unlikely]] {
			return;
		}

		std::destroy(begin() + newSize, end());
		elementCount = newSize;
	}

	constexpr void clear() noexcept {
		reduce_size_to(0);
	}

	// Precondition: size() != 0
	constexpr void pop_back() noexcept {
		begin()->~value_type();
		--elementCount;
	}

	external_storage& operator=(external_storage const & other) {
		clear();
		append_range(other.span());
	}

	constexpr external_storage& operator=(external_storage&& other
	) noexcept {
		swap(other);
	}
};

template <class Value, class InplaceSizeType, std::size_t InplaceCapacity>
struct inplace_storage final {
	using inplace_size_type = InplaceSizeType;

	inplace_size_type sizeOrExternalStorageMark = 0;

	using value_type = Value;

	alignas(value_type
	) std::array<std::byte, InplaceCapacity * sizeof(value_type)> bytes;

	constexpr std::size_t size() const noexcept {
		return sizeOrExternalStorageMark;
	}

	constexpr value_type const * begin() const noexcept {
		return (value_type const *) bytes.data();
	}

	constexpr value_type* begin() noexcept {
		return (value_type*) bytes.data();
	}

	constexpr value_type const * end() const noexcept {
		return begin() + size();
	}

	constexpr value_type* end() noexcept {
		return begin() + size();
	}

	constexpr std::span<value_type const> span() const noexcept {
		return { begin(), size() };
	}

	constexpr std::span<value_type> span() noexcept {
		return { begin(), size() };
	}

	constexpr void swap(inplace_storage& other) noexcept {
		swap_values<value_type>(
			bytes.data(), size(), other.bytes.data(), other.size()
		);
		using std::swap;
		swap(sizeOrExternalStorageMark, other.sizeOrExternalStorageMark);
	}

	constexpr inplace_storage() noexcept { }

	constexpr void append_range(std::ranges::sized_range auto&& range) {
		auto rangeBegin = std::ranges::begin(range);
		std::size_t rangeSize = std::ranges::size(range);
		std::uninitialized_copy_n(rangeBegin, rangeSize, end());
		sizeOrExternalStorageMark += rangeSize;
	}

	// Precondtion: size() < InplaceCapacity
	template <class... Args>
	constexpr value_type& emplace_back(Args&&... args) {
		value_type* locationOfNewObject = end();
		new (locationOfNewObject) value_type(std::forward<Args>(args)...);
		++sizeOrExternalStorageMark;
		return *locationOfNewObject;
	}

	constexpr inplace_storage(inplace_storage&& other) noexcept {
		move_to_uninitialized_then_destroy(
			other.begin(), other.size(), bytes.start()
		);
		sizeOrExternalStorageMark = other.sizeOrExternalStorageMark;
		other.sizeOrExternalStorageMark = 0;
	}

	constexpr inplace_storage(inplace_storage const & other) {
		append_range(other);
	}

	constexpr void reduce_size_to(std::size_t newSize) noexcept {
		if (newSize < size()) [[unlikely]] {
			return;
		}

		std::destroy(begin() + newSize, end());
		sizeOrExternalStorageMark = newSize;
	}

	constexpr void clear() {
		reduce_size_to(0);
	}

	// Precondition: size() != 0
	constexpr void pop_back() noexcept {
		begin()->~value_type();
		--sizeOrExternalStorageMark;
	}

	constexpr inplace_storage& operator=(inplace_storage const & other) {
		clear();
		append_range(other.span());
	}

	constexpr inplace_storage& operator=(inplace_storage&& other) {
		clear();
		std::uninitialized_move_n(other.begin(), other.size(), begin());
		sizeOrExternalStorageMark = other.size();
		other.clear();
	}

	constexpr ~inplace_storage() {
		std::destroy_n(begin(), size());
	}
};

// Note: requires manually updating the size integers afterwards
template <class Value>
void move_to_uninitialized_then_destroy(
	Value* source, std::size_t numValues, std::byte* destination
) {
	std::uninitialized_move_n(source, numValues, (Value*) destination);
	std::destroy_n(source, numValues);
}

// Note: requires manually swapping the size integers afterwards
template <class Value>
void swap_values(
	std::byte* storageA,
	std::size_t numValuesA,
	std::byte* storageB,
	std::size_t numValuesB
) {
	if (numValuesA > numValuesB) {
		swap_values<Value>(storageB, numValuesB, storageA, numValuesA);
		return;
	}
	Value* beginA = (Value*) storageA;
	Value* oldEndA = (Value*) storageA + numValuesA;
	Value* newEndA = (Value*) storageA + numValuesB;

	Value* beginB = (Value*) storageB;
	Value* oldEndB = (Value*) storageB + numValuesB;
	Value* newEndB = (Value*) storageB + numValuesA;
	std::swap_ranges(beginA, oldEndA, beginB);
	std::uninitialized_move(newEndB, oldEndB, oldEndA);
	std::destroy(newEndB, oldEndB);
}

template <class Value, std::size_t InplaceCapacity>
requires(
	std::is_nothrow_destructible_v<Value>
	&& std::is_nothrow_move_assignable_v<Value>
	&& std::is_nothrow_move_constructible_v<Value>
	&& std::is_nothrow_swappable_v<Value>

) class usually_inplace_vector final {
  public:
	using value_type = Value;
	using reference = value_type&;
	using const_reference = value_type const &;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;
	using iterator = value_type*;
	using const_iterator = value_type const *;
	// TODO: Round up
	static constexpr std::size_t inplace_capacity = InplaceCapacity;

  private:
	using inplace_size_type
		= inplace_size_type_for_num_elements<InplaceCapacity>;

	using inplace_storage_type
		= inplace_storage<value_type, inplace_size_type, inplace_capacity>;
	using external_storage_type
		= external_storage<value_type, inplace_size_type>;
	using common_to_both_storage_types_type
		= common_to_both_storage_types<inplace_size_type>;

	union storage_union {
		inplace_storage_type inplaceStorage;
		external_storage_type externalStorage;
		common_to_both_storage_types_type commonToBothStorageTypes;

		constexpr bool stored_inplace() const noexcept {
			return commonToBothStorageTypes.sizeOrExternalStorageMark
				!= inplace_size_type(-1);
		}

		constexpr storage_union() noexcept : inplaceStorage{} { }

		constexpr storage_union(storage_union const & other) {
			if (other.stored_inplace()) [[likely]] {
				new (&inplaceStorage)
					inplace_storage_type{ other.inplaceStorage };
			} else {
				new (&externalStorage)
					external_storage_type{ other.externalStorage };
			}
		}

		constexpr storage_union(storage_union&& other) noexcept {
			if (other.stored_inplace()) [[likely]] {
				new (&inplaceStorage)
					inplace_storage_type{ std::move(other.inplaceStorage) };
			} else {
				new (&externalStorage
				) external_storage_type{ std::move(other.externalStorage) };
			}
		}

		constexpr ~storage_union() noexcept {
			if (stored_inplace()) [[likely]] {
				inplaceStorage.~inplace_storage_type();
			} else {
				externalStorage.~external_storage_type();
			}
		}

		constexpr void reserve(std::size_t newCapacity) {
			if (newCapacity <= inplace_capacity) [[likely]] {
				return;
			}

			if (stored_inplace()) {
				external_storage_type ls{ newCapacity };
				ls.append_range(
					inplaceStorage.span() | std::views::as_rvalue
				);
				inplaceStorage.~inplace_storage_type();
				new (&externalStorage) external_storage_type{};
				return;
			}
			externalStorage.reserve(newCapacity);
		}

		constexpr void shrink_to_fit() {
			if (stored_inplace()) [[likely]] {
				return;
			}
			std::size_t newCapacity = externalStorage.size();
			if (newCapacity <= inplace_capacity) {
				external_storage_type ls{ std::move(externalStorage) };
				externalStorage.~external_storage_type();
				new (&inplaceStorage) inplace_storage_type{};
				inplaceStorage.append_range(
					ls.span() | std::views::as_rvalue
				);
				return;
			}
			externalStorage.shrink_to_fit();
		}

		constexpr void swap(storage_union& other) noexcept {
			bool const inplaceA = stored_inplace();
			bool const inplaceB = other.stored_inplace();
			if (inplaceA && inplaceB) [[likely]] {
				inplaceStorage.swap(other.inplaceStorage);
				return;
			}
			if (inplaceB) {
				other.swap(*this);
				return;
			}
			if (inplaceA) {
				external_storage_type ls{ std::move(other.externalStorage
				) };

				other.externalStorage.~external_storage_type();
				new (&other.inplaceStorage)
					inplace_storage_type{ std::move(inplaceStorage) };

				inplaceStorage.~internal_storage_type();
				new (&externalStorage)
					external_storage_type{ std::move(ls) };
				return;
			}
			externalStorage.swap(other.externalStorage);
		}
	};

	storage_union storage;

  public:
	constexpr bool stored_inplace() const noexcept {
		return storage.stored_inplace();
	}

	constexpr std::size_t size() const noexcept {
		if (stored_inplace()) [[likely]] {
			return storage.inplaceStorage.size();
		}
		return storage.externalStorage.size();
	}

	[[nodiscard]] constexpr bool empty() const noexcept {
		return size() == 0;
	}

	constexpr void clear() const noexcept {
		if (stored_inplace()) [[likely]] {
			storage.inplaceStorage.clear();
			return;
		}
		storage.externalStorage.clear();
	}

	constexpr void reserve(std::size_t newCapacity) {
		return storage.reserve(newCapacity);
	}

	constexpr std::size_t capacity() const noexcept {
		if (stored_inplace()) [[likely]] {
			return InplaceCapacity;
		}
		return storage.externalStorage.capacity();
	}

	constexpr void shrink_to_fit() {
		return storage.shrink_to_fit();
	}

	constexpr const_iterator begin() const noexcept {
		if (stored_inplace()) [[likely]] {
			return storage.inplaceStorage.begin();
		}
		return storage.externalStorage.begin();
	}

	constexpr iterator begin() noexcept {
		if (stored_inplace()) [[likely]] {
			return storage.inplaceStorage.begin();
		}
		return storage.externalStorage.begin();
	}

	constexpr const_iterator cbegin() const noexcept {
		return begin();
	}

	constexpr const_iterator end() const noexcept {
		if (stored_inplace()) [[likely]] {
			return storage.inplaceStorage.end();
		}
		return storage.externalStorage.end();
	}

	constexpr iterator end() noexcept {
		if (stored_inplace()) [[likely]] {
			return storage.inplaceStorage.end();
		}
		return storage.externalStorage.end();
	}

	constexpr const_iterator cend() const noexcept {
		return end();
	}

	constexpr std::span<value_type const> span() const noexcept {
		if (stored_inplace()) [[likely]] {
			return storage.inplaceStorage.span();
		}
		return storage.externalStorage.span();
	}

	constexpr std::span<value_type> span() noexcept {
		if (stored_inplace()) [[likely]] {
			return storage.inplaceStorage.span();
		}
		return storage.externalStorage.span();
	}

	constexpr void append_range(std::ranges::sized_range auto&& range) {
		if (stored_inplace()) [[likely]] {
			storage.inplaceStorage.append_range(std::forward(range));
			return;
		}
		storage.externalStorage.append_range(std::forward(range));
	}

	template <class... Args>
	constexpr reference emplace_back(Args&&... args) {
		if (stored_inplace()) [[likely]] {
			return storage.inplaceStorage.emplace_back(
				std::forward<Args>(args)...
			);
		}
		return storage.externalStorage.emplace_back(std::forward<Args>(args
		)...);
	}

	constexpr const_reference operator[](std::size_t index) const noexcept {
		return begin()[index];
	}

	constexpr reference operator[](std::size_t index) noexcept {
		return begin()[index];
	}

	constexpr const_reference front() const noexcept {
		return *begin();
	}

	constexpr reference front() noexcept {
		return *begin();
	}

	constexpr const_reference back() const noexcept {
		return *(end() - 1);
	}

	constexpr reference back() noexcept {
		return *(end() - 1);
	}

	constexpr void fill(const_reference value) noexcept {
		std::ranges::fill(span(), value);
	}

	constexpr void pop_back() noexcept {
		if (stored_inplace()) [[likely]] {
			storage.inplaceStorage.pop_back();
			return;
		}
		storage.externalStorage.pop_back();
	}

	constexpr void clear() noexcept {
		if (stored_inplace()) [[likely]] {
			storage.inplaceStorage.clear();
			return;
		}
		storage.externalStorage.clear();
	}

	constexpr void reduce_size_to(std::size_t newSize) noexcept {
		if (stored_inplace()) [[likely]] {
			storage.inplaceStorage.reduce_size_to(newSize);
			return;
		}
		storage.externalStorage.reduce_size_to(newSize);
	}

	constexpr bool operator==(usually_inplace_vector const & other
	) const noexcept {
		return std::ranges::equal(span(), other.span());
	}

	constexpr bool operator!=(usually_inplace_vector const & other
	) const noexcept {
		return !operator==(other);
	}

	constexpr auto operator<=>(usually_inplace_vector const & other
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
