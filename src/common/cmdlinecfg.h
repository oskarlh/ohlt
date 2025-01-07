#pragma once

#include "cmdlib.h" //--vluzacn
extern void ParseParamFile (const int argc, char ** const argv, int &argcnew, char **&argvnew);

constexpr inline bool is_ascii_whitespace(char8_t c) {
	return
		c == u8' ' ||
		c == u8'\n' ||
		c == u8'\r' ||
		c == u8'\t' ||
		c == u8'\v'
	;
}
