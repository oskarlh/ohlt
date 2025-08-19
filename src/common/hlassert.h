#pragma once

#ifdef _DEBUG

#include "log.h"

#define assume(exp, message)                                                   \
	{                                                                          \
		if (!(exp)) {                                                          \
			Log("\n***** ERROR *****\nAssume '%s' failed\n at %s:%d\n %s\n\n", \
			    #exp,                                                          \
			    __FILE__,                                                      \
			    __LINE__,                                                      \
			    message);                                                      \
			exit(-1);                                                          \
		}                                                                      \
	}
#define hlassert(exp) assume(exp, "")

#else // _DEBUG

#define assume(exp, message)                                \
	{                                                       \
		if (!(exp)) {                                       \
			Error(                                          \
				"\nAssume '%s' failed\n at %s:%d\n %s\n\n", \
				#exp,                                       \
				__FILE__,                                   \
				__LINE__,                                   \
				message                                     \
			);                                              \
		}                                                   \
	} // #define assume(exp, message) {if (!(exp)) {Error("\nAssume '%s'
	  // failed\n\n", #exp, __FILE__, __LINE__, message);}} //--vluzacn
#define hlassert(exp)

#endif // _DEBUG
