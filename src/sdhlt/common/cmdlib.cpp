

#include "cmdlib.h"
#include "messages.h"
#include "hlassert.h"
#include "blockmem.h"
#include "log.h"
#include "mathlib.h"

#include <bit>
#ifdef SYSTEM_POSIX
#include <sys/time.h>
#endif
#include <ranges>

/*
 * ================
 * I_FloatTime
 * ================
 */

double          I_FloatTime()
{
    struct timeval  tp;
    struct timezone tzp;
    static int      secbase;

    gettimeofday(&tp, &tzp);

    if (!secbase)
    {
        secbase = tp.tv_sec;
        return tp.tv_usec / 1000000.0;
    }

    return (tp.tv_sec - secbase) + tp.tv_usec / 1000000.0;
}


static char8_t ascii_character_to_lowercase(char8_t c) {
	const char8_t add = (c >= u8'A' && c <= u8'Z') ? (u8'a' - u8'A') : u8'0';
	return c + add;
}
static char8_t ascii_character_to_uppercase(char8_t c) {
	const char8_t subtract = (c >= u8'a' && c <= u8'z') ? (u8'a' - u8'A') : u8'0';
	return c - subtract;
}


static auto ascii_characters_to_lowercase_in_utf8_string_as_view(std::u8string_view input) {
	return std::ranges::transform_view(input, [](char8_t c) {
		const char8_t add = (c >= u8'A' && c <= u8'Z') ? (u8'a' - u8'A') : u8'0';
		return c + add;
	});
}
static auto ascii_characters_to_uppercase_in_utf8_string_as_view(std::u8string_view input) {
	return std::ranges::transform_view(input, [](char8_t c) {
		const char8_t subtract = (c >= u8'a' && c <= u8'z') ? (u8'a' - u8'A') : u8'0';
		return c - subtract;
	});
}

std::u8string ascii_characters_to_lowercase_in_utf8_string(std::u8string_view input) {
	auto lowercaseView = ascii_characters_to_lowercase_in_utf8_string_as_view(input);
	return std::u8string{lowercaseView.begin(), lowercaseView.end()};
}

std::u8string ascii_characters_to_uppercase_in_utf8_string(std::u8string_view input) {
	auto uppercaseView = ascii_characters_to_uppercase_in_utf8_string_as_view(input);
	return std::u8string{uppercaseView.begin(), uppercaseView.end()};
}

void make_ascii_characters_lowercase_in_utf8_string(std::u8string& input) {
	for(char8_t& c : input) {
		c = ascii_character_to_lowercase(c);
	}
}
void make_ascii_characters_uppercase_in_utf8_string(std::u8string& input) {
	for(char8_t& c : input) {
		c = ascii_character_to_uppercase(c);
	}
}

bool strings_equal_with_ascii_case_insensitivity(std::u8string_view a, std::u8string_view b) {
	return std::ranges::equal(
		ascii_characters_to_lowercase_in_utf8_string_as_view(a),
		ascii_characters_to_lowercase_in_utf8_string_as_view(b)
	);
}



char*           strlwr(char* string)
{
    int             i;
    int             len = strlen(string);

    for (i = 0; i < len; i++)
    {
        string[i] = tolower(string[i]);
    }
    return string;
}

// Case-insensitive substring matching
bool a_contains_b_ignoring_ascii_character_case_differences(std::u8string_view string, std::u8string_view substring)
{
    std::u8string string_lowercase = ascii_characters_to_lowercase_in_utf8_string(string);
    std::u8string substring_lowercase = ascii_characters_to_lowercase_in_utf8_string(substring);
	return string_lowercase.contains(substring_lowercase);
}

/*--------------------------------------------------------------------
// New implementation of FlipSlashes, DefaultExtension, StripFilename, 
// StripExtension, ExtractFilePath, ExtractFile, ExtractFileBase, etc.
----------------------------------------------------------------------*/

//Since all of these functions operate around either the extension 
//or the directory path, centralize getting both numbers here so we
//can just reference them everywhere else.  Use strrchr to give a
//speed boost while we're at it.
inline void getFilePositions(const char* path, int* extension_position, int* directory_position)
{
	const char* ptr = strrchr(path,'.');
	if(ptr == 0)
	{ *extension_position = -1; }
	else
	{ *extension_position = ptr - path; }

	ptr = std::max(strrchr(path,'/'),strrchr(path,'\\'));
	if(ptr == 0)
	{ *directory_position = -1; }
	else
	{ 
		*directory_position = ptr - path;
		if(*directory_position > *extension_position)
		{ *extension_position = -1; }
		
		//cover the case where we were passed a directory - get 2nd-to-last slash
		if(*directory_position == (int)strlen(path) - 1)
		{
			do
			{
				--(*directory_position);
			}
			while(*directory_position > -1 && path[*directory_position] != '/' && path[*directory_position] != '\\');
		}
	}
}

char* FlipSlashes(char* string)
{
	char* ptr = string;
	if(SYSTEM_SLASH_CHAR == '\\')
	{
		while((ptr = strchr(ptr,'/')))
		{ *ptr = SYSTEM_SLASH_CHAR; }
	}
	else
	{
		while((ptr = strchr(ptr,'\\')))
		{ *ptr = SYSTEM_SLASH_CHAR; }
	}
	return string;
}

void DefaultExtension(char* path, const char* extension)
{
	int extension_pos, directory_pos;
	getFilePositions(path,&extension_pos,&directory_pos);
	if(extension_pos == -1)
	{ strcat(path,extension); }
}

void StripFilename(char* path)
{
	int extension_pos, directory_pos;
	getFilePositions(path,&extension_pos,&directory_pos);
	if(directory_pos == -1)
	{ path[0] = 0; }
	else
	{ path[directory_pos] = 0; }
}

void StripExtension(char* path)
{
	int extension_pos, directory_pos;
	getFilePositions(path,&extension_pos,&directory_pos);
	if(extension_pos != -1)
	{ path[extension_pos] = 0; }
}

void ExtractFilePath(const char* const path, char* dest)
{
	int extension_pos, directory_pos;
	getFilePositions(path,&extension_pos,&directory_pos);
	if(directory_pos != -1)
	{
	    memcpy(dest,path,directory_pos+1); //include directory slash
	    dest[directory_pos+1] = 0;
	}
	else
	{ dest[0] = 0; }
}

void ExtractFile(const char* const path, char* dest)
{
	int extension_pos, directory_pos;
	getFilePositions(path,&extension_pos,&directory_pos);

	int length = strlen(path);

	length -= directory_pos + 1;

    memcpy(dest,path+directory_pos+1,length); //exclude directory slash
    dest[length] = 0;
}

void ExtractFileBase(const char* const path, char* dest)
{
	int extension_pos, directory_pos;
	getFilePositions(path,&extension_pos,&directory_pos);
	int length = extension_pos == -1 ? strlen(path) : extension_pos;

	length -= directory_pos + 1;

    memcpy(dest,path+directory_pos+1,length); //exclude directory slash
    dest[length] = 0;
}

void ExtractFileExtension(const char* const path, char* dest)
{
	int extension_pos, directory_pos;
	getFilePositions(path,&extension_pos,&directory_pos);
	if(extension_pos != -1)
	{
		int length = strlen(path) - extension_pos;
	    memcpy(dest,path+extension_pos,length); //include extension '.'
	    dest[length] = 0;
	}
	else
	{ dest[0] = 0; }
}
//-------------------------------------------------------------------


//=============================================================================

bool FORMAT_PRINTF(3,4)      safe_snprintf(char* const dest, const size_t count, const char* const args, ...)
{
    size_t          amt;
    va_list         argptr;

    hlassert(count > 0);

    va_start(argptr, args);
    amt = vsnprintf(dest, count, args, argptr);
    va_end(argptr);

    // truncated (bad!, snprintf doesn't null terminate the string when this happens)
    if (amt == count)
    {
        dest[count - 1] = 0;
        return false;
    }

    return true;
}

bool            safe_strncpy(char* const dest, const char* const src, const size_t count)
{
    return safe_snprintf(dest, count, "%s", src);
}

bool            safe_strncat(char* const dest, const char* const src, const size_t count)
{
    if (count)
    {
        strncat(dest, src, count);

        dest[count - 1] = 0;                               // Ensure it is null terminated
        return true;
    }
    else
    {
        Warning("safe_strncat passed empty count");
        return false;
    }
}

bool            TerminatedString(const char* buffer, const int size)
{
    int             x;

    for (x = 0; x < size; x++, buffer++)
    {
        if ((*buffer) == 0)
        {
            return true;
        }
    }
    return false;
}
