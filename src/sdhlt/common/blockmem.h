#ifndef BLOCKMEM_H__
#define BLOCKMEM_H__
#include "cmdlib.h" //--vluzacn

#if _MSC_VER >= 1000
#pragma once
#endif

extern void*    AllocBlock(unsigned long size);
extern bool     FreeBlock(void* pointer);

extern void*    Alloc(unsigned long size);
extern bool     Free(void* pointer);

#endif // BLOCKMEM_H__
