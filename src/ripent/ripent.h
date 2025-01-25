#include "bspfile.h"
#include "cmdlib.h"
#include "cmdlinecfg.h"
#include "filelib.h"
#include "hlassert.h"
#include "log.h"
#include "mathlib.h"
#include "messages.h"
#include "scriplib.h"
#include "threads.h"
#include "win32fix.h"

#define DEFAULT_PARSE					false
#define DEFAULT_TEXTUREPARSE			false
#define DEFAULT_WRITEEXTENTFILE			false
#define DEFAULT_DELETEEMBEDDEDLIGHTMAPS false
