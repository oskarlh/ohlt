#pragma once

#include <filesystem>

#include "cmdlib.h" //--vluzacn

extern time_t   getfiletime(const char* const filename);
extern long     getfilesize(const char* const filename);
extern long     getfiledata(const char* const filename, char* buffer, const int buffersize);
extern int      q_filelength(FILE* f);

extern FILE*    SafeOpenWrite(const char* const filename);
extern FILE*    SafeOpenRead(const std::filesystem::path& filename);
extern void     SafeRead(FILE* f, void* buffer, int count);
extern void     SafeWrite(FILE* f, const void* const buffer, int count);

extern int      LoadFile(const std::filesystem::path&, char** bufferptr);
extern void     SaveFile(const char* const filename, const void* const buffer, int count);
