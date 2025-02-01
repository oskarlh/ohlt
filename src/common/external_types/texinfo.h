#pragma once

#include "mathtypes.h"

#include <array>
#include <cstdint>
#include <utility>

// Flags for texinfo_t
enum class texinfo_flags : std::uint32_t {
	NO_FLAGS = 0,

	// Sky or slime or null, no lightmap or 256 subdivision
	TEX_SPECIAL = 1
};

constexpr texinfo_flags operator~(texinfo_flags a) noexcept {
	return texinfo_flags{ ~std::to_underlying(a) };
}

constexpr texinfo_flags
operator&(texinfo_flags a, texinfo_flags b) noexcept {
	return texinfo_flags{ std::to_underlying(a) & std::to_underlying(b) };
}

constexpr texinfo_flags&
operator&=(texinfo_flags& a, texinfo_flags b) noexcept {
	return a = (a & b);
}

constexpr texinfo_flags
operator|(texinfo_flags& a, texinfo_flags b) noexcept {
	return texinfo_flags{ std::to_underlying(a) | std::to_underlying(b) };
}

constexpr texinfo_flags&
operator|=(texinfo_flags& a, texinfo_flags b) noexcept {
	return a = (a | b);
}

struct tex_vec {
	float3_array xyz;
	float offset;
	constexpr auto operator<=>(tex_vec const &) const noexcept = default;
};

struct texinfo_t {
	// [s/t] unit vectors in world space.
	// [i][3] is the s/t offset relative to the origin.
	// float s = dot_product(3dPoint, vecs[0].xyz) + vecs[0].offset;
	// float t = dot_product(3dPoint, vecs[1].xyz) + vecs[1].offset;
	// In Quake and Xash3d, the type `float vecs[2][4];` is used instead,
	// with `vecs[i][3]` being equivalent of `vecs[i].offset`
	std::array<tex_vec, 2> vecs;

	std::int32_t miptex;
	texinfo_flags flags;

	constexpr bool has_special_flag() const noexcept {
		return (flags & texinfo_flags::TEX_SPECIAL)
			!= texinfo_flags::NO_FLAGS;
	}

	constexpr void set_special_flag(bool on) noexcept {
		if (on) {
			flags |= texinfo_flags::TEX_SPECIAL;
		} else {
			flags &= ~(texinfo_flags::TEX_SPECIAL);
		}
	}
};
