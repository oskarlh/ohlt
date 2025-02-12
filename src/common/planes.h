#pragma once

#include "mathtypes.h"

#include <array>

struct mapplane_t final {
	double3_array normal;
	double3_array origin;
	double dist; // Distance from the origin
	planetype type;
};

namespace std {
	template <>
	struct hash<mapplane_t> {
		constexpr std::size_t operator()(mapplane_t const & mapPlane
		) const noexcept {
			return hash_multiple(
				mapPlane.normal,
				mapPlane.origin,
				mapPlane.dist,
				mapPlane.type
			);
		}
	};
} // namespace std
