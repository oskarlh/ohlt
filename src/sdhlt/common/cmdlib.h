#pragma once
#define MODIFICATIONS_STRING "Submit detailed bug reports to (" PROJECT_ISSUE_TRACKER ")\n"

#define VERSION_STRING "v" PROJECT_VERSION

#define PROJECT_NAME_AND_VERSION PROJECT_NAME " v" PROJECT_VERSION


#if !defined (SDHLCSG) && !defined (SDHLBSP) && !defined (SDHLVIS) && !defined (SDHLRAD) && !defined (SDRIPENT) //seedee
#error "You must define one of these in the settings of each project: SDHLCSG, SDHLBSP, SDHLVIS, SDHLRAD, SDRIPENT. The most likely cause is that you didn't use CMake correctly."
#endif
#if !defined (VERSION_LINUX) && !defined (VERSION_MACOS) && !defined (VERSION_OTHER)
#error "You must define one of these in the settings of each project: VERSION_LINUX, VERSION_MACOS, VERSION_OTHER. The most likely cause is that you didn't use CMake correctly."
#endif


//=====================================================================
// AJM: Different features of the tools can be undefined here
//      these are not officially beta tested, but seem to work okay

// ZHLT_* features are spread across more than one tool. Hence, changing
//      one of these settings probably means recompiling the whole set


// tool specific settings below only mean a recompile of the tool affected

	#ifdef SYSTEM_WIN32
#define HLCSG_GAMETEXTMESSAGE_UTF8 //--vluzacn
	#endif

//=====================================================================


#include "win32fix.h"
#include "mathtypes.h"

#include <algorithm>
#include <ranges>

#include <stdarg.h>
#include <stdint.h>
#include <string>

#ifdef SYSTEM_WIN32
#define SYSTEM_SLASH_CHAR  '\\'
#define SYSTEM_SLASH_STR   "\\"
#endif
#ifdef SYSTEM_POSIX
#define SYSTEM_SLASH_CHAR  '/'
#define SYSTEM_SLASH_STR   "/"
#endif

// the dec offsetof macro doesn't work very well...
#define myoffsetof(type,identifier) ((size_t)&((type*)0)->identifier)
#define sizeofElement(type,identifier) (sizeof((type*)0)->identifier)




constexpr char8_t ascii_character_to_lowercase(char8_t c) noexcept {
	const char8_t add = (c >= u8'A' && c <= u8'Z') ? (u8'a' - u8'A') : u8'0';
	return c + add;
}
constexpr char8_t ascii_character_to_uppercase(char8_t c) noexcept {
	const char8_t subtract = (c >= u8'a' && c <= u8'z') ? (u8'a' - u8'A') : u8'0';
	return c - subtract;
}

constexpr auto ascii_characters_to_lowercase_in_utf8_string_as_view(std::u8string_view input) noexcept {
	return std::ranges::transform_view(input, ascii_character_to_lowercase);
}
constexpr auto ascii_characters_to_uppercase_in_utf8_string_as_view(std::u8string_view input) noexcept {
	return std::ranges::transform_view(input, ascii_character_to_uppercase);
}

constexpr std::u8string ascii_characters_to_lowercase_in_utf8_string(std::u8string_view input) noexcept {
	auto lowercaseView = ascii_characters_to_lowercase_in_utf8_string_as_view(input);
	return std::u8string{lowercaseView.begin(), lowercaseView.end()};
}
constexpr std::u8string ascii_characters_to_uppercase_in_utf8_string(std::u8string_view input) noexcept {
	auto uppercaseView = ascii_characters_to_uppercase_in_utf8_string_as_view(input);
	return std::u8string{uppercaseView.begin(), uppercaseView.end()};
}

template<class U8String>
constexpr void make_ascii_characters_lowercase_in_utf8_string(U8String& input) noexcept {
	for(char8_t& c : input) {
		c = ascii_character_to_lowercase(c);
	}
}
template<class U8String>
constexpr void make_ascii_characters_uppercase_in_utf8_string(U8String& input) noexcept {
	for(char8_t& c : input) {
		c = ascii_character_to_uppercase(c);
	}
}

constexpr bool strings_equal_with_ascii_case_insensitivity(std::u8string_view a, std::u8string_view b) noexcept {
	return std::ranges::equal(
		ascii_characters_to_lowercase_in_utf8_string_as_view(a),
		ascii_characters_to_lowercase_in_utf8_string_as_view(b)
	);
}

// TODO: Delete these three when we're using UTF-8 types in enough places
constexpr inline bool strings_equal_with_ascii_case_insensitivity(std::u8string_view a, std::string_view b) noexcept {
	return strings_equal_with_ascii_case_insensitivity(
		a,
		std::u8string_view((const char8_t*) b.begin(), (const char8_t*) b.end())
	);
}
constexpr inline bool strings_equal_with_ascii_case_insensitivity(std::string_view a, std::u8string_view b) noexcept {
	return strings_equal_with_ascii_case_insensitivity(
		b,
		a
	);
}
constexpr inline bool strings_equal_with_ascii_case_insensitivity(std::string_view a, std::string_view b) noexcept {
	return strings_equal_with_ascii_case_insensitivity(
		std::u8string_view((const char8_t*) a.begin(), (const char8_t*) a.end()),
		b
	);
}





#ifdef SYSTEM_POSIX
extern char*    strlwr(char* string);
#endif


bool a_starts_with_b_ignoring_ascii_character_case_differences(std::u8string_view string, std::u8string_view substring);
bool a_contains_b_ignoring_ascii_character_case_differences(std::u8string_view string, std::u8string_view substring);

extern bool FORMAT_PRINTF(3,4) safe_snprintf(char* const dest, const size_t count, const char* const args, ...);
extern bool     safe_strncpy(char* const dest, const char* const src, const size_t count);
extern bool     safe_strncat(char* const dest, const char* const src, const size_t count);
extern bool     TerminatedString(const char* buffer, const int size);

extern char*    FlipSlashes(char* string);

extern double   I_FloatTime();

extern int      CheckParm(char* check);

extern void     DefaultExtension(char* path, const char* extension);
extern void     DefaultPath(char* path, char* basepath);
extern void     StripFilename(char* path);
extern void     StripExtension(char* path);

std::u8string_view extract_filename_from_filepath_string(std::u8string_view pathString);

extern void     ExtractFile(const char* const path, char* dest);
extern void     ExtractFilePath(const char* const path, char* dest);
extern void     ExtractFileBase(const char* const path, char* dest);
extern void     ExtractFileExtension(const char* const path, char* dest);
