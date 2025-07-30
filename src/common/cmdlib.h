#pragma once

#include "win32fix.h"

#include <stdarg.h>

extern bool FORMAT_PRINTF(3, 4) safe_snprintf(
	char* const dest, size_t const count, char const * const args, ...
);
extern bool
safe_strncpy(char* const dest, char const * const src, size_t const count);

inline bool safe_strncpy(
	char8_t* const dest, char const * const src, size_t const count
) {
	return safe_strncpy((char*) dest, (char*) src, count);
}

inline bool safe_strncpy(
	char* const dest, char8_t const * const src, size_t const count
) {
	return safe_strncpy((char*) dest, (char*) src, count);
}

inline bool safe_strncpy(
	char8_t* const dest, char8_t const * const src, size_t const count
) {
	return safe_strncpy((char*) dest, (char*) src, count);
}

extern bool
safe_strncat(char* const dest, char const * const src, size_t const count);

extern void DefaultExtension(char* path, char const * extension);

extern void ExtractFile(char const * const path, char* dest);
extern void ExtractFilePath(char const * const path, char* dest);
