#pragma once

#include <string_view>
#include <type_traits>
#include <utility>

void compress_compatability_test(void);

constexpr std::size_t unused_size = 3; // located at the end of a block

enum class float_type {
	float32 = 0,
	float16,
	float8
};
constexpr std::size_t float_type_count = std::size_t(float_type::float8)
	+ 1;

constexpr bool is_valid_float_type(std::underlying_type_t<float_type> num) {
	return num >= std::to_underlying(float_type::float32)
		&& num <= std::to_underlying(float_type::float8);
}

extern std::array<std::u8string_view, float_type_count> const
	float_type_string;

extern size_t const float_size[];

enum class vector_type {
	vector96 = 0,
	vector48,
	vector32,
	vector24,
};
constexpr std::size_t vector_type_count = std::size_t(vector_type::vector24)
	+ 1;

template <class Num>
constexpr bool is_valid_vector_type(Num num) {
	return num >= Num(vector_type::vector96)
		&& num <= Num(vector_type::vector24);
}

extern char const * vector_type_string[];

extern size_t const vector_size[];

void float_compress(float_type t, void* s, float f);
float float_decompress(float_type t, std::byte const * s);
void vector_compress(vector_type t, void* s, float f1, float f2, float f3);
void vector_decompress(
	vector_type t, void const * s, float* f1, float* f2, float* f3
);
