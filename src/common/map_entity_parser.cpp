#include "map_entity_parser.h"

parsed_brush parsed_brushes_iterator::operator*() const noexcept {
	return parsed_brush{
		.entityLocalBrushNumber = entityLocalBrushNumber,
		.sides
		= std::span{ parsedBrushesContainer->sides.begin()
						 + parsedBrushesContainer->firstSideNumberPerBrush
							   [entityLocalBrushNumber],
					 parsedBrushesContainer->sides.begin()
						 + parsedBrushesContainer->firstSideNumberPerBrush
							   [entityLocalBrushNumber + 1] }
	};
}

parsed_brushes::parsed_brushes() {
	sides.reserve(2048 * 6);
	firstSideNumberPerBrush.reserve(2048);
	firstSideNumberPerBrush.push_back(0);
}

parsed_brushes_iterator& parsed_brushes_iterator::operator++() noexcept {
	++entityLocalBrushNumber;
	return *this;
}

bool parsed_brushes_iterator::operator!=(
	parsed_brushes_iterator const & other
) const noexcept {
	return entityLocalBrushNumber != other.entityLocalBrushNumber;
}

void parsed_brushes::clear() {
	sides.clear();
	firstSideNumberPerBrush.clear();
	firstSideNumberPerBrush.push_back(0);
}

void parsed_brushes::free_memory() {
	sides.shrink_to_fit();
	firstSideNumberPerBrush.shrink_to_fit();
}

bool parsed_brushes::empty() const noexcept {
	return sides.empty();
}

std::size_t parsed_brushes::size() const noexcept {
	return firstSideNumberPerBrush.size() - 1;
}

parsed_brushes_iterator parsed_brushes::cbegin() const noexcept {
	parsed_brushes_iterator it;
	it.parsedBrushesContainer = this;
	it.entityLocalBrushNumber = 0;
	return it;
}

parsed_brushes_iterator parsed_brushes::cend() const noexcept {
	parsed_brushes_iterator it;
	it.parsedBrushesContainer = this;
	it.entityLocalBrushNumber = size();
	return it;
}

parsed_brushes_iterator parsed_brushes::begin() const noexcept {
	return cbegin();
}

parsed_brushes_iterator parsed_brushes::end() const noexcept {
	return cend();
}

std::optional<std::u8string_view>
map_entity_parser::parse_quoted_string(std::u8string& quotedStringBuffer
) noexcept {
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
				|| nextSpecial >= remainingInput.size() - 1) [[unlikely]] {
				return std::nullopt;
			}
			isBackslash = remainingInput[nextSpecial] == u8'\\';
			escapedCharacter = remainingInput[nextSpecial + 1];
			foundEscapeSequence = isBackslash
				&& (escapedCharacter == u8'\\' || escapedCharacter == u8'"'
				);
		} while (isBackslash && !foundEscapeSequence);

		std::u8string_view const untilSpecial = remainingInput.substr(
			1, nextSpecial - 1
		);

		remainingInput.remove_prefix(nextSpecial + 1);
		if (!foundEscapeSequence && quotedStringBuffer.empty()) [[likely]] {
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

bool map_entity_parser::parse_key_values(
	std::vector<entity_key_value>& keyValues
) noexcept {
	while (remainingInput.starts_with(u8'"')) {
		auto maybeKey = parse_quoted_string(quotedStringBufferForKey);
		if (!maybeKey || maybeKey.value().empty()) [[unlikely]] {
			return false;
		}

		skip_whitespace_and_comments(remainingInput);

		auto maybeValue = parse_quoted_string(quotedStringBufferForValue);
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

wad_texture_name map_entity_parser::parse_texture_name() noexcept {
	wad_texture_name textureName{};

	bool const isQuotedTextureName = remainingInput.starts_with(u8'"');
	if (isQuotedTextureName) [[unlikely]] {
		auto maybeValue = parse_quoted_string(quotedStringBufferForValue);
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

bool map_entity_parser::parse_sides(std::vector<parsed_side>& out
) noexcept {
	while (!remainingInput.starts_with(u8'}')) {
		// Read 3-point plane definition for brush side
		std::array<double3_array, 3> planePoints;
		for (std::size_t pointIndex = 0; pointIndex != 3; ++pointIndex) {
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

bool map_entity_parser::parse_brushes(parsed_brushes& out) noexcept {
	while (try_to_skip_one(remainingInput, u8'{')) {
		skip_whitespace_and_comments(remainingInput);

		std::size_t const numSidesBefore = out.sides.size();
		if (!parse_sides(out.sides)) [[unlikely]] {
			return false;
		}

		std::size_t const numSidesAdded = out.sides.size() - numSidesBefore;

		if (numSidesAdded < 4) [[unlikely]] {
			// Less than 4 sides can't make a concave brush
			return false;
		}

		if (numSidesAdded >= max_brushes_per_entity) {
			return false;
		}

		out.firstSideNumberPerBrush.push_back(
			out.firstSideNumberPerBrush.back() + numSidesAdded
		);

		if (!try_to_skip_one(remainingInput, u8'}')) [[unlikely]] {
			return false;
		}
		skip_whitespace_and_comments(remainingInput);
	}

	return true;
}

bool map_entity_parser::isOldFormat() const noexcept {
	std::u8string_view input{ allInput };

	constexpr std::u8string_view worldspawnClassnameValue
		= u8"\"worldspawn\"";
	std::size_t worldspawnClassnameValueStart = input.find(
		worldspawnClassnameValue
	);
	if (worldspawnClassnameValueStart == std::u8string_view::npos) {
		return false;
	}
	input.remove_prefix(
		worldspawnClassnameValueStart + worldspawnClassnameValue.length()
	);

	constexpr std::u8string_view mapVersionKey = u8"\"mapversion\"";
	std::size_t mapVersionKeyStart = input.find(mapVersionKey);
	if (mapVersionKeyStart == std::u8string_view::npos) {
		return true;
	}
	input.remove_prefix(mapVersionKeyStart + mapVersionKey.length());

	return !input.contains(u8"\"220\"");
}

map_entity_parser::map_entity_parser(std::u8string_view str) noexcept :
	allInput(str), remainingInput{ str } { }

parse_entity_outcome map_entity_parser::parse_entity(parsed_entity& ent
) noexcept {
	ent.clear();

	skip_whitespace_and_comments(remainingInput);

	if (remainingInput.empty()
		|| (remainingInput.length() == 1 && remainingInput[0] == u8'\0')) {
		ent.free_memory();
		return parse_entity_outcome::reached_end;
	}

	// Entity start
	if (try_to_skip_one(remainingInput, u8'{')) [[likely]] {
		skip_whitespace_and_comments(remainingInput);

		if (parse_key_values(ent.keyValues) && parse_brushes(ent.brushes)
			&& try_to_skip_one(remainingInput, u8'}') // Entity end
		) [[likely]] {
			ent.entityNumber = numParsedEntities++;
			return parse_entity_outcome::entity_parsed;
		}
	}

	if (isOldFormat()) {
		return parse_entity_outcome::not_valve220_map_format;
	}
	return parse_entity_outcome::bad_input;
}

std::u8string_view map_entity_parser::remaining_input() noexcept {
	return remainingInput;
}
