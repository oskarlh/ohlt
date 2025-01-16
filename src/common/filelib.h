#pragma once

#include "cmdlib.h"
#include "legacy_character_encodings.h"

#include <filesystem>
#include <fstream>
#include <string>

// Success, number of elements, elements
std::tuple<bool, std::size_t, std::unique_ptr<std::byte[]>>
// TODO: Add restrictions to make sure Element is a POD-like type
read_binary_file(std::filesystem::path const & filePath);

std::optional<std::u8string> read_utf8_file(
	std::filesystem::path const & filePath, bool windowsLineEndingsToUnix,
	std::optional<legacy_encoding> legacyEncoding = std::nullopt,
	bool forceLegacyEncoding = false
);
std::filesystem::path
get_path_to_directory_with_executable(char** argv0Fallback);

std::u8string_view
filename_in_file_path_string(std::u8string_view filePathString);
std::filesystem::path
filename_in_file_path_string_as_path(std::u8string_view filePathString);

int q_filelength(FILE* f);
FILE* SafeOpenWrite(char const * const filename);
FILE* SafeOpenRead(std::filesystem::path const & filename);
void SafeRead(FILE* f, void* buffer, int count);
void SafeWrite(FILE* f, void const * const buffer, int count);
void SaveFile(
	char const * const filename, void const * const buffer, int count
);
