#pragma once

#include <memory>
#include <type_traits>

// Unlike std::vector, this container doesn't value initializing elements.
// Use it with types like std::array<float, 3> or bool if the elements will
// always be written to at least once before being read from. Values should
// be considered lost after resizing.
template <class T>
class vector_for_overwriting {
	static_assert(
		std::is_trivially_default_constructible_v<T>
			&& std::is_trivially_destructible_v<T>,
		"This container is meant for trivially constructible types."
		" Either change the type to make it trivially constructible"
		" and destructible, or just use std::vector."
	);

  private:
	std::unique_ptr<T[]> dataPtr{};
	std::size_t allocated{ 0 };
	std::size_t lastRequestedSize{ 0 };

  public:
	friend inline void
	swap(vector_for_overwriting& a, vector_for_overwriting& b) {
		using std::swap;
		swap(a.dataPtr, b.dataPtr);
		swap(a.allocated, b.allocated);
		swap(a.lastRequestedSize, b.lastRequestedSize);
	}

	constexpr vector_for_overwriting() noexcept = default;
	constexpr vector_for_overwriting(vector_for_overwriting const & other
	) = default;

	constexpr vector_for_overwriting(vector_for_overwriting&& other
	) noexcept {
		swap(*this, other);
	}

	constexpr ~vector_for_overwriting() = default;

	constexpr vector_for_overwriting&
	operator=(vector_for_overwriting const & other
	) = default;

	constexpr vector_for_overwriting&
	operator=(vector_for_overwriting&& other) noexcept {
		swap(*this, other);
		return *this;
	}

	constexpr std::size_t size() const noexcept {
		return lastRequestedSize;
	}

	constexpr T* data() noexcept {
		return dataPtr.get();
	}

	constexpr T const * data() const noexcept {
		return dataPtr.get();
	}

	constexpr T* begin() noexcept {
		return data();
	}

	constexpr T const * begin() const noexcept {
		return data();
	}

	constexpr T* end() noexcept {
		return data() + size();
	}

	constexpr T const * end() const noexcept {
		return data() + size();
	}

	constexpr T& front() noexcept {
		return *begin();
	}

	constexpr T const & front() const noexcept {
		return *begin();
	}

	constexpr T& back() noexcept {
		return *(end() - 1);
	}

	constexpr T const & back() const noexcept {
		return *(end() - 1);
	}

	constexpr void reset(std::size_t newRequestedSize) {
		if (newRequestedSize > allocated) {
			dataPtr.reset();
			// Since this container is meant for temporary data that will
			// soon be deallocated, we can afford to splurge and avoid
			// having to do too many allocations.
			allocated = std::max(
				newRequestedSize, newRequestedSize * 2 + 32
			);
			dataPtr = std::make_unique_for_overwrite<T[]>(allocated);
		}
		lastRequestedSize = newRequestedSize;
	}

	constexpr std::span<T> span() noexcept {
		return std::span(data(), size());
	}

	constexpr std::span<T const> span() const noexcept {
		return std::span(data(), size());
	}
};
