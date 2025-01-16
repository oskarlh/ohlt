#pragma once

#include "cmdlib.h" //--vluzacn

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <span>

// Is this needed?
#include <stdlib.h>

// TODO: Remove all uses of this function
inline char* c_strdup(char const * from) {
	std::size_t const numBytes = std::strlen(from) + 1;
	char* dup = (char*) std::malloc(numBytes);
	std::ranges::copy(std::span(from, numBytes), dup);
	return dup;
}

#ifdef SYSTEM_POSIX
#define _MAX_PATH 4096

#define _open  open	 //--vluzacn
#define _read  read	 //--vluzacn
#define _close close //--vluzacn

#define FORMAT_PRINTF(STRING_INDEX, FIRST_TO_CHECK)              \
	__attribute__((format(printf, STRING_INDEX, FIRST_TO_CHECK)) \
	) //--vluzacn

#endif
/////////////////////////////
