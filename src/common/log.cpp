#include "log.h"

#include "cli_option_defaults.h"
#include "cmdlib.h"
#include "hlassert.h"
#include "messages.h"
#include "project_constants.h"

#include <filesystem>

std::u8string g_Program = u8"Uninitialized variable";
std::filesystem::path g_Mapname;
std::filesystem::path g_Wadpath;

developer_level g_developer = cli_option_defaults::developer;
bool g_verbose = cli_option_defaults::verbose;
bool g_log = cli_option_defaults::log;

static FILE* CompileLog = nullptr;
static bool fatal = false;

////////

std::filesystem::path path_to_temp_file_with_extension(
	std::filesystem::path mapBasePath, std::u8string_view extension
) {
	mapBasePath += extension;
	return mapBasePath;
}

static std::filesystem::path
path_to_log_file(std::filesystem::path mapBasePath) {
	return path_to_temp_file_with_extension(mapBasePath, u8".log");
}

static std::filesystem::path
path_to_error_log_file(std::filesystem::path mapBasePath) {
	return path_to_temp_file_with_extension(mapBasePath, u8".err");
}

void ResetTmpFiles() {
	if (g_log) {
		constexpr auto extensions = std::to_array<std::u8string_view>({
			u8".b0",
			u8".b1",
			u8".b2",
			u8".b3",
			u8".bsp",
			u8".ext",
			u8".hsz",
			u8".inc",
			u8".lin",
			u8".p0",
			u8".p1",
			u8".p2",
			u8".p3",
			u8".pln",
			u8".prt",
			u8".pts",
			u8".wa_",

		});
		for (auto const & extension : extensions) {
			std::filesystem::remove(
				path_to_temp_file_with_extension(g_Mapname, extension)
			);
		}
	}
}

void ResetLog() {
	if (g_log) {
		std::filesystem::remove(path_to_log_file(g_Mapname));
	}
}

void ResetErrorLog() {
	if (g_log) {
		std::filesystem::remove(path_to_error_log_file(g_Mapname));
	}
}

void CheckForErrorLog() {
	if (g_log) {
		std::filesystem::path const errorFilePath{
			path_to_error_log_file(g_Mapname)
		};
		if (std::filesystem::exists(errorFilePath)) {
			Log(">> There was a problem compiling the map.\n"
			    ">> Check the files %s and %s for the cause.\n",
			    errorFilePath.c_str(),
			    path_to_log_file(g_Mapname).c_str());
			exit(1);
		}
	}
}

void LogError(char const * const message) {
	if (g_log && CompileLog) {
		std::filesystem::path filePath{ path_to_error_log_file(g_Mapname) };
		FILE* ErrorLog{ fopen(filePath.c_str(), "a") };

		if (ErrorLog) {
			fprintf(
				ErrorLog,
				"%s: %s\n",
				(char const *) g_Program.data(),
				message
			);
			fflush(ErrorLog);
			fclose(ErrorLog);
			ErrorLog = nullptr;
		} else {
			fprintf(
				stderr,
				"ERROR: Could not open error logfile %s",
				filePath.c_str()
			);
			fflush(stderr);
		}
	}
}

void OpenLog() {
	if (g_log) {
		std::filesystem::path filePath{ path_to_log_file(g_Mapname) };
		CompileLog = fopen(filePath.c_str(), "a");

		if (!CompileLog) {
			fprintf(
				stderr, "ERROR: Could not open logfile %s", filePath.c_str()
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

#define MAX_ERROR   2048
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
	Fatal(assume_msg msgid, char const * const warning, ...) {
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
		MessageTable_t const & msg = get_assume(msgid);

		PrintOnce(
			"%s\nDescription: %s\nHow to fix: %s\n",
			(char const *) msg.title.data(),
			(char const *) msg.text.data(),
			(char const *) msg.howto.data()
		);
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
	if (g_developer == developer_level::disabled) {
		Log("Developer messages disabled.\n");
		return;
	}

	Log("Developer messages enabled, logging everything above level: %s\n",
	    (char const *) name_of_developer_level(g_developer).data());
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
	    (char const *) g_Program.data(),
	    (char const *) projectVersionString.data(),
	    (char const *) projectPlatformVersion.data());

	Log((char const *) u8"" PROJECT_NAME "\n"
	                   "By Oskar Larsson Högfeldt AKA Oskar_Potatis ( https://oskar.pm/ )\n"
	                   "Based on code modifications by Sean \"Zoner\" Cavanaugh, Ryan \"Nemesis\" Gregg, amckern, Tony \"Merl\" Moore, Vluzacn, Uncle Mike and seedee.\n"
	                   "Based on Valve's compile tools, modified with permission.\n"
	                   "Submit detailed bug reports to %s\n",
	    (char const *) projectIssueTracker.data());
}

// =====================================================================================
//  LogStart
// =====================================================================================
void LogStart(int argc, char** argv) {
	Banner();
	Log("-----  BEGIN  %s -----\n", (char const *) g_Program.data());
	LogCommandLine(argc, argv);
	DisplayDeveloperLevel();
}

// =====================================================================================
//  LogEnd
// =====================================================================================
void LogEnd() {
	Log("\n-----   END   %s -----\n\n\n\n",
	    (char const *) g_Program.data());
}

// =====================================================================================
//  hlassume
//      my assume
// =====================================================================================
void hlassume(bool exp, assume_msg msgid) {
	if (exp) {
		return;
	}

	MessageTable_t const & msg = get_assume(msgid);
	Error(
		"%s\nDescription: %s\nHow to fix: %s\n",
		(char const *) msg.title.data(),
		(char const *) msg.text.data(),
		(char const *) msg.howto.data()
	);
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

void FlushConsole() {
	fflush(stdout);
}
