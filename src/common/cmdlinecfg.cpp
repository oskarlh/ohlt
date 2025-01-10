#include "cmdlib.h"
#include "scriplib.h"
#include "cmdlinecfg.h"
#include "log.h"
#include "filelib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <algorithm>
#ifdef SYSTEM_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

using namespace std::literals;

const char paramfilename[_MAX_PATH] = "settings.txt";
const char sepchr = '\n';
bool error = false;
#define SEPSTR "\n"

int plen (const char8_t *p)
{
	int l;
	for (l = 0; ; l++)
	{
		if (p[l] == '\0')
			return -1;
		if (p[l] == sepchr)
			return l;
	}
}
bool pvalid (const char8_t *p)
{
	return plen (p) >= 0;
}
bool pmatch (const char8_t *cmdlineparam, const char8_t *param)
{
	int cl, cstart, cend, pl, pstart, pend, k;
	cl = plen (cmdlineparam);
	pl = plen (param);
	if (cl < 0 || pl < 0)
		return false;
	bool anystart = (pl > 0 && param[0] == '*');
	bool anyend = (pl > 0 && param[pl-1] == '*');
	pstart = anystart ? 1 : 0;
	pend = anyend ? pl-1 : pl;
	if (pend < pstart) pend = pstart;
	for (cstart = 0; cstart <= cl; ++cstart)
	{
		for (cend = cl; cend >= cstart; --cend)
		{
			if (cend - cstart == pend - pstart)
			{
				for (k = 0; k < cend - cstart; ++k)
					if (tolower (cmdlineparam[k+cstart]) != tolower (param[k+pstart]))
						break;
				if (k == cend - cstart)
					return true;
			}
			if (!anyend)
				break;
		}
		if (!anystart)
			break;
	}
	return false;
}
char8_t * pnext (char8_t *p)
{
	return p + (plen (p) + 1);
}
char8_t * findparams (char8_t *cmdlineparams, char8_t *params)
{
	char8_t *c1, *c, *p;
	for (c1 = cmdlineparams; pvalid (c1); c1 = pnext (c1))
	{
		for (c = c1, p = params; pvalid (p); c = pnext (c), p = pnext (p))
			if (!pvalid (c) || !pmatch (c, p))
				break;
		if (!pvalid (p))
			return c1;
	}
	return nullptr;
}
void addparams (char8_t *cmdline, char8_t *params, unsigned int n)
{
	if (strlen ((const char*) cmdline) + strlen ((const char*) params) + 1 <= n)
		strcat ((char*) cmdline, (char*) params);
	else
		error = true;
}
void delparams (char8_t *cmdline, char8_t *params)
{
	char8_t *c, *p;
	if (!pvalid (params)) //avoid infinite loop
		return;
	while (cmdline = findparams (cmdline, params), cmdline != nullptr)
	{
		for (c = cmdline, p = params; pvalid (p); c = pnext (c), p = pnext (p))
			;
		memmove (cmdline, c, strlen ((char*) c) + 1);
	}
}
typedef enum
{
	IFDEF, IFNDEF, ELSE, ENDIF, DEFINE, UNDEF
}
command_t;
struct execute_t {
	int stack;
	bool skip;
	int skipstack;
};
void parsecommand (execute_t &e, char8_t *cmdline, char8_t *words, unsigned int n)
{
	command_t t;
	if (!pvalid (words))
		return;
	if (pmatch (words, u8"#ifdef" SEPSTR))
		t = IFDEF;
	else if (pmatch (words, u8"#ifndef" SEPSTR))
		t = IFNDEF;
	else if (pmatch (words, u8"#else" SEPSTR))
		t = ELSE;
	else if (pmatch (words, u8"#endif" SEPSTR))
		t = ENDIF;
	else if (pmatch (words, u8"#define" SEPSTR))
		t = DEFINE;
	else if (pmatch (words, u8"#undef" SEPSTR))
		t = UNDEF;
	else
		return;
	if (t == IFDEF || t == IFNDEF)
	{
		e.stack ++;
		if (!e.skip)
		{
			if (t == IFDEF && findparams (cmdline, pnext (words)) ||
				(t == IFNDEF && !findparams (cmdline, pnext (words))))
				e.skip = false;
			else
			{
				e.skipstack = e.stack;
				e.skip = true;
			}
		}
	}
	else if (t == ELSE)
	{
		if (e.skip)
		{
			if (e.stack == e.skipstack)
				e.skip = false;
		}
		else
		{
			e.skipstack = e.stack;
			e.skip = true;
		}
	}
	else if (t == ENDIF)
	{
		if (e.skip)
		{
			if (e.stack == e.skipstack)
				e.skip = false;
		}
		e.stack --;
	}
	else
	{
		if (!e.skip)
		{
			if (t == DEFINE)
				addparams (cmdline, pnext(words), n);
			if (t == UNDEF)
				delparams (cmdline, pnext(words));
		}
	}
}

const char8_t * nextword (const char8_t *s, char8_t *token, unsigned int n)
{
	unsigned int i;
	const char8_t *c;
	bool quote, comment, content;
	for (c=s, i=0, quote=false, comment=false, content=false; c[0] != '\0'; c++)
	{
		if (c[0] == u8'\"')
			quote = !quote;
		if (c[0] == u8'\n')
			quote = false;
		if (c[0] == u8'\n')
			comment = false;
		if (!quote && c[0] == u8'/' && c[1] == u8'/')
			comment = true;
		if (!comment && !(c[0] == u8'\n' || is_ascii_whitespace (c[0])))
			content = true;
		if (!quote && !comment && content && (c[0] == u8'\n' || is_ascii_whitespace (c[0])))
			break;
		if (content && c[0] != u8'\"')
		{
			if (i<n-1)
			{
				token[i++] = c[0];
			}
			else
			{
				error = true;
			}
		}
	}
	token[i] = u8'\0';
	return content? c: nullptr;
}
void parsearg (int argc, char ** argv, char8_t *cmdline, unsigned int n)
{
	int i;
	strcpy ((char*) cmdline, "");
	strcat ((char*) cmdline, "<");
	strcat ((char*) cmdline, g_Program);
	strcat ((char*) cmdline, ">");
	strcat ((char*) cmdline, SEPSTR);
	for (i=1; i<argc; ++i)
	{
		if (strlen ((char*) cmdline) + strlen (argv[i]) + strlen (SEPSTR) + 1 <= n)
		{
			strcat ((char*) cmdline, argv[i]);
			strcat ((char*) cmdline, SEPSTR);
		}
		else
			error = true;
	}
}
void unparsearg (int &argc, char **&argv, char8_t *cmdline)
{
	// TODO: Conversion from native encoding to UTF-8 here
	char8_t *c;
	int i, j;
	i = 0;
	for (c = cmdline; pvalid ( c); c = pnext (c))
		i++;
	argc = i;
	argv = (char **)malloc (argc * sizeof (char *));
	if (!argv)
	{
		error = true;
		return;
	}
	for (c = cmdline, i = 0; pvalid (c); c = pnext (c), i++)
	{
		argv[i] = (char *)malloc (plen (c) + 1);
		if (!argv[i])
		{
			error = true;
			return;
		}
		for (j = 0; j < plen (c); j++)
			argv[i][j] = c[j];
		argv[i][j] = '\0';
	}
}


void ParseParamFile (const int argc, char ** const argv, int &argcnew, char **&argvnew)
{
	char8_t token[MAXTOKEN], words[MAXTOKEN], cmdline[MAXTOKEN];

    std::filesystem::path settingsFilePath = get_path_to_directory_with_executable(argv) / paramfilename;


	if (auto f = read_utf8_file(settingsFilePath, true))
	{
		const char8_t* c = f.value().c_str();
		execute_t e;
		memset (&e, 0, sizeof (e));
		words[0] = u8'\0';
		token[0] = u8'\0';
		parsearg (argc, argv, cmdline, MAXTOKEN);
		while (1)
		{
			while (1)
			{
				c = nextword (c, token, MAXTOKEN);
				if (token[0] == '#' || !c)
					break;
			}
			if (!c)
				break;
			if (strlen ((const char*) token) + strlen (SEPSTR) + 1 <= MAXTOKEN)
			{
				strcpy ((char*) words, (char*)  token);
				strcat ((char*) words, SEPSTR);
			}
			else
			{
				error = true;
				break;
			}
			while (1)
			{
				const char8_t* c0 = c;
				c = nextword (c, token, MAXTOKEN);
				if (token[0] == '#' || !c)
				{
					c = c0;
					break;
				}
				if (strlen ((const char*) words) + strlen ((const char*) token) + strlen (SEPSTR) + 1 <= MAXTOKEN)
				{
					strcat ((char*) words, (char*) token);
					strcat ((char*) words, SEPSTR);
				}
				else
				{
					error = true;
					break;
				}
			}
			parsecommand (e, cmdline, words, MAXTOKEN);
		}
		unparsearg (argcnew, argvnew, cmdline);
		if (error)
		{
			argvnew = argv;
			argcnew = argc;
		}
		argvnew[0] = argv[0];
	}
	else
	{
		argvnew = argv;
		argcnew = argc;
	}
}
