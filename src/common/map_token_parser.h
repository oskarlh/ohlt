#pragma once

#include "entity_key_value.h"
#include "mathtypes.h"
#include "wad_texture_name.h"

#include <optional>
#include <string>

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
