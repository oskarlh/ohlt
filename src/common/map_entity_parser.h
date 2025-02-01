#pragma once

#include "entity_key_value.h"
#include "mathtypes.h"
#include "wad_texture_name.h"

#include <algorithm>
#include <optional>
#include <string>

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

class map_entity_parser;

class parsed_brushes {
  private:
	friend map_entity_parser;
	friend parsed_brushes_iterator;

	// std::uint16_t was chosen because you couldn't reasonably need more
	// than 65K faces per brush
	using brush_face_count = std::uint16_t;

	std::vector<parsed_face> faces;
	std::vector<brush_face_count> numFacesInEachBrush;

  public:
	static constexpr std::size_t max_faces_per_brush
		= std::numeric_limits<brush_face_count>::max();

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
};

class map_entity_parser {
  private:
	std::u8string_view input;

	constexpr std::optional<std::u8string_view>
	parse_quoted_string(std::u8string& quotedStringBuffer) noexcept {
		if (!input.starts_with(u8'"')) {
			return std::nullopt;
		}

		quotedStringBuffer.clear();
		bool isEndQuote{};
		do {
			std::size_t const nextSpecial = input.find_first_of(
				u8"\\\"", 1
			);
			if (nextSpecial == std::u8string_view::npos) [[unlikely]] {
				return std::nullopt;
			}
			isEndQuote = input[nextSpecial] == u8'"';
			std::u8string_view const untilSpecial = input.substr(
				1, nextSpecial - 1
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

	std::u8string quotedStringBufferForKey;
	std::u8string quotedStringBufferForValue;

	constexpr bool parse_key_values(std::vector<entity_key_value>& keyValues
	) noexcept {
		while (input.starts_with(u8'"')) {
			auto maybeKey = parse_quoted_string(quotedStringBufferForKey);
			if (!maybeKey || maybeKey.value().empty()) [[unlikely]] {
				return false;
			}

			skip_whitespace_and_comments(input);

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

			skip_whitespace_and_comments(input);
		}
		return true;
	}

	std::u8string quotedStringBufferForTextureName;

	constexpr wad_texture_name parse_texture_name() noexcept {
		wad_texture_name textureName{};

		bool const isQuotedTextureName = input.starts_with(u8'"');
		if (isQuotedTextureName) [[unlikely]] {
			auto maybeValue = parse_quoted_string(quotedStringBufferForValue
			);
			if (!maybeValue) [[unlikely]] {
				return wad_texture_name{};
			}
			textureName = { maybeValue.value() };
		} else {
			std::size_t const nameLength = std::min(
				input.size(), input.find_first_of(ascii_whitespace)
			);
			if (nameLength == 0) [[unlikely]] {
				return wad_texture_name{};
			}
			textureName = std::u8string_view{ input.begin(), nameLength };
			input = input.substr(nameLength);
		}
		skip_whitespace_and_comments(input);
		return textureName;
	}

	template <std::size_t N>
	constexpr std::optional<std::array<double, N>>
	parse_doubles() noexcept {
		std::array<double, N> result;
		for (std::size_t i = 0; i != N; ++i) {
			std::optional<double> maybeElement = try_to_parse_double(input);

			if (!maybeElement) [[unlikely]] {
				return std::nullopt;
			}
			result[i] = maybeElement.value();

			skip_whitespace_and_comments(input);
		}
		return result;
	}

	template <std::size_t N>
	constexpr std::optional<std::array<double, N>>
	parse_surrounded_doubles(char8_t startChar, char8_t endChar) noexcept {
		if (!try_to_skip_one(input, startChar)) [[unlikely]] {
			return std::nullopt;
		}
		skip_whitespace_and_comments(input);

		std::optional<std::array<double, N>> maybeResult{ parse_doubles<N>(
		) };

		if (!maybeResult) [[unlikely]] {
			return std::nullopt;
		}

		if (!try_to_skip_one(input, endChar)) [[unlikely]] {
			return std::nullopt;
		}
		skip_whitespace_and_comments(input);

		return maybeResult;
	}

	constexpr bool parse_faces(std::vector<parsed_face>& out) noexcept {
		while (!input.starts_with(u8'}')) {
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

			out.push_back(parsed_face{
				.textureName = textureName,
				.planePoints = planePoints,
				.shift = shift,
				.textureScale = maybeTextureScale.value(),
				.uAxis = uAxis,
				.vAxis = vAxis });

			skip_whitespace_and_comments(input);
		}
		return true;
	}

	constexpr bool parse_brushes(parsed_brushes& out) noexcept {
		while (try_to_skip_one(input, u8'{')) {
			skip_whitespace_and_comments(input);

			std::size_t const numFacesBefore = out.faces.size();
			if (!parse_faces(out.faces)) [[unlikely]] {
				return false;
			}

			std::size_t const numFacesAdded = out.faces.size()
				- numFacesBefore;

			if (numFacesAdded < 4) [[unlikely]] {
				// Less than 4 faces can't make a concave brush
				return false;
			}

			if (numFacesAdded > parsed_brushes::max_faces_per_brush) {
				return false;
			}

			out.numFacesInEachBrush.push_back(numFacesAdded);

			if (!try_to_skip_one(input, u8'}')) [[unlikely]] {
				return false;
			}
			skip_whitespace_and_comments(input);
		}

		return true;
	}

  public:
	// Note: str will be kept and used
	constexpr map_entity_parser(std::u8string_view str) noexcept :
		input{ str } { }

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

		skip_whitespace_and_comments(input);
		if (!parse_key_values(ent.keyValues)) [[unlikely]] {
			return parse_entity_outcome::bad_input;
		}

		if (!parse_brushes(ent.brushes)) [[unlikely]] {
			return parse_entity_outcome::bad_input;
		}

		// Entity end
		if (!try_to_skip_one(input, u8'}')) [[unlikely]] {
			return parse_entity_outcome::bad_input;
		}

		return parse_entity_outcome::entity_parsed;
	}

	constexpr std::u8string_view remaining_input() noexcept {
		return input;
	}
};
