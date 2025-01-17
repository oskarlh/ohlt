#include "log.h"

#include "cmdlib.h"
#include "filelib.h"
#include "hlassert.h"
#include "messages.h"
#include "project_constants.h"

#ifdef SYSTEM_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "cli_option_defaults.h"
#include "scriplib.h"

char const * g_Program = "Uninitialized variable ::g_Program";
char g_Mapname[_MAX_PATH] = "Uninitialized variable ::g_Mapname";
char g_Wadpath[_MAX_PATH] = "Uninitialized variable ::g_Wadpath";

developer_level g_developer = cli_option_defaults::developer;
bool g_verbose = cli_option_defaults::verbose;
bool g_log = cli_option_defaults::log;

unsigned long g_clientid = 0;
unsigned long g_nextclientid = 0;

static FILE* CompileLog = nullptr;
static bool fatal = false;

FILE* conout = nullptr;

////////

void ResetTmpFiles() {
	if (g_log) {
		char filename[_MAX_PATH];

		safe_snprintf(filename, _MAX_PATH, "%s.bsp", g_Mapname);
		std::filesystem::remove(filename);

		safe_snprintf(filename, _MAX_PATH, "%s.inc", g_Mapname);
		std::filesystem::remove(filename);

		safe_snprintf(filename, _MAX_PATH, "%s.p0", g_Mapname);
		std::filesystem::remove(filename);

		safe_snprintf(filename, _MAX_PATH, "%s.p1", g_Mapname);
		std::filesystem::remove(filename);

		safe_snprintf(filename, _MAX_PATH, "%s.p2", g_Mapname);
		std::filesystem::remove(filename);

		safe_snprintf(filename, _MAX_PATH, "%s.p3", g_Mapname);
		std::filesystem::remove(filename);

		safe_snprintf(filename, _MAX_PATH, "%s.prt", g_Mapname);
		std::filesystem::remove(filename);

		safe_snprintf(filename, _MAX_PATH, "%s.pts", g_Mapname);
		std::filesystem::remove(filename);

		safe_snprintf(filename, _MAX_PATH, "%s.lin", g_Mapname);
		std::filesystem::remove(filename);

		safe_snprintf(filename, _MAX_PATH, "%s.hsz", g_Mapname);
		std::filesystem::remove(filename);

		safe_snprintf(filename, _MAX_PATH, "%s.pln", g_Mapname);
		std::filesystem::remove(filename);

		safe_snprintf(filename, _MAX_PATH, "%s.b0", g_Mapname);
		std::filesystem::remove(filename);

		safe_snprintf(filename, _MAX_PATH, "%s.b1", g_Mapname);
		std::filesystem::remove(filename);

		safe_snprintf(filename, _MAX_PATH, "%s.b2", g_Mapname);
		std::filesystem::remove(filename);

		safe_snprintf(filename, _MAX_PATH, "%s.b3", g_Mapname);
		std::filesystem::remove(filename);

		safe_snprintf(filename, _MAX_PATH, "%s.wa_", g_Mapname);
		std::filesystem::remove(filename);

		safe_snprintf(filename, _MAX_PATH, "%s.ext", g_Mapname);
		std::filesystem::remove(filename);
	}
}

void ResetLog() {
	if (g_log) {
		char logfilename[_MAX_PATH];

		safe_snprintf(logfilename, _MAX_PATH, "%s.log", g_Mapname);
		std::filesystem::remove(logfilename);
	}
}

void ResetErrorLog() {
	if (g_log) {
		char logfilename[_MAX_PATH];

		safe_snprintf(logfilename, _MAX_PATH, "%s.err", g_Mapname);
		std::filesystem::remove(logfilename);
	}
}

void CheckForErrorLog() {
	if (g_log) {
		char logfilename[_MAX_PATH];

		safe_snprintf(logfilename, _MAX_PATH, "%s.err", g_Mapname);
		if (std::filesystem::exists(logfilename)) {
			Log(">> There was a problem compiling the map.\n"
				">> Check the file %s.log for the cause.\n",
				g_Mapname);
			exit(1);
		}
	}
}

///////

void LogError(char const * const message) {
	if (g_log && CompileLog) {
		char logfilename[_MAX_PATH];
		FILE* ErrorLog = nullptr;

		safe_snprintf(logfilename, _MAX_PATH, "%s.err", g_Mapname);
		ErrorLog = fopen(logfilename, "a");

		if (ErrorLog) {
			fprintf(ErrorLog, "%s: %s\n", g_Program, message);
			fflush(ErrorLog);
			fclose(ErrorLog);
			ErrorLog = nullptr;
		} else {
			fprintf(
				stderr,
				"ERROR: Could not open error logfile %s",
				logfilename
			);
			fflush(stderr);
		}
	}
}

void OpenLog(int const clientid) {
	if (g_log) {
		char logfilename[_MAX_PATH];
		safe_snprintf(logfilename, _MAX_PATH, "%s.log", g_Mapname);
		CompileLog = fopen(logfilename, "a");

		if (!CompileLog) {
			fprintf(
				stderr, "ERROR: Could not open logfile %s", logfilename
			);
			fflush(stderr);
		}
	}
}

void CloseLog() {
	if (g_log && CompileLog) {
		LogEnd();
		fflush(CompileLog);
		fclose(CompileLog);
		CompileLog = nullptr;
	}
}

//
//  Every function up to this point should check g_log, the functions below
//  should not
//

void WriteLog(char const * const message) {
	if (CompileLog) {
		fprintf(
			CompileLog, "%s", message
		); // fprintf(CompileLog, message); //--vluzacn
		fflush(CompileLog);
	}

	fprintf(stdout, "%s", message); // fprintf(stdout, message); //--vluzacn
	fflush(stdout);
}

// =====================================================================================
//  CheckFatal
// =====================================================================================
void CheckFatal() {
	if (fatal) {
		hlassert(false);
		exit(1);
	}
}

#define MAX_ERROR	2048
#define MAX_WARNING 2048
#define MAX_MESSAGE 2048

// =====================================================================================
//  Error
//      for formatted error messages, fatals out
// =====================================================================================
void FORMAT_PRINTF(1, 2) Error(char const * const error, ...) {
	char message[MAX_ERROR];
	char message2[MAX_ERROR];
	va_list argptr;

	/*#if defined( SYSTEM_WIN32 ) && !defined( __MINGW32__ ) && !defined(
   __BORLANDC__ )
	   {
		   char* wantint3 = getenv("WANTINT3");
		   if (wantint3)
		   {
			   if (atoi(wantint3))
			   {
				   __asm
				   {
					   int 3;
				   }
			   }
		   }
	   }
   #endif*/

	va_start(argptr, error);
	vsnprintf(message, MAX_ERROR, error, argptr);
	va_end(argptr);

	safe_snprintf(message2, MAX_MESSAGE, "%s%s\n", "Error: ", message);
	WriteLog(message2);
	LogError(message2);

	fatal = 1;
	CheckFatal();
}

// =====================================================================================
//  Fatal
//      For formatted 'fatal' warning messages
//      automatically appends an extra newline to the message
//      This function sets a flag that the compile should abort before
//      completing
// =====================================================================================
void FORMAT_PRINTF(2, 3)
	Fatal(assume_msgs msgid, char const * const warning, ...) {
	char message[MAX_WARNING];
	char message2[MAX_WARNING];

	va_list argptr;

	va_start(argptr, warning);
	vsnprintf(message, MAX_WARNING, warning, argptr);
	va_end(argptr);

	safe_snprintf(message2, MAX_MESSAGE, "%s%s\n", "Error: ", message);
	WriteLog(message2);
	LogError(message2);

	{
		char message[MAX_MESSAGE];
		MessageTable_t const * msg = GetAssume(msgid);

		safe_snprintf(
			message,
			MAX_MESSAGE,
			"%s\n%s%s\n%s%s\n",
			msg->title,
			"Description: ",
			msg->text,
			"Howto Fix: ",
			msg->howto
		);
		PrintOnce("%s", message);
	}

	fatal = 1;
}

// =====================================================================================
//  PrintOnce
//      This function is only callable one time. Further calls will be
//      ignored
// =====================================================================================
void FORMAT_PRINTF(1, 2) PrintOnce(char const * const warning, ...) {
	char message[MAX_WARNING];
	char message2[MAX_WARNING];
	va_list argptr;
	static int count = 0;

	if (count > 0) // make sure it only gets called once
	{
		return;
	}
	count++;

	va_start(argptr, warning);
	vsnprintf(message, MAX_WARNING, warning, argptr);
	va_end(argptr);

	safe_snprintf(message2, MAX_MESSAGE, "%s%s\n", "Error: ", message);
	WriteLog(message2);
	LogError(message2);
}

// =====================================================================================
//  Warning
//      For formatted warning messages
//      automatically appends an extra newline to the message
// =====================================================================================
void FORMAT_PRINTF(1, 2) Warning(char const * const warning, ...) {
	char message[MAX_WARNING];
	char message2[MAX_WARNING];

	va_list argptr;

	va_start(argptr, warning);
	vsnprintf(message, MAX_WARNING, warning, argptr);
	va_end(argptr);

	safe_snprintf(message2, MAX_MESSAGE, "%s%s\n", "Warning: ", message);
	WriteLog(message2);
}

// =====================================================================================
//  Verbose
//      Same as log but only prints when in verbose mode
// =====================================================================================
void FORMAT_PRINTF(1, 2) Verbose(char const * const warning, ...) {
	if (g_verbose) {
		char message[MAX_MESSAGE];

		va_list argptr;

		va_start(argptr, warning);
		vsnprintf(message, MAX_MESSAGE, warning, argptr);
		va_end(argptr);

		WriteLog(message);
	}
}

// =====================================================================================
//  Developer
//      Same as log but only prints when in developer mode
// =====================================================================================
void FORMAT_PRINTF(2, 3)
	Developer(developer_level level, char const * const warning, ...) {
	if (level <= g_developer) {
		char message[MAX_MESSAGE];

		va_list argptr;

		va_start(argptr, warning);
		vsnprintf(message, MAX_MESSAGE, warning, argptr);
		va_end(argptr);

		WriteLog(message);
	}
}

// =====================================================================================
//  DisplayDeveloperLevel
// =====================================================================================
static void DisplayDeveloperLevel() {
	char message[MAX_MESSAGE];

	safe_strncpy(message, "Developer messages enabled : [", MAX_MESSAGE);
	if (g_developer >= developer_level::megaspam) {
		safe_strncat(message, "MegaSpam ", MAX_MESSAGE);
	}
	if (g_developer >= developer_level::spam) {
		safe_strncat(message, "Spam ", MAX_MESSAGE);
	}
	if (g_developer >= developer_level::fluff) {
		safe_strncat(message, "Fluff ", MAX_MESSAGE);
	}
	if (g_developer >= developer_level::message) {
		safe_strncat(message, "Message ", MAX_MESSAGE);
	}
	if (g_developer >= developer_level::warning) {
		safe_strncat(message, "Warning ", MAX_MESSAGE);
	}
	if (g_developer >= developer_level::error) {
		safe_strncat(message, "Error", MAX_MESSAGE);
	}
	if (g_developer >= developer_level::always) {
		safe_strncat(message, "]\n", MAX_MESSAGE);
		Log("%s", message);
	}
}

// =====================================================================================
//  Log
//      For formatted log output messages
// =====================================================================================
void FORMAT_PRINTF(1, 2) Log(char const * const warning, ...) {
	char message[MAX_MESSAGE];

	va_list argptr;

	va_start(argptr, warning);
	vsnprintf(message, MAX_MESSAGE, warning, argptr);
	va_end(argptr);

	WriteLog(message);
}

void log_arguments(int argc, char** argv) {
	Log("Arguments: ");
	// i = 1 to skip the executable path
	for (int i = 1; i < argc; i++) {
		std::string_view arg{ argv[i] };
		if (arg.contains(' ')) {
			Log("\"%s\" ", argv[i]);
		} else {
			Log("%s ", argv[i]);
		}
	}
	Log("\n");
}

static void LogCommandLine(int argc, char** argv) {
	Log("Command line: ");
	for (int i = 0; i < argc; i++) {
		std::string_view arg{ argv[i] };
		if (arg.contains(' ')) {
			Log("\"%s\" ", argv[i]);
		} else {
			Log("%s ", argv[i]);
		}
	}
	Log("\n");
}

// =====================================================================================
//  Banner
// =====================================================================================
void Banner() {
	Log((char const *) u8"%s %s %s\n",
		g_Program,
		(char const *) projectVersionString.data(),
		(char const *) projectPlatformVersion.data());
	// Log("BUGGY %s (built: %s)\nUse at own risk.\n", g_Program, __DATE__);

	Log((char const *) u8"" PROJECT_NAME "\n"
					   "By Oskar Larsson Högfeldt ( https://oskar.pm/ )\n"
					   "Based on code modifications by Sean 'Zoner' Cavanaugh, Vluzacn, and seedee.\n"
					   "Based on Valve's compile tools, modified with permission.\n"
					   "Submit detailed bug reports to %s\n",
		(char const *) projectIssueTracker.data());
}

// =====================================================================================
//  LogStart
// =====================================================================================
void LogStart(int argc, char** argv) {
	Banner();
	Log("-----  BEGIN  %s -----\n", g_Program);
	LogCommandLine(argc, argv);
	DisplayDeveloperLevel();
}

// =====================================================================================
//  LogEnd
// =====================================================================================
void LogEnd() {
	Log("\n-----   END   %s -----\n\n\n\n", g_Program);
}

// =====================================================================================
//  hlassume
//      my assume
// =====================================================================================
void hlassume(bool exp, assume_msgs msgid) {
	if (!exp) {
		char message[MAX_MESSAGE];
		MessageTable_t const * msg = GetAssume(msgid);

		safe_snprintf(
			message,
			MAX_MESSAGE,
			"%s\n%s%s\n%s%s\n",
			msg->title,
			"Description: ",
			msg->text,
			"Howto Fix: ",
			msg->howto
		);
		Error("%s", message);
	}
}

// =====================================================================================
//  seconds_to_hhmm
// =====================================================================================
static void seconds_to_hhmm(
	unsigned int elapsed_time,
	unsigned& days,
	unsigned& hours,
	unsigned& minutes,
	unsigned& seconds
) {
	seconds = elapsed_time % 60;
	elapsed_time /= 60;

	minutes = elapsed_time % 60;
	elapsed_time /= 60;

	hours = elapsed_time % 24;
	elapsed_time /= 24;

	days = elapsed_time;
}

// =====================================================================================
//  LogTimeElapsed
// =====================================================================================
void LogTimeElapsed(float elapsed_time) {
	unsigned days = 0;
	unsigned hours = 0;
	unsigned minutes = 0;
	unsigned seconds = 0;

	seconds_to_hhmm(elapsed_time, days, hours, minutes, seconds);

	if (days) {
		Log("%.2f seconds elapsed [%ud %uh %um %us]\n",
			elapsed_time,
			days,
			hours,
			minutes,
			seconds);
	} else if (hours) {
		Log("%.2f seconds elapsed [%uh %um %us]\n",
			elapsed_time,
			hours,
			minutes,
			seconds);
	} else if (minutes) {
		Log("%.2f seconds elapsed [%um %us]\n",
			elapsed_time,
			minutes,
			seconds);
	} else {
		Log("%.2f seconds elapsed\n", elapsed_time);
	}
}

void FORMAT_PRINTF(1, 2) PrintConsole(char const * const warning, ...) {
	char message[MAX_MESSAGE];

	va_list argptr;

	va_start(argptr, warning);

	vsnprintf(message, MAX_MESSAGE, warning, argptr);
	va_end(argptr);

	fprintf(stdout, "%s", message);
}
