#include "cmdlinecfg.h"

#include "filelib.h"
#include "log.h"
#include "utf8.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace std::literals;

constexpr std::u8string_view paramfilename = u8"settings.txt";

constexpr std::size_t plen(std::u8string_view p) noexcept {
	return p.find(u8'\n');
}

constexpr bool pvalid(std::u8string_view p) noexcept {
	return p.contains(u8'\n');
}

bool pmatch(char8_t const * cmdlineparam, char8_t const * param) {
	int cl, cstart, cend, pl, pstart, pend, k;
	cl = plen(cmdlineparam);
	pl = plen(param);
	if (cl == std::u8string_view::npos || pl == std::u8string_view::npos) {
		return false;
	}
	bool anystart = (pl > 0 && param[0] == '*');
	bool anyend = (pl > 0 && param[pl - 1] == '*');
	pstart = anystart ? 1 : 0;
	pend = anyend ? pl - 1 : pl;
	if (pend < pstart) {
		pend = pstart;
	}
	for (cstart = 0; cstart <= cl; ++cstart) {
		for (cend = cl; cend >= cstart; --cend) {
			if (cend - cstart == pend - pstart) {
				for (k = 0; k < cend - cstart; ++k) {
					if (tolower(cmdlineparam[k + cstart])
						!= tolower(param[k + pstart])) {
						break;
					}
				}
				if (k == cend - cstart) {
					return true;
				}
			}
			if (!anyend) {
				break;
			}
		}
		if (!anystart) {
			break;
		}
	}
	return false;
}

static char8_t* pnext(char8_t* p) {
	return p + (plen(p) + 1);
}

static char8_t const * pnext(char8_t const * p) {
	return p + (plen(p) + 1);
}

char8_t* findparams(char8_t* cmdlineparams, char8_t* params) {
	for (char8_t* c1 = cmdlineparams; pvalid(c1); c1 = pnext(c1)) {
		char8_t* p = params;
		for (char8_t* c = c1; pvalid(p); c = pnext(c), p = pnext(p)) {
			if (!pvalid(c) || !pmatch(c, p)) {
				break;
			}
		}
		if (!pvalid(p)) {
			return c1;
		}
	}
	return nullptr;
}

void addparams(
	char8_t* cmdline, char8_t* params, unsigned int n, bool& error
) {
	if (strlen((char const *) cmdline) + strlen((char const *) params) + 1
		<= n) {
		strcat((char*) cmdline, (char*) params);
	} else {
		error = true;
	}
}

void delparams(char8_t* cmdline, char8_t* params) {
	char8_t *c, *p;
	if (!pvalid(params)) { // avoid infinite loop
		return;
	}
	while (cmdline = findparams(cmdline, params), cmdline != nullptr) {
		for (c = cmdline, p = params; pvalid(p); c = pnext(c), p = pnext(p))
			;
		memmove(cmdline, c, strlen((char*) c) + 1);
	}
}
enum class command_t {
	IFDEF,
	IFNDEF,
	ELSE,
	ENDIF,
	DEFINE,
	UNDEF
};

struct execute_t final {
	int stack;
	bool skip;
	int skipstack;
};

void parsecommand(
	execute_t& e,
	char8_t* cmdline,
	char8_t* words,
	unsigned int n,
	bool& error
) {
	if (!pvalid(words)) {
		return;
	}

	command_t t;
	if (pmatch(words, u8"#ifdef\n")) {
		t = command_t::IFDEF;
	} else if (pmatch(words, u8"#ifndef\n")) {
		t = command_t::IFNDEF;
	} else if (pmatch(words, u8"#else\n")) {
		t = command_t::ELSE;
	} else if (pmatch(words, u8"#endif\n")) {
		t = command_t::ENDIF;
	} else if (pmatch(words, u8"#define\n")) {
		t = command_t::DEFINE;
	} else if (pmatch(words, u8"#undef\n")) {
		t = command_t::UNDEF;
	} else {
		return;
	}
	if (t == command_t::IFDEF || t == command_t::IFNDEF) {
		e.stack++;
		if (!e.skip) {
			if (t == command_t::IFDEF && findparams(cmdline, pnext(words))
				|| (t == command_t::IFNDEF
					&& !findparams(cmdline, pnext(words)))) {
				e.skip = false;
			} else {
				e.skipstack = e.stack;
				e.skip = true;
			}
		}
	} else if (t == command_t::ELSE) {
		if (e.skip) {
			if (e.stack == e.skipstack) {
				e.skip = false;
			}
		} else {
			e.skipstack = e.stack;
			e.skip = true;
		}
	} else if (t == command_t::ENDIF) {
		if (e.skip) {
			if (e.stack == e.skipstack) {
				e.skip = false;
			}
		}
		e.stack--;
	} else if (!e.skip) {
		if (t == command_t::DEFINE) {
			addparams(cmdline, pnext(words), n, error);
		}
		if (t == command_t::UNDEF) {
			delparams(cmdline, pnext(words));
		}
	}
}

char8_t const *
nextword(char8_t const * s, char8_t* token, unsigned int n, bool& error) {
	unsigned int i;
	char8_t const * c;
	bool quote, comment, content;
	for (c = s, i = 0, quote = false, comment = false, content = false;
		 c[0] != '\0';
		 c++) {
		if (c[0] == u8'\"') {
			quote = !quote;
		}
		if (c[0] == u8'\n') {
			quote = false;
		}
		if (c[0] == u8'\n') {
			comment = false;
		}
		if (!quote && c[0] == u8'/' && c[1] == u8'/') {
			comment = true;
		}
		if (!comment && !(c[0] == u8'\n' || is_ascii_whitespace(c[0]))) {
			content = true;
		}
		if (!quote && !comment && content
			&& (c[0] == u8'\n' || is_ascii_whitespace(c[0]))) {
			break;
		}
		if (content && c[0] != u8'\"') {
			if (i < n - 1) {
				token[i++] = c[0];
			} else {
				error = true;
			}
		}
	}
	token[i] = u8'\0';
	return content ? c : nullptr;
}

void parsearg(
	int argc, char** argv, char8_t* cmdline, unsigned int n, bool& error
) {
	int i;
	strcpy((char*) cmdline, "");
	strcat((char*) cmdline, "<");
	strcat((char*) cmdline, (char const *) g_Program.data());
	strcat((char*) cmdline, ">");
	strcat((char*) cmdline, "\n");
	for (i = 1; i < argc; ++i) {
		if (strlen((char*) cmdline) + strlen(argv[i]) + u8"\n"sv.length()
				+ 1
			<= n) {
			strcat((char*) cmdline, argv[i]);
			strcat((char*) cmdline, "\n");
		} else {
			error = true;
		}
	}
}

void unparsearg(int& argc, char**& argv, char8_t* cmdline, bool& error) {
	// TODO: Conversion from native encoding to UTF-8 here

	int i, j;
	i = 0;
	for (char8_t const * c = cmdline; pvalid(c); c = pnext(c)) {
		i++;
	}
	argc = i;
	argv = (char**) malloc(argc * sizeof(char*));
	if (!argv) {
		error = true;
		return;
	}
	for (char8_t *c = cmdline, i = 0; pvalid(c); c = pnext(c), i++) {
		std::size_t const plenc = plen(c);
		argv[i] = (char*) malloc(plenc + 1);
		if (!argv[i]) {
			error = true;
			return;
		}
		for (j = 0; j < plenc; j++) {
			argv[i][j] = c[j];
		}
		argv[i][j] = '\0';
	}
}

void ParseParamFile(
	int const argc, char** const argv, int& argcnew, char**& argvnew
) {
	bool error = false;

	// MAXTOKEN is arbitrary. TODO: Replace strings with
	// std::u8string/std::u8string_view
	constexpr std::size_t MAXTOKEN = 4444;
	char8_t token[MAXTOKEN], words[MAXTOKEN], cmdline[MAXTOKEN];

	std::filesystem::path settingsFilePath
		= get_path_to_directory_with_executable(argv)
		/ std::filesystem::path{ paramfilename,
								 std::filesystem::path::generic_format };

	if (auto f = read_utf8_file(settingsFilePath, true)) {
		char8_t const * c = f.value().c_str();
		execute_t e{};
		words[0] = u8'\0';
		token[0] = u8'\0';
		parsearg(argc, argv, cmdline, MAXTOKEN, error);
		while (1) {
			while (1) {
				c = nextword(c, token, MAXTOKEN, error);
				if (token[0] == '#' || !c) {
					break;
				}
			}
			if (!c) {
				break;
			}
			if (strlen((char const *) token) + u8"\n"sv.length() + 1
				<= MAXTOKEN) {
				strcpy((char*) words, (char*) token);
				strcat((char*) words, "\n");
			} else {
				error = true;
				break;
			}
			while (1) {
				char8_t const * c0 = c;
				c = nextword(c, token, MAXTOKEN, error);
				if (token[0] == '#' || !c) {
					c = c0;
					break;
				}
				if (strlen((char const *) words)
						+ strlen((char const *) token) + u8"\n"sv.length()
						+ 1
					<= MAXTOKEN) {
					strcat((char*) words, (char*) token);
					strcat((char*) words, "\n");
				} else {
					error = true;
					break;
				}
			}
			parsecommand(e, cmdline, words, MAXTOKEN, error);
		}
		unparsearg(argcnew, argvnew, cmdline, error);
		if (error) {
			argvnew = argv;
			argcnew = argc;
		}
		argvnew[0] = argv[0];
	} else {
		argvnew = argv;
		argcnew = argc;
	}
}
