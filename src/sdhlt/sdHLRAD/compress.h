#pragma once

#include "cmdlib.h" //--vluzacn


#ifdef WORDS_BIGENDIAN
#error
#endif

void compress_compatability_test (void);

extern const size_t unused_size; // located at the end of a block

typedef enum
{
	FLOAT32 = 0,
	FLOAT16,
	FLOAT8,
	float_type_count
}
float_type;

extern const char *float_type_string[];

extern const size_t float_size[];

enum vector_type
{
	VECTOR96 = 0,
	VECTOR48,
	VECTOR32,
	VECTOR24,
	vector_type_count
};

extern const char *vector_type_string[];

extern const size_t vector_size[];


void float_compress(float_type t, void *s, const float *f);
void float_decompress(float_type t, const void *s, float *f);
void vector_compress(vector_type t, void *s, float f1, float f2, float f3);
void vector_decompress(vector_type t, const void *s, float *f1, float *f2, float *f3);
