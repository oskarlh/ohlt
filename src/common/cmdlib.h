#pragma once

#include "mathtypes.h"
#include "win32fix.h"

#include <algorithm>
#include <filesystem>
#include <ranges>
#include <stdarg.h>
#include <stdint.h>
#include <string>

#ifdef SYSTEM_WIN32
#define SYSTEM_SLASH_CHAR '\\'
#define SYSTEM_SLASH_STR  "\\"
#endif
#ifdef SYSTEM_POSIX
#define SYSTEM_SLASH_CHAR '/'
#define SYSTEM_SLASH_STR  "/"
#endif

// the dec offsetof macro doesn't work very well...
#define myoffsetof(type, identifier)	((size_t) & ((type*) 0)->identifier)
#define sizeofElement(type, identifier) (sizeof((type*) 0)->identifier)

bool a_starts_with_b_ignoring_ascii_character_case_differences(
	std::u8string_view string, std::u8string_view substring
);
bool a_contains_b_ignoring_ascii_character_case_differences(
	std::u8string_view string, std::u8string_view substring
);

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
extern bool TerminatedString(char const * buffer, int const size);

extern char* FlipSlashes(char* string);

extern int CheckParm(char* check);

extern void DefaultExtension(char* path, char const * extension);
extern void DefaultPath(char* path, char* basepath);
extern void StripFilename(char* path);
extern void StripExtension(char* path);

extern void ExtractFile(char const * const path, char* dest);
extern void ExtractFilePath(char const * const path, char* dest);
extern void ExtractFileBase(char const * const path, char* dest);
extern void ExtractFileExtension(char const * const path, char* dest);
