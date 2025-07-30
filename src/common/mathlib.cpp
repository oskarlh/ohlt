#include "mathlib.h"

std::uint16_t float_to_half(float v) {
	static_assert(std::numeric_limits<float>::is_iec559);

	std::uint32_t i = *((std::uint32_t*) &v);
	std::uint32_t e = (i >> 23) & 0x00ff;
	std::uint32_t m = i & 0x007f'ffff;
	std::uint16_t half;

	if (e <= 127 - 15) {
		half = ((m | 0x0080'0000) >> (127 - 14 - e)) >> 13;
	} else {
		half = (i >> 13) & 0x3fff;
	}

	half |= (i >> 16) & 0xc000;

	return half;
}

float half_to_float(std::uint16_t half) {
	static_assert(std::numeric_limits<float>::is_iec559);

	std::uint32_t f = (half << 16) & 0x8000'0000;
	std::uint32_t em = half & 0x7fff;

	if (em > 0x03ff) {
		f |= (em << 13) + ((127 - 15) << 23);
	} else {
		std::uint32_t m = em & 0x03ff;

		if (m != 0) {
			std::uint32_t e = (em >> 10) & 0x1f;

			while ((m & 0x0400) == 0) {
				m <<= 1;
				e--;
			}

			m &= 0x3ff;
			f |= ((e + (127 - 14)) << 23) | (m << 13);
		}
	}

	return *((float*) &f);
}
