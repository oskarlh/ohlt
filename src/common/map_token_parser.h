#pragma once

#include "entity_key_value.h"
#include "mathtypes.h"
#include "wad_texture_name.h"

#include <algorithm>
#include <optional>
#include <string>

// skip_ascii_whitespace();

// Allow wad names to be quoted (and then escaped and possibly containing
// spaces)

struct parsed_face {
	wad_texture_name textureName;
	std::array<double3_array, 3> planePoints;
	std::array<double, 2> shift;
	std::array<double, 2> textureScale;
	double3_array uAxis;
	double3_array vAxis;
};

class parsed_brushes_iterator {
  private:
	friend class parsed_brushes;

	std::vector<parsed_face>::const_iterator facesIt;
	std::vector<std::uint16_t>::const_iterator numFacesInEachBrushIt;

  public:
	using value_type = std::span<parsed_face const>;

	constexpr parsed_brushes_iterator& operator++() noexcept {
		facesIt += *numFacesInEachBrushIt;
		++numFacesInEachBrushIt;
		return *this;
	}

	constexpr value_type operator*() const noexcept {
		return value_type(facesIt, facesIt + *numFacesInEachBrushIt);
	}

	constexpr bool operator!=(parsed_brushes_iterator const & other
	) const noexcept {
		return numFacesInEachBrushIt != other.numFacesInEachBrushIt;
	}
};

class parsed_brushes {
  private:
	friend parsed_brushes_iterator;
	std::vector<parsed_face> faces;
	std::vector<std::uint16_t> numFacesInEachBrush;

  public:
	using const_iterator = parsed_brushes_iterator;
	using iterator = parsed_brushes_iterator;
	using value_type = std::span<parsed_face const>;
	using const_reference = value_type&;

	void clear() {
		faces.clear();
		numFacesInEachBrush.clear();
	}

	void free_memory() {
		faces.shrink_to_fit();
		numFacesInEachBrush.shrink_to_fit();
	}

	constexpr parsed_brushes_iterator cbegin() const noexcept {
		parsed_brushes_iterator it;
		it.facesIt = faces.cbegin();
		it.numFacesInEachBrushIt = numFacesInEachBrush.cbegin();
		return it;
	}

	constexpr parsed_brushes_iterator cend() const noexcept {
		parsed_brushes_iterator it;
		it.facesIt = faces.cend();
		it.numFacesInEachBrushIt = numFacesInEachBrush.cend();
		return it;
	}

	constexpr parsed_brushes_iterator begin() const noexcept {
		return cbegin();
	}

	constexpr parsed_brushes_iterator end() const noexcept {
		return cend();
	}

	constexpr bool empty() const noexcept {
		return numFacesInEachBrush.empty();
	}

	constexpr std::size_t size() const noexcept {
		return numFacesInEachBrush.size();
	}
};

struct parsed_entity {
	std::vector<entity_key_value> keyValues;
	parsed_brushes brushes;

	void clear() {
		keyValues.clear();
		brushes.clear();
	}

	void free_memory() {
		keyValues.shrink_to_fit();
		brushes.free_memory();
	}
};

constexpr bool
try_to_skip_one(std::u8string_view& str, char8_t c) noexcept {
	bool const startsWithC = str.starts_with(c);
	str.substr((std::size_t) startsWithC);
	return startsWithC;
}

constexpr void skip_whitespace_and_comments(std::u8string_view& str
) noexcept {
	bool foundComment{};
	do {
		str = skip_ascii_whitespace(str);

		foundComment = str.starts_with(u8"//") || str.starts_with(u8"#")
			|| str.starts_with(u8";");

		if (foundComment) [[unlikely]] {
			std::size_t const endOfLine = str.find(u8'\n');
			str = str.substr(endOfLine + 1);
			bool const isEndOfFile = endOfLine == std::u8string_view::npos;
			if (isEndOfFile) {
				str = {};
			}
		}
	} while (foundComment);
}

enum class parse_entity_outcome {
	parsed_entity,
	reached_end,
	bad_input,
};

class map_parser {
  private:
	std::u8string quotedStringBufferA;
	std::u8string quotedStringBufferB;
	std::u8string_view input;

	constexpr std::optional<std::u8string_view>
	parse_quoted_string(std::u8string& quotedStringBuffer) noexcept {
		if (!input.starts_with(u8'"')) {
			return std::nullopt;
		}

		quotedStringBuffer.clear();
		bool isEndQuote{};
		do {
			std::size_t const nextSpecial = input.find_first_of(u8"\\", 1);
			if (nextSpecial == std::u8string_view::npos) [[unlikely]] {
				return std::nullopt;
			}
			isEndQuote = input[nextSpecial] == u8'"';
			std::u8string_view const untilSpecial = input.substr(
				1, nextSpecial
			);
			input = input.substr(nextSpecial + 1);
			if (isEndQuote && quotedStringBuffer.empty()) [[likely]] {
				// No escape characters in the string at all, we don't need
				// to use a buffer
				return untilSpecial;
			}
			quotedStringBuffer += untilSpecial;
		} while (!isEndQuote);
		return quotedStringBuffer;
	}

  public:
	constexpr parse_entity_outcome parse_entity(parsed_entity& ent
	) noexcept {
		ent.clear();

		skip_whitespace_and_comments(input);

		if (input.empty()) {
			ent.free_memory();
			return parse_entity_outcome::reached_end;
		}

		if (!try_to_skip_one(input, u8'{')) [[unlikely]] {
			return parse_entity_outcome::bad_input;
		}

		// Key-values
		while (input.starts_with(u8'"')) {
			skip_whitespace_and_comments(input);

			auto maybeKey = parse_quoted_string(quotedStringBufferA);
			if (!maybeKey) [[unlikely]] {
				return parse_entity_outcome::bad_input;
			}

			skip_whitespace_and_comments(input);

			auto maybeValue = parse_quoted_string(quotedStringBufferB);
			if (!maybeValue) [[unlikely]] {
				return parse_entity_outcome::bad_input;
			}

			// Ignore empty values
			if (maybeValue.value().empty()) {
				continue;
			}

			entity_key_value kv{ maybeKey.value(), maybeValue.value() };

			if (std::some(ent.keyValues))
		}

		// Key values
		if (!try_to_skip_one(input, u8'"')) [[unlikely]] {
			return parse_entity_outcome::bad_input;
		}

		if (!) {
			return parse_entity_outcome::parsed_entity;
		}
	}
};

/*
class map_token_parser {
  private:
	std::u8string_view remainingSource;

  public:
	constexpr map_token_parser() noexcept = default;
	std::optional<std::u8string> read_raw_token() noexcept;

	std::optional<float> read_float() noexcept;
	std::optional<double> read_double() noexcept;

	std::optional<float3_array> read_float3() noexcept;
	std::optional<double3_array> read_double3() noexcept;

	std::optional<entity_key_value> read_key_and_value() noexcept;
	std::optional<wad_texture_name> read_texture_name() noexcept;

	bool skip(char8_t symbol) noexcept;
};
*/
