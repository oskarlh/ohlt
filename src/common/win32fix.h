#pragma once

#include <cstdlib>
#include <cstring>

// Is this needed?
#include <stdlib.h>

#ifdef SYSTEM_POSIX
#define _MAX_PATH 4096

#define FORMAT_PRINTF(STRING_INDEX, FIRST_TO_CHECK)              \
	__attribute__((format(printf, STRING_INDEX, FIRST_TO_CHECK)) \
	) //--vluzacn

#endif
/////////////////////////////
