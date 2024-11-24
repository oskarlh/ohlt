#pragma once

#include "cmdlib.h" //--vluzacn

extern void*    CreateResourceLock(int LockNumber);
extern void     ReleaseResourceLock(void** lock);
