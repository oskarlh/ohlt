#pragma once

#include "external_types/external_types.h"
#include "legacy_character_encodings.h"

#include <filesystem>
#include <string>

// Success, number of elements, elements
std::tuple<bool, std::size_t, std::unique_ptr<std::byte[]>>
// TODO: Add restrictions to make sure Element is a POD-like type
read_binary_file(std::filesystem::path const & filePath);

std::optional<std::u8string> read_utf8_file(
	std::filesystem::path const & filePath,
	bool windowsLineEndingsToUnix,
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

constexpr std::array<std::u8string_view, NUM_HULLS> brushFileExtensions{
	u8".b0", u8".b1", u8".b2", u8".b3"
};
constexpr std::array<std::u8string_view, NUM_HULLS> polyFileExtensions{
	u8".p0", u8".p1", u8".p2", u8".p3"
};
constexpr std::array<std::u8string_view, NUM_HULLS> surfaceFileExtensions{
	u8"_surface0.pts",
	u8"_surface1.pts",
	u8"_surface2.pts",
	u8"_surface3.pts"
};

// Meant for strings from .map files, string such as "models/barney.mdl" or
// "model\\barney.mdl"
std::filesystem::path
parse_relative_file_path(std::u8string_view relativeFilePath);
