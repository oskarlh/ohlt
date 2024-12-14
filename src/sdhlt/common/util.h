#pragma once

#include <concepts>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

template<class... Ts> struct overload : Ts... { using Ts::operator()...; };
template<class... Ts> overload(Ts...) -> overload<Ts...>;
template<class var_t, class... Func> auto visit_with(var_t && variant, Func &&... funcs)
{
	return std::visit(overload{ funcs... }, variant);
}




// TODO: Use std::inplace_vector instead when there's enough compiler support
template<class T, std::size_t N>
requires(std::is_nothrow_destructible_v<T>)
class custom_inplace_vector final {
	private:
		std::size_t vectorSize{0};
		alignas(T) std::array<std::byte, sizeof(T) * N> vectorData;

		template<class... Args> constexpr T& emplace_back_without_size_check(Args&&... args)
		noexcept(std::is_nothrow_constructible_v<T, Args...>)
		 {
			T* elementPointer = new((std::byte*) end()) T(std::forward<Args>(args)...);
			++vectorSize;
			return *elementPointer;
		}
	public:
		constexpr custom_inplace_vector() noexcept = default;

		constexpr custom_inplace_vector(const custom_inplace_vector& copyFrom) noexcept
			requires(std::is_trivially_copy_constructible_v<T>) = default;

		constexpr custom_inplace_vector(const custom_inplace_vector& copyFrom)
			noexcept(noexcept(emplace_back_without_size_check(std::declval<const T&>())))
			requires(
				!std::is_trivially_copy_constructible_v<T>
				&& std::is_copy_constructible_v<T>
			) {

			for(const T& originalElement : copyFrom) {
				emplace_back_without_size_check(originalElement);
			}
		}

		constexpr custom_inplace_vector(custom_inplace_vector&& moveFrom) noexcept
			requires(std::is_trivially_copyable_v<T>) = default;


		constexpr custom_inplace_vector(custom_inplace_vector&& moveFrom)
			noexcept(noexcept(T(std::declval<T&&>())))
			requires(!std::is_trivially_copyable_v<T>
				&& std::is_nothrow_move_constructible_v<T>
			)
			{
			for(T& originalElement : moveFrom) {
				emplace_back_without_size_check(std::move(originalElement));
			}
			moveFrom.clear();
		}

		constexpr custom_inplace_vector(custom_inplace_vector&& moveFrom)
			noexcept(noexcept(T(std::declval<T&&>())))
			requires(
				!std::is_trivially_copyable_v<T>
				&& std::is_trivially_move_constructible_v<T>
			)
			{
			for(T& originalElement : moveFrom) {
				emplace_back_without_size_check(std::move(originalElement));
			}
			moveFrom.clear();
		}


		constexpr custom_inplace_vector& operator=(const custom_inplace_vector& other) noexcept
			requires(std::is_trivially_copyable_v<T>) = default;

		constexpr custom_inplace_vector& operator=(const custom_inplace_vector& other)
		// TODO: noexcept((((((((((((( TODO )))))))))))))
			requires(
				!std::is_trivially_copyable_v<T> &&
				std::is_copy_assignable_v<T> &&
				std::is_copy_constructible_v<T>
			) {
			std::size_t index = 0;
			std::size_t assignStop = std::min(size(), other.size());
			while(index < assignStop) {
				at(index) = other.at(index);
				++index;
			}
			while(index < other.size()) {
				emplace_back(other.at(index));
				++index;
			}
			shrink_to(other.size());
		};
		constexpr custom_inplace_vector& operator=(custom_inplace_vector&& other) noexcept
			requires(std::is_trivially_copyable_v<T>) = default;

		constexpr custom_inplace_vector& operator=(custom_inplace_vector&& other)
		// TODO: noexcept(()()()()())
			requires(!std::is_trivially_copyable_v<T>
				&& std::is_nothrow_swappable_v<T>
				&& std::is_nothrow_move_constructible_v<T>
			)
		{
			std::size_t index = 0;
			std::size_t assignStop = std::min(size(), other.size());
			while(index < assignStop) {
				at(index) = std::move(other.at(index));
				++index;
			}
			while(index < other.size()) {
				emplace_back(std::move(other.at(index)));
				++index;
			}
			shrink_to(other.size());
			other.clear();
		};

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

		constexpr T* end() {
			return begin() + size();
		}
		constexpr const T* end() const {
			return begin() + size();
		}

		constexpr T& at(std::size_t index) {
			return *(begin() + index);
		}
		constexpr const T& at(std::size_t index) const {
			return *(begin() + index);
		}

		template<class... Args> constexpr T& emplace_back(Args&&... args) {
			if(size() == N) {
				throw std::bad_alloc();
			}
			return emplace_back_without_size_check(std::forward<Args>(args)...);
		}


		// Note: This invalidates iterators, pointers, and references to elements
		constexpr void swap(custom_inplace_vector& other) noexcept
		requires(
			std::is_trivially_move_assignable_v<custom_inplace_vector>
		)
		{
			using std::swap;
			swap(vectorData, other.vectorData);
			swap(vectorSize, other.vectorSize);
		}

		constexpr void swap(custom_inplace_vector& other) 
		noexcept
		requires(
			!std::is_trivially_move_assignable_v<custom_inplace_vector>
			&& std::is_nothrow_swappable_v<custom_inplace_vector>
			&& std::is_nothrow_move_constructible_v<custom_inplace_vector>
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
			end()->~T();
			return true;
		}

		constexpr void shrink_to(std::size_t newSize) noexcept
		requires(std::is_trivially_destructible_v<T>)
		{
			vectorSize = 0;
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

		constexpr ~custom_inplace_vector() noexcept
		requires(std::is_trivially_destructible_v<T>) = default;


		constexpr ~custom_inplace_vector() noexcept
		requires(!std::is_trivially_default_constructible_v<T>) {
			clear();
		}
};

//static_assert(std::is_trivially_copy_assignable_v<custom_inplace_vector<int, 3>>);
//static_assert(std::is_trivially_copyable_v<custom_inplace_vector<int, 3>>);


#include <iostream>
inline void testzzzzzz () {
	custom_inplace_vector<std::string, 4> strings;
	custom_inplace_vector<int, 4> ints;

	custom_inplace_vector<int, 4> ints2(ints);
	custom_inplace_vector<std::string, 4> strings2(strings);
	custom_inplace_vector<int, 4> ints3(std::move(ints));
	custom_inplace_vector<std::string, 4> strings3(std::move(strings));
	

	custom_inplace_vector<float, 4123> floats;
	floats.emplace_back(43.1325);
	floats.emplace_back(43.1325);
	floats.emplace_back(43.1325);
	floats.emplace_back(43.1325);
	floats.emplace_back(0);

	strings3.emplace_back("g");
	strings3.emplace_back("abbb");
	strings3.emplace_back("testtest");
		std::cout << "AAAAAAAAA" << floats.at(0) << '\n';
		std::cout << "AAAAAAAAA" << floats.at(1) << '\n';
		std::cout << "AAAAAAAAA" << floats.at(2) << '\n';
		std::cout << "AAAAAAAAA" << floats.at(3) << '\n';
		std::cout << "AAAAAAAAA" << floats.at(4) << '\n';
	for(auto& t : std::span(floats)) {
		std::cout << "AAAAAAAAB" << t << '\n';
	}
	std::cout << '\n';
}

/*

template<class T> class short_vector_base {
	protected:
		std::size_t vectorSize;
		T* dataPointer;

		short_vector_base(std::size_t vs, T* dp): {}
	protected:

}



template<class T, std::size_t N> class short_vector final {
	private:
		std::size_t vectorSize;
		std::array<std::byte, sizeof(T) * N> storage;
}
*/
