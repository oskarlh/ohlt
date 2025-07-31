#pragma once

#include "filelib.h"

#include <string_view>

class worldspawn_wad_value_iterator final {
  private:
	std::u8string_view remaining;
	std::u8string_view currentWad;

  public:
	using value_type = std::u8string_view;

	constexpr worldspawn_wad_value_iterator& operator++() noexcept {
		do {
			std::size_t nextSeparator = remaining.find(u8';');
			if (nextSeparator == std::u8string_view::npos) {
				currentWad = remaining;
				remaining = {};
			} else {
				currentWad = filename_in_file_path_string(
					remaining.substr(0, nextSeparator)
				);
				remaining = remaining.substr(nextSeparator + 1);
			}
		} while (currentWad.empty() && !remaining.empty());
		return *this;
	}

	constexpr worldspawn_wad_value_iterator() = default;
	constexpr worldspawn_wad_value_iterator(worldspawn_wad_value_iterator const &)
		= default;

	constexpr worldspawn_wad_value_iterator(std::u8string_view wadValue
	) noexcept :
		remaining(wadValue) {
		++*this;
	}

	constexpr worldspawn_wad_value_iterator&
	operator=(worldspawn_wad_value_iterator const &)
		= default;

	constexpr std::u8string_view operator*() const noexcept {
		return currentWad;
	}

	constexpr bool operator!=(worldspawn_wad_value_iterator const & other
	) const noexcept {
		return remaining.length() != other.remaining.length()
			|| currentWad.length() != other.currentWad.length();
	}
};

class worldspawn_wad_value_parser final {
  private:
	std::u8string_view wadValue;

  public:
	constexpr worldspawn_wad_value_parser(std::u8string_view wadString) :
		wadValue(wadString) {};

	constexpr worldspawn_wad_value_iterator begin() const noexcept {
		return worldspawn_wad_value_iterator(wadValue);
	}

	constexpr worldspawn_wad_value_iterator end() const noexcept {
		return worldspawn_wad_value_iterator();
	}
};
