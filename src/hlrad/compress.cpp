#include "compress.h"

#include "log.h"

#include <algorithm>
#include <array>
#include <bit>
#include <string_view>

using namespace std::literals;

std::array<std::u8string_view, float_type_count> const float_type_string = {
	u8"32bit"sv, u8"16bit"sv, u8"8bit"sv
};

size_t const float_size[float_type_count] = { 4u, 2u, 1u };

char const *(vector_type_string[vector_type_count]
) = { "96bit", "48bit", "32bit", "24bit" };

size_t const vector_size[vector_type_count] = { 12u, 6u, 4u, 3u };

void fail() {
	Error("HLRAD_TRANSFERDATA_COMPRESS compatability test failed.");
}

void compress_compatability_test() {
	std::array<std::byte, 16> v{};
	if (sizeof(char) != 1 || sizeof(unsigned int) != 4
	    || sizeof(float) != 4) {
		fail();
	}
	*(float*) (&v[1]) = 0.123f;
	if (*(unsigned int*) v.data() != 4'226'247'936u
	    || *(unsigned int*) (&v[1]) != 1'039'918'957u) {
		fail();
	}
	*(float*) (&v[1]) = -58;
	if (*(unsigned int*) v.data() != 1'744'830'464u
	    || *(unsigned int*) (&v[1]) != 3'261'595'648u) {
		fail();
	}
	float f[5] = { 0.123f, 1.f, 0.f, 0.123f, 0.f };
	std::ranges::fill(v, std::byte(~0));
	vector_compress(vector_type::vector24, v.data(), f[0], f[1], f[2]);
	float_compress(float_type::float16, &v[6], f[3]);
	float_compress(float_type::float16, &v[4], f[4]);
	if (((unsigned int*) v.data())[0] != 4'286'318'595u
	    || ((unsigned int*) v.data())[1] != 3'753'771'008u) {
		fail();
	}
	f[3] = float_decompress(float_type::float16, &v[6]);
	f[4] = float_decompress(float_type::float16, &v[4]);
	vector_decompress(vector_type::vector24, v.data(), &f[0], &f[1], &f[2]);
	constexpr float ans[5] = {
		0.109375f, 1.015625f, 0.015625f, 0.123001f, 0.000000f
	};
	for (std::size_t i = 0; i < 5; ++i) {
		if (f[i] - ans[i] > 0.00001f || f[i] - ans[i] < -0.00001f) {
			fail();
		}
	}
}

constexpr std::uint32_t
bitget(std::uint32_t i, std::uint32_t start, std::uint32_t end) {
	return (i & ~(~0u << end)) >> start;
}

constexpr std::uint32_t bitput(std::uint32_t i, std::uint32_t start) {
	return i << start;
}

constexpr std::uint32_t
bitclr(std::uint32_t i, std::uint32_t start, std::uint32_t end) {
	return i & (~(~0u << start) | (~0u << end));
}

constexpr std::uint32_t float_iswrong(std::uint32_t i) {
	return i >= 0x7F80'0000u;
}

constexpr std::uint32_t float_istoobig(std::uint32_t i) {
	return i >= 0x4000'0000u;
}

constexpr std::uint32_t float_istoosmall(std::uint32_t i) {
	return i < 0x3080'0000u;
}

void float_compress(float_type t, void* s, float f) {
	std::uint32_t* m = (std::uint32_t*) s;
	std::uint32_t p = std::bit_cast<std::uint32_t>(f);
	switch (t) {
		case float_type::float32:
			m[0] = p;
			break;
		case float_type::float16:
			m[0] = bitclr(m[0], 0, 16);
			if (float_iswrong(p))
				;
			else if (float_istoobig(p)) {
				m[0] |= bitget(~0u, 0, 16);
			} else if (float_istoosmall(p))
				;
			else {
				m[0] |= bitget(p, 12, 28);
			}
			break;
		case float_type::float8:
			m[0] = bitclr(m[0], 0, 8);
			if (float_iswrong(p))
				;
			else if (float_istoobig(p)) {
				m[0] |= bitget(~0u, 0, 8);
			} else if (float_istoosmall(p))
				;
			else {
				m[0] |= bitget(p, 20, 28);
			}
			break;
		default:;
	}
}

float float_decompress(float_type t, std::byte const * s) {
	std::uint32_t const * m = (std::uint32_t const *) s;
	switch (t) {
		case float_type::float32:
			return std::bit_cast<float>(m[0]);
		case float_type::float16:
			if (bitget(m[0], 0, 16) == 0) {
				return 0;
			}
			return std::bit_cast<float>(
				bitput(1, 11) | bitput(bitget(m[0], 0, 16), 12)
				| bitput(3, 28)
			);
		default:
		case float_type::float8:
			if (bitget(m[0], 0, 8) == 0) {
				return 0;
			}
			return std::bit_cast<float>(
				bitput(1, 19) | bitput(bitget(m[0], 0, 8), 20)
				| bitput(3, 28)
			);
	}
	// This shouldn't happen...
	return 0.0f;
}

void vector_compress(vector_type t, void* s, float f1, float f2, float f3) {
	unsigned int* m = (unsigned int*) s;
	unsigned int const p1 = std::bit_cast<unsigned int>(f1);
	unsigned int const p2 = std::bit_cast<unsigned int>(f2);
	unsigned int const p3 = std::bit_cast<unsigned int>(f3);
	unsigned int max, i1, i2, i3;
	switch (t) {
		case vector_type::vector96:
			m[0] = p1;
			m[1] = p2;
			m[2] = p3;
			break;
		case vector_type::vector48:
			if (float_iswrong(p1) || float_iswrong(p2)
			    || float_iswrong(p3)) {
				break;
			}
			m[0] = 0, m[1] = bitclr(m[1], 0, 16);
			if (float_istoobig(p1)) {
				m[0] |= bitget(~0u, 0, 16);
			} else if (float_istoosmall(p1))
				;
			else {
				m[0] |= bitget(p1, 12, 28);
			}
			if (float_istoobig(p2)) {
				m[0] |= bitput(bitget(~0u, 0, 16), 16);
			} else if (float_istoosmall(p2))
				;
			else {
				m[0] |= bitput(bitget(p2, 12, 28), 16);
			}
			if (float_istoobig(p3)) {
				m[1] |= bitget(~0u, 0, 16);
			} else if (float_istoosmall(p3))
				;
			else {
				m[1] |= bitget(p3, 12, 28);
			}
			break;
		case vector_type::vector32:
		case vector_type::vector24:
			if (float_iswrong(p1) || float_iswrong(p2)
			    || float_iswrong(p3)) {
				max = i1 = i2 = i3 = 0;
			} else {
				max = p1 > p2 ? (p1 > p3 ? p1 : p3) : (p2 > p3 ? p2 : p3);
				max = float_istoobig(max)   ? 0x7F
					: float_istoosmall(max) ? 0x60
											: bitget(max, 23, 31);
				i1 = float_istoobig(p1)
					? ~0u
					: (bitget(p1, 0, 23) | bitput(1, 23))
						>> ((1 + max - bitget(p1, 23, 31)) % 32);
				i2 = float_istoobig(p2)
					? ~0u
					: (bitget(p2, 0, 23) | bitput(1, 23))
						>> ((1 + max - bitget(p2, 23, 31)) % 32);
				i3 = float_istoobig(p3)
					? ~0u
					: (bitget(p3, 0, 23) | bitput(1, 23))
						>> ((1 + max - bitget(p3, 23, 31)) % 32);
			}
			if (t == vector_type::vector32) {
				m[0] = 0 | bitput(bitget(i1, 14, 23), 0)
					| bitput(bitget(i2, 14, 23), 9)
					| bitput(bitget(i3, 14, 23), 18)
					| bitput(bitget(max, 0, 5), 27);
			} else {
				m[0] = bitclr(m[0], 0, 24) | bitput(bitget(i1, 17, 23), 0)
					| bitput(bitget(i2, 17, 23), 6)
					| bitput(bitget(i3, 17, 23), 12)
					| bitput(bitget(max, 0, 5), 18);
			}
			break;
		default:;
	}
}

void vector_decompress(
	vector_type t, void const * s, float* f1, float* f2, float* f3
) {
	unsigned int const * m = (unsigned int const *) s;
	unsigned int* p1 = (unsigned int*) f1;
	unsigned int* p2 = (unsigned int*) f2;
	unsigned int* p3 = (unsigned int*) f3;
	switch (t) {
		case vector_type::vector96:
			*p1 = m[0];
			*p2 = m[1];
			*p3 = m[2];
			break;
		case vector_type::vector48:
			if (bitget(m[0], 0, 16) == 0) {
				*p1 = 0;
			} else {
				*p1 = bitput(1, 11) | bitput(bitget(m[0], 0, 16), 12)
					| bitput(3, 28);
			}
			if (bitget(m[0], 16, 32) == 0) {
				*p2 = 0;
			} else {
				*p2 = bitput(1, 11) | bitput(bitget(m[0], 16, 32), 12)
					| bitput(3, 28);
			}
			if (bitget(m[1], 0, 16) == 0) {
				*p3 = 0;
			} else {
				*p3 = bitput(1, 11) | bitput(bitget(m[1], 0, 16), 12)
					| bitput(3, 28);
			}
			break;
		case vector_type::vector32:
		case vector_type::vector24:
			float f;
			if (t == vector_type::vector32) {
				*p1 = bitput(1, 13) | bitput(bitget(m[0], 0, 9), 14)
					| bitput(bitget(m[0], 27, 32), 23) | bitput(3, 28);
				*p2 = bitput(1, 13) | bitput(bitget(m[0], 9, 18), 14)
					| bitput(bitget(m[0], 27, 32), 23) | bitput(3, 28);
				*p3 = bitput(1, 13) | bitput(bitget(m[0], 18, 27), 14)
					| bitput(bitget(m[0], 27, 32), 23) | bitput(3, 28);
				*((unsigned int*) &f) = bitput(bitget(m[0], 27, 32), 23)
					| bitput(3, 28);
			} else {
				*p1 = bitput(1, 16) | bitput(bitget(m[0], 0, 6), 17)
					| bitput(bitget(m[0], 18, 23), 23) | bitput(3, 28);
				*p2 = bitput(1, 16) | bitput(bitget(m[0], 6, 12), 17)
					| bitput(bitget(m[0], 18, 23), 23) | bitput(3, 28);
				*p3 = bitput(1, 16) | bitput(bitget(m[0], 12, 18), 17)
					| bitput(bitget(m[0], 18, 23), 23) | bitput(3, 28);
				*((unsigned int*) &f) = bitput(bitget(m[0], 18, 23), 23)
					| bitput(3, 28);
			}
			*f1 = (*f1 - f) * 2.f;
			*f2 = (*f2 - f) * 2.f;
			*f3 = (*f3 - f) * 2.f;
			break;
		default:;
	}
}
