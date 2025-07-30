#pragma once

#include <span>
#include <string>
#include <vector>

// Having hashing implemented for types helps when debugging,
// because we can detect unintended changes to data

template <class Object>
struct hash_multiple_helper {
	constexpr std::size_t operator()(Object const & obj) const noexcept {
		return std::hash<Object>()(obj);
	}
};

template <class T, std::size_t Extent>
struct hash_multiple_helper<std::span<T const, Extent>> {
	constexpr std::size_t operator()(std::span<T const, Extent> elements
	) const noexcept {
		std::size_t h = 0;
		for (T const & element : elements) {
			h ^= (h << 6) + (h >> 2) + 5431zu
				+ hash_multiple_helper<T>()(element);
		}
		return h;
	}
};

template <class T, std::size_t Extent>
struct hash_multiple_helper<std::span<T, Extent>> {
	constexpr std::size_t operator()(std::span<T, Extent> elements
	) const noexcept {
		std::size_t h = 0;
		for (T const & element : elements) {
			h ^= (h << 6) + (h >> 2) + 5431zu
				+ hash_multiple_helper<T>()(element);
		}
		return h;
	}
};

template <class T, std::size_t N>
struct hash_multiple_helper<std::array<T, N>> {
	constexpr std::size_t operator()(std::array<T, N> const & elements
	) const noexcept {
		return hash_multiple_helper<std::span<T const, N>>()(
			std::span<T const, N>{ elements }
		);
	}
};

// Unnecessary?
template <class T>
struct hash_multiple_helper<std::basic_string<T>> {
	constexpr std::size_t operator()(std::basic_string<T> const & elements
	) const noexcept {
		return std::hash<std::basic_string_view<T>>()(
			std::basic_string_view<T>{ elements }
		);
	}
};

template <class T>
struct hash_multiple_helper<std::vector<T>> {
	constexpr std::size_t operator()(std::vector<T> const & elements
	) const noexcept {
		return hash_multiple_helper<std::span<T const>>()(std::span{
			elements.cbegin(), elements.cend() });
	}
};

template <class Object>
struct hash_multiple_helper<Object*> {
	constexpr std::size_t operator()(Object const * objPtr) const noexcept {
		return objPtr ? hash_multiple_helper<Object>()(*objPtr) : 0;
	}
};

template <class Object>
constexpr std::size_t hash_multiple(Object const & object) noexcept {
	return hash_multiple_helper<Object>()(object);
}

template <class FirstObject, class... Rest>
constexpr std::size_t hash_multiple(
	FirstObject const & firstObject, Rest const &... rest
) noexcept {
	std::size_t h = hash_multiple_helper<FirstObject>()(firstObject);
	h ^= (h << 6) + (h >> 2) + 5431zu + hash_multiple<Rest...>(rest...);
	return h;
}
