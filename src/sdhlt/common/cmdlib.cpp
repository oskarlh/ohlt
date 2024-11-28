

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

char*           strupr(char* string)
{
    int             i;
    int             len = strlen(string);

    for (i = 0; i < len; i++)
    {
        string[i] = toupper(string[i]);
    }
    return string;
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

// Case Insensitive substring matching
const char*     stristr(const char* const string, const char* const substring)
{
    char*           string_copy;
    char*           substring_copy;
    const char*     match;

    string_copy = _strdup(string);
    _strlwr(string_copy);

    substring_copy = _strdup(substring);
    _strlwr(substring_copy);

    match = strstr(string_copy, substring_copy);
    if (match)
    {
        match = (string + (match - string_copy));
    }

    free(string_copy);
    free(substring_copy);
    return match;
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
