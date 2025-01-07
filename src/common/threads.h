#pragma once

#include "cmdlib.h" //--vluzacn

constexpr std::size_t MAX_THREADS = 64;

enum class q_threadpriority
{
    eThreadPriorityLow = -1,
    eThreadPriorityNormal,
    eThreadPriorityHigh
};

typedef void (*q_threadfunction) (int);


extern std::ptrdiff_t g_numthreads;
extern q_threadpriority g_threadpriority;

extern void     ThreadSetPriority(q_threadpriority type);
void ThreadSetDefault();
extern int      GetThreadWork();
extern void     ThreadLock();
extern void     ThreadUnlock();

extern void     RunThreadsOnIndividual(int workcnt, bool showpacifier, q_threadfunction);
extern void     RunThreadsOn(int workcnt, bool showpacifier, q_threadfunction);

#define NamedRunThreadsOn(n,p,f) { Log("%s\n", (#f ":")); RunThreadsOn(n,p,f); }
#define NamedRunThreadsOnIndividual(n,p,f) { Log("%s\n", (#f ":")); RunThreadsOnIndividual(n,p,f); }
