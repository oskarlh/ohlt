#pragma once

#include <filesystem>
#include <fstream>
#include <string>

#include "cmdlib.h"

// Success, number of elements, elements
std::tuple<bool, std::size_t, std::unique_ptr<std::byte[]>>
// TODO: Add restrictions to make sure Element is a POD-like type
read_binary_file(const std::filesystem::path& filePath);

std::optional<std::u8string> read_utf8_file(const std::filesystem::path& filePath, bool windowsLineEndingsToUnix);
std::filesystem::path get_path_to_directory_with_executable(char** argv0Fallback);

int      q_filelength(FILE* f);
FILE*    SafeOpenWrite(const char* const filename);
FILE*    SafeOpenRead(const std::filesystem::path& filename);
void     SafeRead(FILE* f, void* buffer, int count);
void     SafeWrite(FILE* f, const void* const buffer, int count);
void     SaveFile(const char* const filename, const void* const buffer, int count);
