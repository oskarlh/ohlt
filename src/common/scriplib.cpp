#include "cmdlib.h"
#include "filelib.h"
#include "messages.h"
#include "log.h"
#include "scriplib.h"
#include <cstring>

std::u8string g_token;

typedef struct
{
    std::filesystem::path filename;
    bool fromMemory;
    std::u8string buffer;
    const char8_t* script_p;
    const char8_t* end_p;
    int line;
}
script_t;


#define	MAX_INCLUDES	8


static script_t s_scriptstack[MAX_INCLUDES];
script_t* s_script;
int s_scriptline;
bool s_endofscript;
bool s_tokenready;                                // only true if UnGetToken was just called


//  AddScriptToStack
//  LoadScriptFile
//  ParseFromMemory
//  UnGetToken
//  EndOfScript
//  GetToken
//  TokenAvailable

// =====================================================================================
//  AddScriptToStack
// =====================================================================================
static void AddScriptToStack(const char* const filename) {
    s_script++;

    if (s_script == &s_scriptstack[MAX_INCLUDES]) {
        Error("s_script file exceeded MAX_INCLUDES");
    }

    s_script->filename = filename;
    s_script->fromMemory = false;

    std::optional<std::u8string> maybeContents = read_utf8_file(filename, true);
    if(!maybeContents) {
        Error("Failed to load %s", filename);
    }

    s_script->buffer = std::move(maybeContents.value());

    Log("Entering %s\n", filename);

    s_script->line = 1;
    s_script->script_p = s_script->buffer.data();
    s_script->end_p = s_script->buffer.data() + s_script->buffer.size();
}

// =====================================================================================
//  LoadScriptFile
// =====================================================================================
void            LoadScriptFile(const char* const filename)
{
    s_script = s_scriptstack;
    AddScriptToStack(filename);

    s_endofscript = false;
    s_tokenready = false;
}

// =====================================================================================
//  ParseFromMemory
// =====================================================================================
void ParseFromMemory(std::u8string_view buffer)
{
    s_script = s_scriptstack;
    s_script++;

    if (s_script == &s_scriptstack[MAX_INCLUDES]) {
        Error("s_script file exceeded MAX_INCLUDES");
    }

    s_script->filename = std::filesystem::path{};
    s_script->fromMemory = true;

    s_script->buffer.clear();
    s_script->line = 1;
    s_script->script_p = buffer.data();
    s_script->end_p = buffer.data() + buffer.size();

    s_endofscript = false;
    s_tokenready = false;
}

// =====================================================================================
//  UnGetToken
/*
 * Signals that the current g_token was not used, and should be reported
 * for the next GetToken.  Note that
 * 
 * GetToken (true);
 * UnGetToken ();
 * GetToken (false);
 * 
 * could cross a line boundary.
 */
// =====================================================================================
void            UnGetToken()
{
    s_tokenready = true;
}

// =====================================================================================
//  EndOfScript
// =====================================================================================
bool            EndOfScript(const bool crossline)
{
    if (!crossline)
        Error("Line %i is incomplete (did you place a \" inside an entity string?) \n", s_scriptline);

    if (s_script->fromMemory)
    {
        s_endofscript = true;
        return false;
    }

    s_script->buffer.clear();
    s_script->buffer.shrink_to_fit();

    if (s_script == s_scriptstack + 1)
    {
        s_endofscript = true;
        return false;
    }

    s_script--;
    s_scriptline = s_script->line;

    Log("Returning to %s\n", s_script->fromMemory ? "Memory buffer" : s_script->filename.c_str());

    return GetToken(crossline);
}

// =====================================================================================
//  GetToken
// =====================================================================================
bool GetToken(const bool crossline) {
    if (s_tokenready)                                        // is a g_token allready waiting?
    {
        s_tokenready = false;
        return true;
    }

    if (s_script->script_p >= s_script->end_p)
        return EndOfScript(crossline);

    // skip space
skipspace:
	while (*s_script->script_p <= 32 && *s_script->script_p >= 0)
    {
        if (s_script->script_p >= s_script->end_p)
            return EndOfScript(crossline);

        if (*s_script->script_p++ == '\n')
        {
            if (!crossline)
                Error("Line %i is incomplete (did you place a \" inside an entity string?) \n", s_scriptline);
            s_scriptline = s_script->line++;
        }
    }

    if (s_script->script_p >= s_script->end_p)
        return EndOfScript(crossline);

    // comment fields
    if (*s_script->script_p == ';' || *s_script->script_p == '#' || // semicolon and # is comment field
        (*s_script->script_p == '/' && *((s_script->script_p) + 1) == '/')) // also make // a comment field
    {
        if (!crossline)
            Error("Line %i is incomplete (did you place a \" inside an entity string?) \n", s_scriptline);

        //ets+++
        if (*s_script->script_p == '/')
            s_script->script_p++;

        //ets---
        while (*s_script->script_p++ != '\n')
        {
            if (s_script->script_p >= s_script->end_p)
                return EndOfScript(crossline);
        }
        //ets+++
        s_scriptline = s_script->line++;                       // AR: this line was missing
        //ets---
        goto skipspace;
    }

    g_token.clear();
    if (*s_script->script_p == '"')
    {
        // quoted token
        s_script->script_p++;
        while (*s_script->script_p != '"')
        {
            g_token.push_back(*s_script->script_p++);

            if (s_script->script_p == s_script->end_p)
                break;

            if (g_token.size() == MAXTOKEN)
                Error("Token too large on line %i\n", s_scriptline);
        }
        s_script->script_p++;
    }
    else
    {
        // regular token
		while ((*s_script->script_p > 32 || *s_script->script_p < 0) && *s_script->script_p != ';')
        {
            g_token.push_back(*s_script->script_p++);

            if (s_script->script_p == s_script->end_p)
                break;

            if (g_token.size() == MAXTOKEN)
                Error("Token too large on line %i\n", s_scriptline);
        }
    }
    return true;
}


// =====================================================================================
//  TokenAvailable
//      returns true if there is another token on the line
// =====================================================================================
bool            TokenAvailable()
{
    const char8_t* search_p = s_script->script_p;

    if (search_p >= s_script->end_p)
        return false;

    while (*search_p <= 32)
    {
        if (*search_p == u8'\n')
            return false;

        search_p++;

        if (search_p == s_script->end_p)
            return false;
    }

    if (*search_p == u8';')
        return false;

    return true;
}
