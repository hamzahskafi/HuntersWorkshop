/* The runtime's own locks, condition variables, threads and clocks. Linux: pthreads and
 * clock_gettime, as before. Windows: the native primitives directly (slim reader/writer locks,
 * condition variables, CRT threads, QueryPerformanceCounter) — no POSIX layer in between. */
#ifndef BB_HOST_SYNC_H
#define BB_HOST_SYNC_H
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <time.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

typedef SRWLOCK HostMutex;
#define HOST_MUTEX_INIT SRWLOCK_INIT
static inline void host_mutex_init(HostMutex *m) { InitializeSRWLock(m); }
static inline void host_lock(HostMutex *m) { AcquireSRWLockExclusive(m); }
static inline void host_unlock(HostMutex *m) { ReleaseSRWLockExclusive(m); }
/* 1 when taken. */
static inline int host_trylock(HostMutex *m) { return TryAcquireSRWLockExclusive(m) != 0; }

/* SRW locks are not recursive: the owner and depth make this one so. */
typedef struct { SRWLOCK lock; DWORD owner; unsigned depth; } HostRecursiveMutex;
#define HOST_RECURSIVE_MUTEX_INIT {SRWLOCK_INIT, 0, 0}
static inline void host_recursive_lock(HostRecursiveMutex *m) {
    DWORD self = GetCurrentThreadId();
    if (__atomic_load_n(&m->owner, __ATOMIC_RELAXED) == self) { ++m->depth; return; }
    AcquireSRWLockExclusive(&m->lock);
    __atomic_store_n(&m->owner, self, __ATOMIC_RELAXED);
    m->depth = 1;
}
static inline void host_recursive_unlock(HostRecursiveMutex *m) {
    if (--m->depth) return;
    __atomic_store_n(&m->owner, 0, __ATOMIC_RELAXED);
    ReleaseSRWLockExclusive(&m->lock);
}

typedef SRWLOCK HostRwlock;
#define HOST_RWLOCK_INIT SRWLOCK_INIT
static inline void host_read_lock(HostRwlock *l) { AcquireSRWLockShared(l); }
static inline void host_read_unlock(HostRwlock *l) { ReleaseSRWLockShared(l); }
static inline void host_write_lock(HostRwlock *l) { AcquireSRWLockExclusive(l); }
static inline void host_write_unlock(HostRwlock *l) { ReleaseSRWLockExclusive(l); }

typedef CONDITION_VARIABLE HostCond;
#define HOST_COND_INIT CONDITION_VARIABLE_INIT
static inline void host_cond_init(HostCond *c) { InitializeConditionVariable(c); }
static inline void host_cond_wait(HostCond *c, HostMutex *m) { SleepConditionVariableSRW(c, m, INFINITE, 0); }
/* 0 when woken (spuriously too), ETIMEDOUT once at least ns passed: Windows waits in whole
 * milliseconds, so the timeout is rounded up. */
static inline int host_cond_timedwait(HostCond *c, HostMutex *m, uint64_t ns) {
    uint64_t ms = (ns + 999999) / 1000000;
    if (SleepConditionVariableSRW(c, m, ms >= INFINITE ? INFINITE - 1 : (DWORD)ms, 0)) return 0;
    return GetLastError() == ERROR_TIMEOUT ? ETIMEDOUT : EINVAL;
}
static inline void host_cond_signal(HostCond *c) { WakeConditionVariable(c); }

typedef uint64_t HostThreadId;
static inline HostThreadId host_thread_id(void) { return GetCurrentThreadId(); }
static inline int host_thread_equal(HostThreadId a, HostThreadId b) { return a == b; }
static inline void host_yield(void) { SwitchToThread(); }
/* The calling thread above the others (1) or back at normal priority (0). */
static inline void host_thread_set_high_priority(int high) {
    SetThreadPriority(GetCurrentThread(), high ? THREAD_PRIORITY_HIGHEST : THREAD_PRIORITY_NORMAL);
}
typedef HANDLE HostThread;

/* 0.4.6 beta 18: a thread parks on a 32-bit word of its own while it holds `expected`, and another
 * wakes it after changing the word (WaitOnAddress / WakeByAddress: in-process, no kernel object; they
 * spin briefly before sleeping). Returns 0 when woken or the word differs, ETIMEDOUT after `ns`
 * (0: no limit; Windows waits in whole milliseconds, so it is rounded up). Spurious returns happen,
 * and a wake can reach a word whose waiter has already gone (harmless): callers check the word. Looked
 * up at run time, as MinGW declares them only for newer targets. */
typedef BOOL (WINAPI *HostWaitOnAddress)(volatile VOID *, PVOID, SIZE_T, DWORD);
typedef VOID (WINAPI *HostWakeByAddress)(PVOID);
static inline HMODULE host_synch_module(void) {
    HMODULE m = GetModuleHandleW(L"api-ms-win-core-synch-l1-2-0.dll");
    return m ? m : GetModuleHandleW(L"kernelbase.dll");
}
static inline HostWaitOnAddress host_wait_on_address(void) {
    static HostWaitOnAddress fn;
    HostWaitOnAddress f = __atomic_load_n(&fn, __ATOMIC_ACQUIRE);
    if (!f) {
        HMODULE m = host_synch_module();
        f = m ? (HostWaitOnAddress)(void *)GetProcAddress(m, "WaitOnAddress") : NULL;
        __atomic_store_n(&fn, f, __ATOMIC_RELEASE);
    }
    return f;
}
static inline HostWakeByAddress host_wake_by_address(int all) {
    static HostWakeByAddress single, every;
    HostWakeByAddress *slot = all ? &every : &single;
    HostWakeByAddress f = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
    if (!f) {
        HMODULE m = host_synch_module();
        f = m ? (HostWakeByAddress)(void *)GetProcAddress(m, all ? "WakeByAddressAll" : "WakeByAddressSingle") : NULL;
        __atomic_store_n(slot, f, __ATOMIC_RELEASE);
    }
    return f;
}
static inline int host_park(volatile uint32_t *word, uint32_t expected, uint64_t ns) {
    DWORD ms = INFINITE;
    if (ns) {
        const uint64_t m = (ns + 999999) / 1000000;
        ms = m >= INFINITE ? INFINITE - 1 : (DWORD)m;
    }
    if (host_wait_on_address()((volatile VOID *)word, &expected, sizeof expected, ms)) return 0;
    return GetLastError() == ERROR_TIMEOUT ? ETIMEDOUT : 0;
}
static inline void host_unpark_one(volatile uint32_t *word) { host_wake_by_address(0)((PVOID)word); }
static inline void host_unpark_all(volatile uint32_t *word) { host_wake_by_address(1)((PVOID)word); }
#else
#include <pthread.h>
#include <sched.h>

typedef pthread_mutex_t HostMutex;
#define HOST_MUTEX_INIT PTHREAD_MUTEX_INITIALIZER
static inline void host_mutex_init(HostMutex *m) { pthread_mutex_init(m, NULL); }
static inline void host_lock(HostMutex *m) { pthread_mutex_lock(m); }
static inline void host_unlock(HostMutex *m) { pthread_mutex_unlock(m); }
static inline int host_trylock(HostMutex *m) { return pthread_mutex_trylock(m) == 0; }

typedef pthread_mutex_t HostRecursiveMutex;
#define HOST_RECURSIVE_MUTEX_INIT PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
static inline void host_recursive_lock(HostRecursiveMutex *m) { pthread_mutex_lock(m); }
static inline void host_recursive_unlock(HostRecursiveMutex *m) { pthread_mutex_unlock(m); }

typedef pthread_rwlock_t HostRwlock;
#define HOST_RWLOCK_INIT PTHREAD_RWLOCK_INITIALIZER
static inline void host_read_lock(HostRwlock *l) { pthread_rwlock_rdlock(l); }
static inline void host_read_unlock(HostRwlock *l) { pthread_rwlock_unlock(l); }
static inline void host_write_lock(HostRwlock *l) { pthread_rwlock_wrlock(l); }
static inline void host_write_unlock(HostRwlock *l) { pthread_rwlock_unlock(l); }

typedef pthread_cond_t HostCond;
#define HOST_COND_INIT PTHREAD_COND_INITIALIZER
static inline void host_cond_init(HostCond *c) { pthread_cond_init(c, NULL); }
static inline void host_cond_wait(HostCond *c, HostMutex *m) { pthread_cond_wait(c, m); }
static inline void host_cond_signal(HostCond *c) { pthread_cond_signal(c); }

typedef pthread_t HostThreadId;
static inline HostThreadId host_thread_id(void) { return pthread_self(); }
static inline int host_thread_equal(HostThreadId a, HostThreadId b) { return pthread_equal(a, b); }
static inline void host_yield(void) { sched_yield(); }
/* Raising a thread's priority needs privileges on Linux: left to the scheduler. */
static inline void host_thread_set_high_priority(int high) { (void)high; }
typedef pthread_t HostThread;

/* 0.4.6 beta 18: parking on a word (see the Windows half): a private futex. */
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
static inline int host_park(volatile uint32_t *word, uint32_t expected, uint64_t ns) {
    struct timespec relative, *limit = NULL;
    if (ns) {
        relative.tv_sec = (time_t)(ns / 1000000000);
        relative.tv_nsec = (long)(ns % 1000000000);
        limit = &relative;
    }
    if (syscall(SYS_futex, (uint32_t *)word, FUTEX_WAIT_PRIVATE, expected, limit, NULL, 0) == -1 &&
        errno == ETIMEDOUT)
        return ETIMEDOUT;
    return 0;
}
static inline void host_unpark_one(volatile uint32_t *word) {
    syscall(SYS_futex, (uint32_t *)word, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
}
static inline void host_unpark_all(volatile uint32_t *word) {
    syscall(SYS_futex, (uint32_t *)word, FUTEX_WAKE_PRIVATE, 0x7fffffff, NULL, NULL, 0);
}
#endif

/* A thread of the runtime (not a guest thread: runtime_thread.c creates those). stack: bytes,
 * 0 for the default. Returns 0 or an errno value. */
int host_thread_start(HostThread *thread, size_t stack, void *(*entry)(void *), void *argument);
int host_thread_join(HostThread thread);
void host_thread_detach(HostThread thread);

/* Clocks: monotonic (steady, arbitrary origin) and realtime (since 1970), in nanoseconds. */
uint64_t host_monotonic_ns(void);
uint64_t host_realtime_ns(void);
void host_sleep_ns(uint64_t ns);
void host_sleep_until_ns(uint64_t monotonic_deadline);
#endif
