#pragma once

#include "entity_key_value.h"
#include "mathtypes.h"
#include "wad_texture_name.h"

#include <algorithm>
#include <optional>
#include <string>

struct parsed_side {
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

	std::vector<parsed_side>::const_iterator sidesIt;
	std::vector<std::uint16_t>::const_iterator numSidesInEachBrushIt;

  public:
	using value_type = std::span<parsed_side const>;

	constexpr parsed_brushes_iterator& operator++() noexcept {
		sidesIt += *numSidesInEachBrushIt;
		++numSidesInEachBrushIt;
		return *this;
	}

	constexpr value_type operator*() const noexcept {
		return value_type(sidesIt, sidesIt + *numSidesInEachBrushIt);
	}

	constexpr bool operator!=(parsed_brushes_iterator const & other
	) const noexcept {
		return numSidesInEachBrushIt != other.numSidesInEachBrushIt;
	}
};

class map_entity_parser;

class parsed_brushes {
  private:
	friend map_entity_parser;
	friend parsed_brushes_iterator;

	// std::uint16_t was chosen because you couldn't reasonably need more
	// than 65K sides per brush
	using brush_side_count = std::uint16_t;

	std::vector<parsed_side> sides;
	std::vector<brush_side_count> numSidesInEachBrush;

  public:
	static constexpr std::size_t max_sides_per_brush
		= std::numeric_limits<brush_side_count>::max();

	using const_iterator = parsed_brushes_iterator;
	using iterator = parsed_brushes_iterator;
	using value_type = std::span<parsed_side const>;
	using const_reference = value_type&;

	void clear() {
		sides.clear();
		numSidesInEachBrush.clear();
	}

	void free_memory() {
		sides.shrink_to_fit();
		numSidesInEachBrush.shrink_to_fit();
	}

	constexpr parsed_brushes_iterator cbegin() const noexcept {
		parsed_brushes_iterator it;
		it.sidesIt = sides.cbegin();
		it.numSidesInEachBrushIt = numSidesInEachBrush.cbegin();
		return it;
	}

	constexpr parsed_brushes_iterator cend() const noexcept {
		parsed_brushes_iterator it;
		it.sidesIt = sides.cend();
		it.numSidesInEachBrushIt = numSidesInEachBrush.cend();
		return it;
	}

	constexpr parsed_brushes_iterator begin() const noexcept {
		return cbegin();
	}

	constexpr parsed_brushes_iterator end() const noexcept {
		return cend();
	}

	constexpr bool empty() const noexcept {
		return numSidesInEachBrush.empty();
	}

	constexpr std::size_t size() const noexcept {
		return numSidesInEachBrush.size();
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

constexpr bool
try_to_skip_one(std::u8string_view& str, char8_t c) noexcept {
	bool const startsWithC = str.starts_with(c);
	str = str.substr((std::size_t) startsWithC);
	return startsWithC;
}

constexpr std::optional<double> try_to_parse_double(std::u8string_view& str
) noexcept {
	parse_number_result<double> pnr = parse_number<double>(str);
	str = pnr.remainingText;
	return pnr.number;
}

enum class parse_entity_outcome {
	entity_parsed,
	reached_end,
	bad_input,
	not_valve220_map_format
};

class map_entity_parser {
  private:
	std::u8string_view allInput;
	std::u8string_view remainingInput;

	constexpr std::optional<std::u8string_view>
	parse_quoted_string(std::u8string& quotedStringBuffer) noexcept {
		if (!remainingInput.starts_with(u8'"')) {
			return std::nullopt;
		}

		quotedStringBuffer.clear();
		bool foundEscapeSequence;
		do {
			std::size_t nextSpecial = 0;
			bool isBackslash;
			char8_t escapedCharacter;
			do {
				nextSpecial = remainingInput.find_first_of(
					u8"\\\"", nextSpecial + 1
				);

				if (nextSpecial == std::u8string_view::npos
					|| nextSpecial >= remainingInput.size() - 1)
					[[unlikely]] {
					return std::nullopt;
				}
				isBackslash = remainingInput[nextSpecial] == u8'\\';
				escapedCharacter = remainingInput[nextSpecial + 1];
				foundEscapeSequence = isBackslash
					&& (escapedCharacter == u8'\\'
						|| escapedCharacter == u8'"');
			} while (isBackslash && !foundEscapeSequence);

			std::u8string_view const untilSpecial = remainingInput.substr(
				1, nextSpecial - 1
			);

			remainingInput.remove_prefix(nextSpecial + 1);
			if (!foundEscapeSequence && quotedStringBuffer.empty())
				[[likely]] {
				// No escape characters in the string at all, we don't need
				// to use a buffer
				return untilSpecial;
			}
			quotedStringBuffer += untilSpecial;
			if (foundEscapeSequence) {
				quotedStringBuffer += escapedCharacter;
			}
		} while (foundEscapeSequence);
		return quotedStringBuffer;
	}

	std::u8string quotedStringBufferForKey;
	std::u8string quotedStringBufferForValue;

	constexpr bool parse_key_values(std::vector<entity_key_value>& keyValues
	) noexcept {
		while (remainingInput.starts_with(u8'"')) {
			auto maybeKey = parse_quoted_string(quotedStringBufferForKey);
			if (!maybeKey || maybeKey.value().empty()) [[unlikely]] {
				return false;
			}

			skip_whitespace_and_comments(remainingInput);

			auto maybeValue = parse_quoted_string(quotedStringBufferForValue
			);
			if (!maybeValue) [[unlikely]] {
				return false;
			}

			std::u8string_view const key = maybeKey.value();
			std::u8string_view const value = maybeValue.value();

			auto it = std::ranges::find(
				keyValues,
				key,
				[](entity_key_value const & e) { return e.key(); }
			);

			if (it != keyValues.end()) [[unlikely]] {
				// The new value replaces the old one
				it->set_value(value);
			} else if (!value.empty()) [[likely]] {
				keyValues.emplace_back(key, value);
			}

			skip_whitespace_and_comments(remainingInput);
		}
		return true;
	}

	std::u8string quotedStringBufferForTextureName;

	constexpr wad_texture_name parse_texture_name() noexcept {
		wad_texture_name textureName{};

		bool const isQuotedTextureName = remainingInput.starts_with(u8'"');
		if (isQuotedTextureName) [[unlikely]] {
			auto maybeValue = parse_quoted_string(quotedStringBufferForValue
			);
			if (!maybeValue) [[unlikely]] {
				return wad_texture_name{};
			}
			textureName = { maybeValue.value() };
		} else {
			std::size_t const nameLength = std::min(
				remainingInput.size(),
				remainingInput.find_first_of(ascii_whitespace)
			);
			if (nameLength == 0) [[unlikely]] {
				return wad_texture_name{};
			}
			textureName = std::u8string_view{ remainingInput.begin(),
											  nameLength };
			remainingInput = remainingInput.substr(nameLength);
		}
		skip_whitespace_and_comments(remainingInput);
		return textureName;
	}

	template <std::size_t N>
	constexpr std::optional<std::array<double, N>>
	parse_doubles() noexcept {
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
	constexpr std::optional<std::array<double, N>>
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

	constexpr bool parse_sides(std::vector<parsed_side>& out) noexcept {
		while (!remainingInput.starts_with(u8'}')) {
			// Read 3-point plane definition for brush side
			std::array<double3_array, 3> planePoints;
			for (std::size_t pointIndex = 0; pointIndex != 3;
				 ++pointIndex) {
				std::optional<double3_array> maybePoint{
					parse_surrounded_doubles<3>(u8'(', u8')')
				};
				if (!maybePoint) [[unlikely]] {
					return false;
				}
				planePoints[pointIndex] = maybePoint.value();
			}

			wad_texture_name const textureName{ parse_texture_name() };
			if (textureName.empty()) [[unlikely]] {
				return false;
			}

			auto maybeU = parse_surrounded_doubles<4>(u8'[', u8']');
			if (!maybeU) [[unlikely]] {
				return false;
			}

			auto maybeV = parse_surrounded_doubles<4>(u8'[', u8']');
			if (!maybeV) [[unlikely]] {
				return false;
			}

			// This value is useless in the Valve 220 MAP format - texture
			// rotation is implicit in U/V axes
			if (!parse_doubles<1>()) [[unlikely]] {
				return false;
			}

			auto maybeTextureScale = parse_doubles<2>();
			if (!maybeTextureScale) [[unlikely]] {
				return false;
			}

			double3_array const uAxis{ maybeU.value()[0],
									   maybeU.value()[1],
									   maybeU.value()[2] };

			double3_array const vAxis{ maybeV.value()[0],
									   maybeV.value()[1],
									   maybeV.value()[2] };

			std::array<double, 2> const shift{ maybeU.value()[3],
											   maybeV.value()[3] };

			out.push_back(parsed_side{
				.textureName = textureName,
				.planePoints = planePoints,
				.shift = shift,
				.textureScale = maybeTextureScale.value(),
				.uAxis = uAxis,
				.vAxis = vAxis });

			skip_whitespace_and_comments(remainingInput);
		}
		return true;
	}

	constexpr bool parse_brushes(parsed_brushes& out) noexcept {
		while (try_to_skip_one(remainingInput, u8'{')) {
			skip_whitespace_and_comments(remainingInput);

			std::size_t const numSidesBefore = out.sides.size();
			if (!parse_sides(out.sides)) [[unlikely]] {
				return false;
			}

			std::size_t const numSidesAdded = out.sides.size()
				- numSidesBefore;

			if (numSidesAdded < 4) [[unlikely]] {
				// Less than 4 sides can't make a concave brush
				return false;
			}

			if (numSidesAdded > parsed_brushes::max_sides_per_brush) {
				return false;
			}

			out.numSidesInEachBrush.push_back(numSidesAdded);

			if (!try_to_skip_one(remainingInput, u8'}')) [[unlikely]] {
				return false;
			}
			skip_whitespace_and_comments(remainingInput);
		}

		return true;
	}

	// The compilers only support the Valve 220 .MAP format. .MAP files
	// created by old versions of WorldCraft or Quark are not supported.
	constexpr bool isValve220Format() const noexcept {
		constexpr std::u8string_view mapVersionKey = u8"\"mapversion\"";
		std::size_t mapVersionKeyStart = allInput.find(mapVersionKey);
		if (mapVersionKeyStart == std::u8string_view::npos) {
			return false;
		}
		std::u8string_view afterMapVersionStartKey = allInput.substr(
			mapVersionKeyStart + mapVersionKey.length()
		);
		skip_whitespace_and_comments(afterMapVersionStartKey);
		return afterMapVersionStartKey.starts_with(u8"\"220\"");
	}

  public:
	// Note: str will be kept and used
	constexpr map_entity_parser(std::u8string_view str) noexcept :
		allInput(str), remainingInput{ str } { }

	constexpr parse_entity_outcome parse_entity(parsed_entity& ent
	) noexcept {
		ent.clear();

		skip_whitespace_and_comments(remainingInput);

		if (remainingInput.empty()) {
			ent.free_memory();
			return parse_entity_outcome::reached_end;
		}

		// Entity start
		if (try_to_skip_one(remainingInput, u8'{')) [[likely]] {
			skip_whitespace_and_comments(remainingInput);

			if (parse_key_values(ent.keyValues)
				&& parse_brushes(ent.brushes)
				&& try_to_skip_one(remainingInput, u8'}') // Entity end
			) [[likely]] {
				return parse_entity_outcome::entity_parsed;
			}
		}

		if (!isValve220Format()) {
			return parse_entity_outcome::not_valve220_map_format;
		}
		return parse_entity_outcome::bad_input;
	}

	constexpr std::u8string_view remaining_input() noexcept {
		return remainingInput;
	}
};
