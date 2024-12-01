#pragma once
#include "cmdlib.h"


extern void*    AllocBlock(unsigned long size);

extern void*    Alloc(unsigned long size);
extern bool     Free(void* pointer);
