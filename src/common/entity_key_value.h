#pragma once

#include "usually_inplace_vector.h"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <string>

// TODO: Remove 0 termination once we no longer use C strings
// anywhere
class entity_key_value {
  private:
	// Contains key, null byte for key, value, null byte for value.
	usually_inplace_vector<char8_t, 40> storage;
	std::size_t keyLength = 0;

  public:
	constexpr entity_key_value() noexcept :
		storage{ std::array{ u8'\0', u8'\0' } } {};

	constexpr entity_key_value(entity_key_value const & other) = default;
	constexpr entity_key_value(entity_key_value&&) noexcept = default;

	constexpr entity_key_value& operator=(entity_key_value const &)
		= default;
	constexpr entity_key_value& operator=(entity_key_value&&) noexcept
		= default;

	constexpr entity_key_value(std::u8string_view k, std::u8string_view v)
		noexcept {
		if (k.empty() || v.empty()) {
			remove();
			return;
		}

		keyLength = k.size();

		storage.reserve(
			// +2 for null terminators
			k.size() + v.size() + 2
		);
		storage.append_range(k);
		storage.emplace_back(u8'\0');
		storage.append_range(v);
		storage.emplace_back(u8'\0');
	}

	friend constexpr inline void
	swap(entity_key_value& a, entity_key_value& b) noexcept {
		using std::swap;
		swap(a.storage, b.storage);
		swap(a.keyLength, b.keyLength);
	}

	constexpr std::u8string_view key() const noexcept {
		return { storage.begin(), keyLength };
	}

	constexpr std::u8string_view value() const noexcept {
		std::span<char8_t const> allData = storage.span();
		return {
			allData.begin() + keyLength + 1, // +1 for null
			allData.end() - 1				 // -1 for null
		};
	}

	constexpr void remove() noexcept {
		storage = std::array{ u8'\0', u8'\0' };
		keyLength = 0;
	}

	constexpr bool is_removed() const noexcept {
		return keyLength == 0;
	}

	constexpr void set_value(std::u8string_view newValue) {
		if (newValue.empty()) [[unlikely]] {
			remove();
			return;
		}

		storage.reduce_size_to(keyLength + 1);				// +1 for null
		storage.reserve(keyLength + newValue.length() + 2); // +2 for nulls
		storage.append_range(newValue);
		storage.emplace_back(u8'\0');
	}
};
