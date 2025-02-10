#pragma once
#include "cmdlib.h"
#include "developer_level.h"
#include "mathtypes.h"
#include "messages.h"

#include <filesystem>

//
// log.c globals
//

extern char const * g_Program;
extern char g_Mapname[_MAX_PATH];
extern char g_Wadpath[_MAX_PATH]; // seedee

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

#ifdef _DEBUG
#define IfDebug(x) (x)
#else
#define IfDebug(x)
#endif

extern void FORMAT_PRINTF(1, 2)
	PrintConsole(char const * const message, ...);
extern void FORMAT_PRINTF(1, 2) Verbose(char const * const message, ...);
extern void FORMAT_PRINTF(1, 2) Log(char const * const message, ...);
extern void FORMAT_PRINTF(1, 2) Error(char const * const error, ...);
extern void FORMAT_PRINTF(2, 3)
	Fatal(assume_msgs msgid, char const * const error, ...);
extern void FORMAT_PRINTF(1, 2) Warning(char const * const warning, ...);

extern void FORMAT_PRINTF(1, 2) PrintOnce(char const * const message, ...);

extern void LogStart(int const argc, char** argv);
extern void LogEnd();
extern void Banner();

extern void LogTimeElapsed(float elapsed_time);

// Note: The first element in argv is skipped since that's usually
// the executable path
void log_arguments(int argc, char** argv);

// Should be in hlassert.h, but well so what
extern void hlassume(bool exp, assume_msgs msgid);
