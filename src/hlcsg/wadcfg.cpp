#include "csg.h"
#include "utf8.h"

#include <string>
using namespace std::literals;


void LoadWadconfig (const char *filename, const char *configname)
{
    char filenameOnly[_MAX_PATH];
    size_t filenameLength = strlen(filename);
    strncpy(filenameOnly, filename, filenameLength);
    filenameOnly[filenameLength] = '\0'; //Null terminate
	auto pos = std::string(filenameOnly).find_last_of("/\\");

	if (pos != std::string::npos) // Copy everything after the last slash to the beginning of filenameOnly
	{
		std::string temp(filenameOnly + pos + 1);
		strncpy(filenameOnly, temp.c_str(), _MAX_PATH);
		filenameOnly[temp.size()] = '\0';
	}
	Log("Loading wadconfig %s from '%s'\n", configname, filenameOnly);
	Log("--------------------------------------\n");
	int wadconfigsFound = 0;
	int wadPathsFound = 0;
	std::optional<std::u8string> maybeContent = read_utf8_file(filename, true);
	if(!maybeContent) {
		Error("Failed to read the WAD config");
	}
	ParseFromMemory (maybeContent.value());

	while (GetToken (true)) //Loop through file
	{
		bool skip = true; //Skip until the -wadconfig configname is found

		if (strings_equal_with_ascii_case_insensitivity(g_token, (const char8_t*) configname)) //If we find configname line
		{
			skip = false;
			wadconfigsFound++;
		}
		if (!GetToken (true) || !strings_equal_with_ascii_case_insensitivity(g_token, u8"{"sv)) //If next line is not an opening bracket
		{
			Error ("Parsing %s (missing '{' opening bracket in '%s' config)\n", filenameOnly, configname);
		}
		while (1) //Loop through content of braces
		{
			if (!GetToken (true))
			{
				Error("Parsing '%s': unexpected EOF in '%s'\n", filenameOnly, configname);
			}
			if (strings_equal_with_ascii_case_insensitivity(g_token, u8"}"sv)) //If we find closing bracket
			{
				break;
			}
			if (skip)
			{
				continue;
			}
			bool include = false;
			if (strings_equal_with_ascii_case_insensitivity(g_token, u8"include"sv))
			{
				Log("[include] ");
				include = true;

				if (!GetToken (true))
				{
					Error ("Parsing '%s': unexpected EOF in '%s'\n", filenameOnly, configname);
				}
			}
			Log ("%s\n", (const char*) g_token.c_str());
			wadPathsFound++;
			PushWadPath (g_token, !include);
		}
	}
	Log("- %d wadpaths found in %s\n", wadPathsFound, configname);
	Log("--------------------------------------\n\n");

	if (wadconfigsFound < 1)
	{
		Error ("Couldn't find wad config %s in '%s'\n", configname, filenameOnly);
	}
	if (wadconfigsFound > 1)
	{
		Error("Found more than one wad config %s in '%s'\n", configname, filenameOnly);
	}
	//Log ("Using custom wadfile configuration: '%s' (with %i wad%s)\n", configname, wadPathsFound, wadPathsFound > 1 ? "s" : "");
}
void LoadWadcfgfile (const char *filename)
{
	Log ("Loading %s\n", filename);
	Log ("------------\n");
	int wadPathsCount = 0;
	std::optional<std::u8string> maybeContent = read_utf8_file(filename, true);
	if(!maybeContent) {
		Error("Failed to read the WAD config");
	}
	ParseFromMemory (maybeContent.value());
	while (GetToken (true)) //Loop through file
	{
		bool include = false;
		if (strings_equal_with_ascii_case_insensitivity(g_token, u8"include"sv)) //If line starts with include (or contains?)
		{
			Log ("include ");
			include = true;
			if (!GetToken (true))
			{
				Error ("parsing '%s': unexpected end of file.", filename);
			}
		}
		Log ("\"%s\"\n", (const char*) g_token.c_str());
		wadPathsCount++;
		PushWadPath (g_token, !include);
	}
	//Log ("Using custom wadfile configuration: '%s' (with %i wad%s)\n", filename, count, count > 1 ? "s" : "");
}
