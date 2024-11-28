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
    return new std::byte[size]();
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
    delete[] (std::byte*) pointer;
    return true;
}

// =====================================================================================
//  Alloc
// =====================================================================================
void*           Alloc(const unsigned long size)
{
    return new std::byte[size]();
}

// =====================================================================================
//  Free
// =====================================================================================
bool            Free(void* pointer)
{
    delete[] (std::byte*) pointer;
    return true;
}
