#pragma once
#include "cmdlib.h"
#include "developer_level.h"
#include "mathtypes.h"
#include "messages.h"


//
// log.c globals
//

extern const char*    g_Program;
extern char     g_Mapname[_MAX_PATH];
extern char     g_Wadpath[_MAX_PATH]; //seedee

extern developer_level_t g_developer;
extern bool          g_verbose;
extern bool          g_log;
extern unsigned long g_clientid;                           // Client id of this program
extern unsigned long g_nextclientid;                       // Client id of next client to spawn from this server

//
// log.c Functions
//

extern void     ResetTmpFiles();
extern void     ResetLog();
extern void     ResetErrorLog();
extern void     CheckForErrorLog();

extern void OpenLog(int clientid);
extern void CloseLog();
extern void     WriteLog(const char* const message);

extern void     CheckFatal();

extern void FORMAT_PRINTF(2,3) Developer(developer_level_t level, const char* const message, ...);

#ifdef _DEBUG
#define IfDebug(x) (x)
#else
#define IfDebug(x)
#endif

extern const char * Localize (const char *s);
extern void LoadLangFile (const char *name, const char *programpath);
extern int InitConsole(int argc, char **argv);
extern void FORMAT_PRINTF(1,2) PrintConsole(const char* const message, ...);
extern void FORMAT_PRINTF(1,2) Verbose(const char* const message, ...);
extern void FORMAT_PRINTF(1,2) Log(const char* const message, ...);
extern void FORMAT_PRINTF(1,2) Error(const char* const error, ...);
extern void FORMAT_PRINTF(2,3) Fatal(assume_msgs msgid, const char* const error, ...);
extern void FORMAT_PRINTF(1,2) Warning(const char* const warning, ...);

extern void FORMAT_PRINTF(1,2) PrintOnce(const char* const message, ...);

extern void     LogStart(const int argc, char** argv);
extern void     LogEnd();
extern void     Banner();

extern void     LogTimeElapsed(float elapsed_time);

// Should be in hlassert.h, but well so what
extern void     hlassume(bool exp, assume_msgs msgid);

