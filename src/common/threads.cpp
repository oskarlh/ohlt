#ifdef SYSTEM_WIN32
#define WIN32_LEAN_AND_MEAN
#include <malloc.h>
#include <windows.h>
#endif
#include "cli_option_defaults.h"
#include "cmdlib.h"
#include "log.h"
#include "messages.h"
#include "threads.h"

#include <thread>

#ifdef SYSTEM_POSIX
#include <pthread.h>
#include <sys/resource.h>
#endif

#include "hlassert.h"

std::ptrdiff_t g_numthreads = cli_option_defaults::numberOfThreads;
q_threadpriority g_threadpriority = cli_option_defaults::threadPriority;

#define THREADTIMES_SIZE  100
#define THREADTIMES_SIZEf (float) (THREADTIMES_SIZE)

static int dispatch = 0;
static int workcount = 0;
static int oldf = 0;
static bool pacifier = false;
static bool threaded = false;
static double threadstart = 0;
static double threadtimes[THREADTIMES_SIZE];

int GetThreadWork() {
	int r, f, i;
	double ct, finish, finish2, finish3;

	ThreadLock();

	if (dispatch == 0) {
		oldf = 0;
	}

	if (dispatch > workcount) {
		Developer(developer_level::error, "dispatch > workcount!!!\n");
		ThreadUnlock();
		return -1;
	}
	if (dispatch == workcount) {
		Developer(
			developer_level::message,
			"dispatch == workcount, work is complete\n"
		);
		ThreadUnlock();
		return -1;
	}
	if (dispatch < 0) {
		Developer(developer_level::error, "negative dispatch!!!\n");
		ThreadUnlock();
		return -1;
	}

	f = THREADTIMES_SIZE * dispatch / workcount;
	if (pacifier) {
		PrintConsole("\r%6d /%6d", dispatch, workcount);

		if (f != oldf) {
			ct = I_FloatTime();
			/* Fill in current time for threadtimes record */
			for (i = oldf; i <= f; i++) {
				if (threadtimes[i] < 1) {
					threadtimes[i] = ct;
				}
			}
			oldf = f;

			if (f > 10) {
				finish
					= (ct - threadtimes[0]) * (THREADTIMES_SIZEf - f) / f;
				finish2 = 10.0 * (ct - threadtimes[f - 10])
					* (THREADTIMES_SIZEf - f) / THREADTIMES_SIZEf;
				finish3 = THREADTIMES_SIZEf * (ct - threadtimes[f - 1])
					* (THREADTIMES_SIZEf - f) / THREADTIMES_SIZEf;

				if (finish > 1.0) {
					PrintConsole(
						"  (%d%%: est. time to completion %ld/%ld/%ld secs)   ",
						f, (long) (finish), (long) (finish2),
						(long) (finish3)
					);
				} else {
					PrintConsole(
						"  (%d%%: est. time to completion <1 sec)   ", f
					);
				}
			}
		}
	} else {
		if (f != oldf) {
			oldf = f;
			switch (f) {
				case 10:
				case 20:
				case 30:
				case 40:
				case 50:
				case 60:
				case 70:
				case 80:
				case 90:
				case 100:
					/*
								case 5:
								case 15:
								case 25:
								case 35:
								case 45:
								case 55:
								case 65:
								case 75:
								case 85:
								case 95:
					*/
					PrintConsole("%d%%...", f);
				default:
					break;
			}
		}
	}

	r = dispatch;
	dispatch++;

	ThreadUnlock();
	return r;
}

q_threadfunction workfunction;

#ifdef SYSTEM_WIN32
#pragma warning(push)
#pragma warning(disable : 4100) // unreferenced formal parameter
#endif
static void ThreadWorkerFunction(int unused) {
	int work;

	while ((work = GetThreadWork()) != -1) {
		workfunction(work);
	}
}

#ifdef SYSTEM_WIN32
#pragma warning(pop)
#endif

void RunThreadsOnIndividual(
	int workcnt, bool showpacifier, q_threadfunction func
) {
	workfunction = func;
	RunThreadsOn(workcnt, showpacifier, ThreadWorkerFunction);
}

#ifndef SINGLE_THREADED

void ThreadSetDefault() {
	if (g_numthreads <= 0) // Set the number of threads automatically
	{
		g_numthreads = std::clamp<std::ptrdiff_t>(
			std::thread::hardware_concurrency(), 1, MAX_THREADS
		);
	}
}

/*====================
| Begin SYSTEM_POSIX
=*/
#ifdef SYSTEM_POSIX

#define USED

void ThreadSetPriority(q_threadpriority type) {
	int val;

	g_threadpriority = type;

	// Currently in Linux land users are incapable of raising the priority
	// level of their processes Unless you are root -high is useless . . .
	switch (g_threadpriority) {
		case q_threadpriority::eThreadPriorityLow:
			val = PRIO_MAX;
			break;

		case q_threadpriority::eThreadPriorityHigh:
			val = PRIO_MIN;
			break;

		case q_threadpriority::eThreadPriorityNormal:
		default:
			val = 0;
			break;
	}
	setpriority(PRIO_PROCESS, 0, val);
}

typedef void* pthread_addr_t;
pthread_mutex_t* my_mutex;

void ThreadLock() {
	if (my_mutex) {
		pthread_mutex_lock(my_mutex);
	}
}

void ThreadUnlock() {
	if (my_mutex) {
		pthread_mutex_unlock(my_mutex);
	}
}

q_threadfunction q_entry;

static void* ThreadEntryStub(void* pParam) {
	q_entry((int) (intptr_t) pParam);
	return nullptr;
}

void threads_InitCrit() {
	pthread_mutexattr_t mattrib;

	if (!my_mutex) {
		my_mutex = new pthread_mutex_t{};
		if (pthread_mutexattr_init(&mattrib) == -1) {
			Error("pthread_mutex_attr_init failed");
		}
		if (pthread_mutex_init(my_mutex, &mattrib) == -1) {
			Error("pthread_mutex_init failed");
		}
	}
}

void threads_UninitCrit() {
	delete my_mutex;
	my_mutex = nullptr;
}

/*
 * =============
 * RunThreadsOn
 * =============
 */
void RunThreadsOn(int workcnt, bool showpacifier, q_threadfunction func) {
	int i;
	pthread_t work_threads[MAX_THREADS];
	pthread_addr_t status;
	pthread_attr_t attrib;
	double start, end;

	threadstart = I_FloatTime();
	start = threadstart;
	for (i = 0; i < THREADTIMES_SIZE; i++) {
		threadtimes[i] = 0;
	}

	dispatch = 0;
	workcount = workcnt;
	oldf = -1;
	pacifier = showpacifier;
	threaded = true;
	q_entry = func;

	if (pacifier) {
		setbuf(stdout, nullptr);
	}

	threads_InitCrit();

	if (pthread_attr_init(&attrib) == -1) {
		Error("pthread_attr_init failed");
	}
#ifdef _POSIX_THREAD_ATTR_STACKSIZE
	if (pthread_attr_setstacksize(&attrib, 0x40'0000) == -1) {
		Error("pthread_attr_setstacksize failed");
	}
#endif

	for (i = 0; i < g_numthreads; i++) {
		if (pthread_create(
				&work_threads[i], &attrib, ThreadEntryStub,
				(void*) (intptr_t) i
			)
			== -1) {
			Error("pthread_create failed");
		}
	}

	for (i = 0; i < g_numthreads; i++) {
		if (pthread_join(work_threads[i], &status) == -1) {
			Error("pthread_join failed");
		}
	}

	threads_UninitCrit();

	q_entry = nullptr;
	threaded = false;

	end = I_FloatTime();
	if (pacifier) {
		PrintConsole("\r%60s\r", "");
	}

	Log(" (%.2f seconds)\n", end - start);
}

#endif /*SYSTEM_POSIX */

/*=
| End SYSTEM_POSIX
=====================*/

#endif /*SINGLE_THREADED */

/*====================
| Begin SINGLE_THREADED
=*/
#ifdef SINGLE_THREADED

int g_numthreads = 1;

void ThreadSetPriority(q_threadpriority type) { }

void threads_InitCrit() { }

void threads_UninitCrit() { }

void ThreadSetDefault() {
	g_numthreads = 1;
}

void ThreadLock() { }

void ThreadUnlock() { }

void RunThreadsOn(int workcnt, bool showpacifier, q_threadfunction func) {
	int i;
	double start, end;

	dispatch = 0;
	workcount = workcnt;
	oldf = -1;
	pacifier = showpacifier;
	threadstart = I_FloatTime();
	start = threadstart;
	for (i = 0; i < THREADTIMES_SIZE; i++) {
		threadtimes[i] = 0.0;
	}

	if (pacifier) {
		setbuf(stdout, nullptr);
	}
	func(0);

	end = I_FloatTime();

	if (pacifier) {
		PrintConsole("\r%60s\r", "");
	}

	Log(" (%.2f seconds)\n", end - start);
}

#endif

/*=
| End SINGLE_THREADED
=====================*/
