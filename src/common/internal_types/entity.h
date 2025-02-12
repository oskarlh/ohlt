#pragma once
#include "../entity_key_value.h"
#include "../hashing.h"
#include "../mathtypes.h"
#include "various.h"

#include <cstdint>
#include <span>

struct entity_t final {
	float3_array origin;
	brush_count firstBrush;
	entity_local_brush_count numbrushes;
	std::vector<entity_key_value> keyValues;
};

namespace std {
	template <>
	struct hash<entity_t> {
		constexpr std::size_t operator()(entity_t const & ent
		) const noexcept {
			return hash_multiple(
				ent.origin, ent.firstBrush, ent.numbrushes, ent.keyValues
			);
		}
	};
} // namespace std
