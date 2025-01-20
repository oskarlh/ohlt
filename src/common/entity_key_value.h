#pragma once

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <string>

class entity_key_value {
	// TODO: Create a generic short vector class and use that instead

	// TODO: Remove 0 termination once we no longer use C strings
	// anywhere

  private:
	static constexpr std::size_t inplace_storage_size = 40;

	// One for the key and one for the value
	static constexpr std::size_t num_null_terminators = 2;

	using short_data = std::array<char8_t, inplace_storage_size>;
	using long_data = char8_t*;

	union key_and_value {
		short_data shortKeyAndValue;
		long_data longKeyAndValue;
	};

	std::uint32_t keyLength;
	std::uint32_t valueLength;

	key_and_value keyAndValue;

	constexpr bool is_short_data() const noexcept {
		return keyLength + valueLength
			<= inplace_storage_size - num_null_terminators;
	}

	char8_t const * key_and_value_pointer() const {
		if (is_short_data()) [[likely]] {
			return keyAndValue.shortKeyAndValue.data();
		}
		return keyAndValue.longKeyAndValue;
	}

  public:
	constexpr entity_key_value() noexcept {
		keyAndValue.shortKeyAndValue = short_data{};
		keyLength = 0;
		valueLength = 0;
	}

	entity_key_value(entity_key_value& other) = delete;

	friend inline void
	swap(entity_key_value& a, entity_key_value& b) noexcept {
		using std::swap;
		swap(a.keyAndValue, b.keyAndValue);
		swap(a.keyLength, b.keyLength);
		swap(a.valueLength, b.valueLength);
	}

	entity_key_value& operator=(entity_key_value& other) = delete;

	constexpr entity_key_value& operator=(entity_key_value&& other
	) noexcept {
		swap(*this, other);
		return *this;
	}

	constexpr entity_key_value(entity_key_value&& other) noexcept :
		entity_key_value() {
		operator=(std::move(other));
	}

	constexpr entity_key_value(
		std::u8string_view key, std::u8string_view value
	) {
		if (key.size() > std::numeric_limits<std::uint32_t>::max()
			|| value.size() > std::numeric_limits<std::uint32_t>::max())
			[[unlikely]] {
			// Too long
			throw std::bad_alloc();
		}

		valueLength = value.size();
		if (value.empty()) [[unlikely]] {
			keyAndValue.shortKeyAndValue = short_data{};
			keyLength = 0;
		} else {
			keyLength = key.length();
			char8_t* keyStart;

			std::size_t const totalStorageSize = std::size_t(keyLength)
				+ std::size_t(valueLength) + num_null_terminators;

			if (totalStorageSize <= inplace_storage_size) [[likely]] {
				keyAndValue.shortKeyAndValue = short_data();
				keyStart = keyAndValue.shortKeyAndValue.data();
			} else {
				keyStart = new char8_t[totalStorageSize];
				keyAndValue.longKeyAndValue = keyStart;
			}

			std::ranges::copy(key, keyStart);
			keyStart[key.length()] = u8'\0';

			char8_t* valueStart{ keyStart + key.length() + 1 };
			std::ranges::copy(value, valueStart);
			valueStart[value.length()] = u8'\0';
		}
	}

	constexpr bool is_removed() const noexcept {
		return valueLength == 0;
	}

	constexpr void remove() noexcept {
		operator=(entity_key_value{});
	}

	constexpr std::u8string_view key() const noexcept {
		return std::u8string_view{ key_and_value_pointer(), keyLength };
	}

	constexpr std::u8string_view value() const noexcept {
		// + 1 for the key's null terminator
		return std::u8string_view{ key_and_value_pointer() + keyLength + 1,
								   valueLength };
	}

	constexpr ~entity_key_value() {
		if (is_short_data()) {
			keyAndValue.shortKeyAndValue.~short_data();
		} else {
			delete[] keyAndValue.longKeyAndValue;
			keyAndValue.longKeyAndValue.~long_data();
		}
	}
};
