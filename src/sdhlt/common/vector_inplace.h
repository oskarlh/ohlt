#pragma once

#include "call_finally.h"

#include <concepts>
#include <memory>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

// TODO: Use std::inplace_vector instead when there's enough compiler support
template<class T, std::size_t N>
requires(std::is_nothrow_destructible_v<T>)
class vector_inplace final {
	private:
		std::size_t vectorSize{0};
		alignas(T) std::array<std::byte, sizeof(T) * N> vectorData;

	public:
		constexpr vector_inplace() noexcept = default;

		constexpr vector_inplace(const vector_inplace& copyFrom) noexcept
			requires(std::is_trivially_copy_constructible_v<T>) = default;

		constexpr vector_inplace(const vector_inplace& copyFrom)
			noexcept(
				noexcept(
					std::ranges::uninitialized_copy(
						std::declval<const vector_inplace&>(),
						std::span(std::declval<T*>(), 1)
					)
				)
			)
			requires(
				!std::is_trivially_copy_constructible_v<T>
			) {
			std::ranges::uninitialized_copy(copyFrom, std::span(begin(), copyFrom.size()));
			vectorSize = copyFrom.size();
		}

		constexpr vector_inplace(vector_inplace&& moveFrom) noexcept
			requires(std::is_trivially_copyable_v<T>) = default;


		constexpr vector_inplace(vector_inplace&& moveFrom)
			noexcept(
				noexcept(
					std::ranges::uninitialized_move(
						std::declval<vector_inplace&>(),
						std::span(std::declval<T*>(), 1)
					)
				)
			)
			requires(!std::is_trivially_copyable_v<T>)
			{
			std::ranges::uninitialized_move(
				moveFrom,
				std::span(begin(), moveFrom.size())
			);
			vectorSize = moveFrom.size();
			moveFrom.clear();
		}

		template <class... Args> constexpr T& emplace(const T* position, Args&&... args)
		requires (
			std::is_nothrow_constructible_v<T, Args...>&&
			std::is_nothrow_move_assignable_v<T> 
		) {
			if(size() == N) {
				throw std::bad_alloc();
			}
			if(!empty()) {
				std::move_backward(position, end(), end() + 1);
				std::destroy_at(position);
			}
			T* elementPointer = std::construct_at(position, std::forward<Args>(args)...);
			return *elementPointer;
		}

		template <class Range> constexpr void insert_range(const T* position, Range&& range)
		{
			std::size_t rangeSize = std::size(range);
			if(rangeSize > N - size()) {
				throw std::bad_alloc();
			}
			std::move_backward(position, end(), end() + rangeSize);
			std::destroy_n(position, rangeSize);
			// Note: If Range returns rvalues (&&), then this may move instead of copying,
			// which is great. But that's likely up to the standard library implementation
			std::uninitialized_copy_n(std::begin(range), rangeSize, position);
		}

		constexpr vector_inplace& operator=(const vector_inplace& other) noexcept
			requires(std::is_trivially_copyable_v<T>) = default;

		constexpr vector_inplace& operator=(const vector_inplace& other)
		// TODO: noexcept((((((((((((( TODO )))))))))))))
			requires(
				!std::is_trivially_copyable_v<T> &&
				std::is_copy_assignable_v<T> &&
				std::is_copy_constructible_v<T>
			) {
			std::copy(other.begin(), other.begin() + std::min(size(), other.size()), begin());
			if(other.size() > size()) {
				std::uninitialized_copy_n(other.begin() + size(), other.size() - size(), begin() + other.size());
			} else {
				shrink_to(other.size());
			}
		};
		constexpr vector_inplace& operator=(vector_inplace&& other) noexcept
			requires(std::is_trivially_copyable_v<T>) = default;

		constexpr vector_inplace& operator=(vector_inplace&& other)
		// TODO: noexcept(()()()()())
			noexcept
			requires(!std::is_trivially_copyable_v<T>)
		{
			auto clearsOther = call_finally{[&other]() { other.clear(); }};

			std::move(other.begin(), other.begin() + std::min(size(), other.size()), begin());
			if(other.size() > size()) {
				std::uninitialized_move_n(other.begin() + size(), other.size() - size(), begin() + other.size());
			} else {
				shrink_to(other.size());
			}
		};

		void resize(std::size_t newSize, const T& valueForNewElements) {
			if(newSize > N) {
				throw std::bad_alloc();
			}
			
			if(size() < newSize) {
				std::uninitialized_fill(end(), begin() + newSize, valueForNewElements);
				vectorSize = newSize;
			} else {
				shrink_to(newSize);
			}
		}

		constexpr std::size_t size() const noexcept {
			return vectorSize;
		}
		constexpr bool empty() const noexcept {
			return !size();
		}
		constexpr operator bool() const noexcept {
			return !empty();
		}

		constexpr T* data() noexcept {
			return (T*) vectorData.data();
		}
		constexpr const T* data() const noexcept {
			return (const T*) vectorData.data();
		}

		constexpr T* begin() noexcept {
			return data();
		}
		constexpr const T* begin() const noexcept {
			return data();
		}

		constexpr T* end() noexcept {
			return begin() + size();
		}
		constexpr const T* end() const noexcept {
			return begin() + size();
		}

		constexpr T& at(std::size_t index) {
			return *(begin() + index);
		}
		constexpr const T& at(std::size_t index) const {
			return *(begin() + index);
		}

		constexpr T& operator[](std::size_t index) {
			return at(index);
		}
		constexpr const T& operator[](std::size_t index) const {
			return at(index);
		}

		template<class... Args> constexpr T& emplace_back(Args&&... args) {
			if(size() == N) {
				throw std::bad_alloc();
			}

			T* elementPointer = std::construct_at(end(), std::forward<Args>(args)...);
			++vectorSize;
			return *elementPointer;
		}


		// Note: This invalidates iterators, pointers, and references to elements
		constexpr void swap(vector_inplace& other) noexcept
		requires(
			std::is_trivially_move_assignable_v<vector_inplace>
		)
		{
			using std::swap;
			swap(vectorData, other.vectorData);
			swap(vectorSize, other.vectorSize);
		}

		constexpr void swap(vector_inplace& other) 
		noexcept
		requires(
			!std::is_trivially_move_assignable_v<vector_inplace>
			&& std::is_nothrow_swappable_v<vector_inplace>
			&& std::is_nothrow_move_constructible_v<vector_inplace>
		)
		{
			if(size() > other.size()) {
				other.swap(*this);
				return;
			}

			for(std::size_t i = size() - 1; i != size(); ++i) {
				using std::swap;
				swap(at(i), other.at(i));
			}
			for(std::size_t i = size(); i != other.size(); ++i) {
				emplace_back(std::move(other.at(i)));
			}
			other.shrink_to(size());
		}

		constexpr bool pop_back() noexcept {
			if(empty()) {
				return false;
			}
			--vectorSize;
			std::destroy_at(end());
			return true;
		}

		constexpr void shrink_to(std::size_t newSize) noexcept
		requires(std::is_trivially_destructible_v<T>)
		{
			vectorSize = std::min(size(), newSize);
		}

		constexpr void shrink_to(std::size_t newSize) noexcept
		requires(!std::is_trivially_destructible_v<T>)
		{
			while(size() > newSize) {
				pop_back();
			}
		}

		constexpr void clear() noexcept {
			shrink_to(0);
		}

		constexpr ~vector_inplace() noexcept
		requires(std::is_trivially_destructible_v<T>) = default;

		constexpr ~vector_inplace() noexcept
		requires(!std::is_trivially_default_constructible_v<T>) {
			clear();
		}
};

static_assert(std::is_trivially_copy_assignable_v<vector_inplace<int, 3>>);

// Commented out because VS Code with the official C/C++ extension, on Mac
// with XCode 16.2 is incorrectly showing the assertion failing:
// static_assert(std::is_trivially_copyable_v<vector_inplace<int, 3>>);
