#pragma once
#include "developer_level.h"
#include "messages.h"
#include "win32fix.h"

#include <filesystem>

//
// log.c globals
//

extern std::u8string g_Program;
extern std::filesystem::path
	g_Mapname; // This is the path to the .map/.bsp file, but without the
               // file extension
extern std::filesystem::path g_Wadpath; // This is just the mod folder...

extern developer_level g_developer;
extern bool g_verbose;
extern bool g_log;

//
// log.c Functions
//

extern void ResetTmpFiles();
extern void ResetLog();
extern void ResetErrorLog();
extern void CheckForErrorLog();

extern void OpenLog();
extern void CloseLog();
extern void WriteLog(char const * const message);

extern void CheckFatal();

extern void FORMAT_PRINTF(2, 3)
	Developer(developer_level level, char const * const message, ...);

std::filesystem::path path_to_temp_file_with_extension(
	std::filesystem::path mapBasePath, std::u8string_view extension
);

void FlushConsole();
void FORMAT_PRINTF(1, 2) PrintConsole(char const * const message, ...);
void FORMAT_PRINTF(1, 2) Verbose(char const * const message, ...);
void FORMAT_PRINTF(1, 2) Log(char const * const message, ...);
void FORMAT_PRINTF(1, 2) Error(char const * const error, ...);
void FORMAT_PRINTF(2, 3)
	Fatal(assume_msgs msgid, char const * const error, ...);
void FORMAT_PRINTF(1, 2) Warning(char const * const warning, ...);

void FORMAT_PRINTF(1, 2) PrintOnce(char const * const message, ...);

void LogStart(int const argc, char** argv);
void LogEnd();
void Banner();

void LogTimeElapsed(float elapsed_time);

// Note: The first element in argv is skipped since that's usually
// the executable path
void log_arguments(int argc, char** argv);

// Should be in hlassert.h, but well so what
void hlassume(bool exp, assume_msgs msgid);
