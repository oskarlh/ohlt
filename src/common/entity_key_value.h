#pragma once

#include <algorithm>
#include <array>
#include <limits>
#include <string>

class entity_key_value {
  private:
	static constexpr std::size_t inplace_storage_size = 32 - 2;

	// TODO: Create a generic short vector class and use that instead (can
	// use inplace_vector as a base)
	struct long_data {
		char8_t* units;
		std::size_t keyLength;
		std::size_t valueLength;
	};

	using short_data = std::array<char8_t, inplace_storage_size>;

	union key_and_value {
		short_data shortKeyAndValue;
		long_data longKeyAndValue;
	};

	key_and_value keyAndValue;
	std::uint8_t shortKeyLength;
	std::uint8_t shortValueLength;

	constexpr bool is_short_data() const noexcept {
		return shortKeyLength
			!= std::numeric_limits<decltype(shortKeyLength)>::max();
	}

  public:
	constexpr entity_key_value() noexcept {
		keyAndValue.shortKeyAndValue = short_data{};
		shortKeyLength = 0;
		shortValueLength = 0;
	}

	entity_key_value(entity_key_value& other) = delete;

	friend inline void
	swap(entity_key_value& a, entity_key_value& b) noexcept {
		using std::swap;
		swap(a.keyAndValue, b.keyAndValue);
		swap(a.shortKeyLength, b.shortKeyLength);
		swap(a.shortValueLength, b.shortValueLength);
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
		if (value.empty()) {
			keyAndValue.shortKeyAndValue = short_data{};
			shortKeyLength = 0;
			shortValueLength = 0;
		} else {
			char8_t* keyStart;

			// TODO: Eventually we'll be able to get rid of the nulls
			// when we've stopped using C string functions
			std::size_t totalLengthWithNulls = key.length() + value.length()
				+ 2;
			if (totalLengthWithNulls <= inplace_storage_size) [[likely]] {
				keyAndValue.shortKeyAndValue = short_data();
				shortKeyLength = key.length();
				shortValueLength = value.length();

				keyStart = keyAndValue.shortKeyAndValue.begin();

			} else {
				keyAndValue.longKeyAndValue = long_data();
				keyAndValue.longKeyAndValue.keyLength = key.length();
				keyAndValue.longKeyAndValue.valueLength = value.length();
				shortKeyLength = -1;
				shortValueLength = -1;
				keyStart = new char8_t[totalLengthWithNulls];
				keyAndValue.longKeyAndValue.units = keyStart;
			}

			std::ranges::copy(key, keyStart);
			keyStart[key.length()] = u8'\0';

			char8_t* valueStart{ keyStart + key.length() + 1 };
			std::ranges::copy(value, valueStart);
			valueStart[value.length()] = u8'\0';
		}
	}

	constexpr bool is_removed() const noexcept {
		return shortValueLength == 0;
	}

	constexpr void remove() noexcept {
		operator=(entity_key_value{});
	}

	constexpr std::u8string_view key() const noexcept {
		if (is_short_data()) [[likely]] {
			return std::u8string_view{ keyAndValue.shortKeyAndValue.data(),
									   shortKeyLength };
		}
		return std::u8string_view{ keyAndValue.longKeyAndValue.units,
								   keyAndValue.longKeyAndValue.keyLength };
	}

	constexpr std::u8string_view value() const noexcept {
		if (is_short_data()) [[likely]] {
			return std::u8string_view{ keyAndValue.shortKeyAndValue.data()
										   + shortKeyLength + 1,
									   shortValueLength };
		}
		return std::u8string_view{
			keyAndValue.longKeyAndValue.units
				+ keyAndValue.longKeyAndValue.keyLength + 1,
			keyAndValue.longKeyAndValue.valueLength
		};
	}

	constexpr ~entity_key_value() {
		if (is_short_data()) {
			keyAndValue.shortKeyAndValue.~short_data();
		} else {
			delete[] keyAndValue.longKeyAndValue.units;
			keyAndValue.longKeyAndValue.~long_data();
		}
	}
};
