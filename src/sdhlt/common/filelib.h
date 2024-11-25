#pragma once

#include <filesystem>

#include "cmdlib.h" //--vluzacn

std::filesystem::path get_path_to_directory_with_executable(char** argv0Fallback);

int      q_filelength(FILE* f);
FILE*    SafeOpenWrite(const char* const filename);
FILE*    SafeOpenRead(const std::filesystem::path& filename);
void     SafeRead(FILE* f, void* buffer, int count);
void     SafeWrite(FILE* f, const void* const buffer, int count);
int      LoadFile(const std::filesystem::path&, char** bufferptr);
void     SaveFile(const char* const filename, const void* const buffer, int count);
