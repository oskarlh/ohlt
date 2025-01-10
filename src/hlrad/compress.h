#pragma once

#include "cmdlib.h" //--vluzacn


void compress_compatability_test (void);

constexpr std::size_t unused_size = 3; // located at the end of a block

enum class float_type {
	float32 = 0,
	float16,
	float8
};
constexpr std::size_t float_type_count = std::size_t(float_type::float8) + 1;
template<class Num> constexpr bool is_valid_float_type(Num num) {
	return num >= Num(float_type::float32) && num <= Num(float_type::float8);
}

extern const std::array<std::u8string_view, float_type_count> float_type_string;

extern const size_t float_size[];

enum class vector_type {
	vector96 = 0,
	vector48,
	vector32,
	vector24,
};
constexpr std::size_t vector_type_count = std::size_t(vector_type::vector24) + 1;
template<class Num> constexpr bool is_valid_vector_type(Num num) {
	return num >= Num(vector_type::vector96) && num <= Num(vector_type::vector24);
}

extern const char *vector_type_string[];

extern const size_t vector_size[];


void float_compress(float_type t, void *s, float f);
float float_decompress(float_type t, const std::byte *s);
void vector_compress(vector_type t, void *s, float f1, float f2, float f3);
void vector_decompress(vector_type t, const void *s, float *f1, float *f2, float *f3);
