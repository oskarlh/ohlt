#pragma once
#define MODIFICATIONS_STRING "Submit detailed bug reports to (" PROJECT_ISSUE_TRACKER ")\n"

#define VERSION_STRING "v" PROJECT_VERSION

#define PROJECT_NAME_AND_VERSION PROJECT_NAME " v" PROJECT_VERSION


#if !defined (VERSION_LINUX) && !defined (VERSION_MACOS) && !defined (VERSION_OTHER) && !defined (VERSION_WINDOWS)
#error "You must define one of these in the settings of each project: VERSION_LINUX, VERSION_MACOS, VERSION_OTHER, VERSION_WINDOWS. The most likely cause is that you didn't use CMake correctly."
#endif


//=====================================================================
// AJM: Different features of the tools can be undefined here
//      these are not officially beta tested, but seem to work okay

// ZHLT_* features are spread across more than one tool. Hence, changing
//      one of these settings probably means recompiling the whole set


// tool specific settings below only mean a recompile of the tool affected

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


#ifdef SYSTEM_POSIX
extern char*    strlwr(char* string);
#endif


std::u8string_view filename_in_file_path_string(std::u8string_view filePathString);

bool a_starts_with_b_ignoring_ascii_character_case_differences(std::u8string_view string, std::u8string_view substring);
bool a_contains_b_ignoring_ascii_character_case_differences(std::u8string_view string, std::u8string_view substring);

extern bool FORMAT_PRINTF(3,4) safe_snprintf(char* const dest, const size_t count, const char* const args, ...);
extern bool     safe_strncpy(char* const dest, const char* const src, const size_t count);
inline bool safe_strncpy(char8_t* const dest, const char* const src, const size_t count) {
	return safe_strncpy((char*) dest, (char*) src, count);
}
inline bool safe_strncpy(char* const dest, const char8_t* const src, const size_t count) {
	return safe_strncpy((char*) dest, (char*) src, count);
}
inline bool safe_strncpy(char8_t* const dest, const char8_t* const src, const size_t count) {
	return safe_strncpy((char*) dest, (char*) src, count);
}
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
