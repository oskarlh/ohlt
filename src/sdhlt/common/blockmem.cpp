


/// ********* POSIX **********

#ifdef SYSTEM_POSIX
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#ifdef STDC_HEADERS
#include <stdlib.h>
#endif
#include "cmdlib.h"
#include "messages.h"
#include "log.h"

// =====================================================================================
//  AllocBlock
// =====================================================================================
void*           AllocBlock(const unsigned long size)
{
    if (!size)
    {
        Warning("Attempting to allocate 0 bytes");
    }
    return calloc(1, size);
}

// =====================================================================================
//  FreeBlock
// =====================================================================================
bool            FreeBlock(void* pointer)
{
    if (!pointer)
    {
        Warning("Freeing a null pointer");
    }
    free(pointer);
    return true;
}

// =====================================================================================
//  Alloc
// =====================================================================================
void*           Alloc(const unsigned long size)
{
    return AllocBlock(size);
}

// =====================================================================================
//  Free
// =====================================================================================
bool            Free(void* pointer)
{
    return FreeBlock(pointer);
}

#endif /// ********* POSIX **********
