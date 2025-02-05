#pragma once

#include "entity_key_value.h"
#include "internal_types/internal_types.h"
#include "mathtypes.h"
#include "parsing.h"
#include "wad_texture_name.h"

#include <algorithm>
#include <optional>
#include <span>
#include <string>

struct parsed_side final {
	wad_texture_name textureName;
	std::array<double3_array, 3> planePoints;
	std::array<double, 2> shift;
	std::array<double, 2> textureScale;
	double3_array uAxis;
	double3_array vAxis;
};

struct parsed_brush final {
	entity_local_brush_count entityLocalBrushNumber;
	std::span<parsed_side const> sides;
};

class parsed_brushes;

class parsed_brushes_iterator final {
  private:
	friend parsed_brushes;

	parsed_brushes const * parsedBrushesContainer{};
	entity_local_brush_count entityLocalBrushNumber{};

  public:
	using value_type = parsed_brush;

	constexpr parsed_brushes_iterator() noexcept = default;

	parsed_brushes_iterator& operator++() noexcept;
	value_type operator*() const noexcept;

	bool operator!=(parsed_brushes_iterator const & other) const noexcept;
};

class map_entity_parser;

class parsed_brushes final {
  private:
	friend map_entity_parser;
	friend parsed_brushes_iterator;

	std::vector<parsed_side> sides;
	std::vector<side_count>
		firstSideNumberPerBrush; // + 1 element at the end to mark the end

  public:
	using const_iterator = parsed_brushes_iterator;
	using iterator = parsed_brushes_iterator;
	using value_type = parsed_brush;
	using const_reference = value_type&;

	parsed_brushes();

	void clear();

	void free_memory();

	bool empty() const noexcept;

	std::size_t size() const noexcept;

	parsed_brushes_iterator cbegin() const noexcept;

	parsed_brushes_iterator cend() const noexcept;

	parsed_brushes_iterator begin() const noexcept;

	parsed_brushes_iterator end() const noexcept;
};

struct parsed_entity final {
	entity_count entityNumber{};
	std::vector<entity_key_value> keyValues;
	parsed_brushes brushes;

	void clear() {
		entityNumber = 0;
		keyValues.clear();
		brushes.clear();
	}

	void free_memory() {
		keyValues.shrink_to_fit();
		brushes.free_memory();
	}
};

constexpr std::optional<double> try_to_parse_double(std::u8string_view& str
) noexcept {
	parse_number_result<double> pnr = parse_number<double>(str);
	str = pnr.remainingText;
	return pnr.number;
}

constexpr bool
try_to_skip_one(std::u8string_view& str, char8_t c) noexcept {
	bool const startsWithC = str.starts_with(c);
	str = str.substr((std::size_t) startsWithC);
	return startsWithC;
}

enum class parse_entity_outcome {
	entity_parsed,
	reached_end,
	bad_input,
	not_valve220_map_format
};

class map_entity_parser final {
  private:
	std::u8string_view allInput;
	std::u8string_view remainingInput;
	entity_count numParsedEntities{};

	std::optional<std::u8string_view>
	parse_quoted_string(std::u8string& quotedStringBuffer) noexcept;

	std::u8string quotedStringBufferForKey;
	std::u8string quotedStringBufferForValue;
	bool parse_key_values(std::vector<entity_key_value>& keyValues
	) noexcept;

	std::u8string quotedStringBufferForTextureName;
	wad_texture_name parse_texture_name() noexcept;

	template <std::size_t N>
	std::optional<std::array<double, N>> parse_doubles() noexcept {
		std::array<double, N> result;
		for (std::size_t i = 0; i != N; ++i) {
			std::optional<double> maybeElement = try_to_parse_double(
				remainingInput
			);

			if (!maybeElement) [[unlikely]] {
				return std::nullopt;
			}
			result[i] = maybeElement.value();

			skip_whitespace_and_comments(remainingInput);
		}
		return result;
	}

	template <std::size_t N>
	std::optional<std::array<double, N>>
	parse_surrounded_doubles(char8_t startChar, char8_t endChar) noexcept {
		if (!try_to_skip_one(remainingInput, startChar)) [[unlikely]] {
			return std::nullopt;
		}
		skip_whitespace_and_comments(remainingInput);

		std::optional<std::array<double, N>> maybeResult{ parse_doubles<N>(
		) };

		if (!maybeResult) [[unlikely]] {
			return std::nullopt;
		}

		if (!try_to_skip_one(remainingInput, endChar)) [[unlikely]] {
			return std::nullopt;
		}
		skip_whitespace_and_comments(remainingInput);

		return maybeResult;
	}

	bool parse_sides(std::vector<parsed_side>& out) noexcept;

	bool parse_brushes(parsed_brushes& out) noexcept;

	// The compilers only support the Valve 220 .MAP format. .MAP files
	// created by old versions of WorldCraft or Quark are not supported.
	bool isValve220Format() const noexcept;

  public:
	// Note: str will be kept and used
	map_entity_parser(std::u8string_view str) noexcept;

	parse_entity_outcome parse_entity(parsed_entity& ent) noexcept;

	std::u8string_view remaining_input() noexcept;
};
