#pragma once

#include "cmdlib.h" //--vluzacn

// Is this needed?
#include <stdlib.h>

/////////////////////////////

/////////////////////////////
#ifdef SYSTEM_POSIX
#define _MAX_PATH  4096


#define _strdup strdup //--vluzacn
#define _strupr strupr //--vluzacn
#define _strlwr strlwr //--vluzacn
#define _open open //--vluzacn
#define _read read //--vluzacn
#define _close close //--vluzacn
#define _unlink unlink //--vluzacn

#define FORMAT_PRINTF(STRING_INDEX,FIRST_TO_CHECK) __attribute__((format (printf, STRING_INDEX, FIRST_TO_CHECK))) //--vluzacn

#endif
/////////////////////////////
