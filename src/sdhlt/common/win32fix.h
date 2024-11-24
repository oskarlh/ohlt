#pragma once

#include "cmdlib.h" //--vluzacn

/////////////////////////////
#ifdef SYSTEM_WIN32
#include <malloc.h>

#define alloca      _alloca

#define strncasecmp _strnicmp
#define strcasecmp  _stricmp

#if _MSC_VER < 1400 // AdamR: Ignore this definition in Visual Studio 2005 and later
#define snprintf  _snprintf
#define vsnprintf _vsnprintf
#endif

#define finite    _finite

#define rotl      _rotl
#define rotr      _rotr

#undef STDCALL
#undef FASTCALL
#undef CDECL

#define STDCALL     __stdcall
#define FASTCALL    __fastcall
#define CDECL       __cdecl

#define INLINE      __inline

#define FORCEINLINE	__forceinline //--vluzacn
#define FORMAT_PRINTF(STRING_INDEX,FIRST_TO_CHECK) //--vluzacn

#else
// Is this needed?
#include <stdlib.h>
#endif
/////////////////////////////

/////////////////////////////
#ifdef SYSTEM_POSIX
#define _MAX_PATH  4096
#define _MAX_DRIVE 4096
#define _MAX_DIR   4096
#define _MAX_FNAME 4096
#define _MAX_EXT   4096

#define STDCALL
#define FASTCALL
#define CDECL

#define INLINE inline

#define _strdup strdup //--vluzacn
#define _strupr strupr //--vluzacn
#define _strlwr strlwr //--vluzacn
#define _open open //--vluzacn
#define _read read //--vluzacn
#define _close close //--vluzacn
#define _unlink unlink //--vluzacn

#define FORCEINLINE __inline__ __attribute__((always_inline))  //--vluzacn
#define FORMAT_PRINTF(STRING_INDEX,FIRST_TO_CHECK) __attribute__((format (printf, STRING_INDEX, FIRST_TO_CHECK))) //--vluzacn

#endif
/////////////////////////////
