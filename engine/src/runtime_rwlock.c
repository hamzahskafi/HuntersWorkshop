/* Host rwlocks (pthread rwlocks on Linux, SRW locks on Windows) behind the PS4 pointer-to-handle
 * ABI.
 *
 * 0.4.6 beta 18 (docs/handoffs-and-placement.md): no lock is shared between rwlocks.
 * - A handle points straight at its record, in a fixed pool, so validating it is a range check and a
 *   state word: no registry walk. Records are reused, never freed. A statically initialized handle
 *   (0) gets one on first use by a compare-and-swap; when two threads race, the loser's record goes
 *   back to the pool.
 * - What each thread holds lives in a table of its own (thread-local): no lock, no allocation in play.
 *   It gives the PS4's answers (EDEADLK, EBUSY for a try, EPERM) and tells unlock which mode to
 *   release.
 * - EBUSY on destroy comes from two counters per record: threads holding it, and threads on their way
 *   in. Destroy claims the record first (LIVE -> DEAD), so a thread on its way in either sees it dead
 *   or is seen.
 * Before, every lock and unlock took one process-wide lock twice, walked the registry and allocated
 * or freed a holder record: about 214,000 a second in play.
 * BB_RUNTIME_SYNC=0: the implementation before beta 18 (runtime_rwlock_legacy.c), this beta only. */
#define _GNU_SOURCE
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

uintptr_t legacy_rwlock_resolve(const char *name);
void legacy_rwlock_report(void);
void legacy_rwlock_census(uint64_t *live, uint64_t *found, uint64_t *steps);

typedef struct { int64_t seconds, nanoseconds; } GuestTime;
#ifdef _WIN32
typedef SRWLOCK NativeRwlock;
static int native_init(NativeRwlock *l) { InitializeSRWLock(l); return 0; }
static int native_destroy(NativeRwlock *l) { (void)l; return 0; }
static int native_try(NativeRwlock *l, int writer) {
    return (writer ? TryAcquireSRWLockExclusive(l) : TryAcquireSRWLockShared(l)) ? 0 : EBUSY;
}
static int native_lock(NativeRwlock *l, int writer) {
    if (writer) AcquireSRWLockExclusive(l); else AcquireSRWLockShared(l);
    return 0;
}
/* SRW locks have no timed acquire: retry until the realtime deadline. */
static int native_timedlock(NativeRwlock *l, int writer, const GuestTime *time) {
    uint64_t deadline = (uint64_t)time->seconds * 1000000000 + (uint64_t)time->nanoseconds;
    for (unsigned attempt = 0;; ++attempt) {
        if (!native_try(l, writer)) return 0;
        if (host_realtime_ns() >= deadline) return ETIMEDOUT;
        if (attempt < 64) YieldProcessor(); else if (attempt < 128) SwitchToThread(); else Sleep(1);
    }
}
static int native_unlock(NativeRwlock *l, int writer) {
    if (writer) ReleaseSRWLockExclusive(l); else ReleaseSRWLockShared(l);
    return 0;
}
#else
typedef pthread_rwlock_t NativeRwlock;
static int native_init(NativeRwlock *l) { return pthread_rwlock_init(l, NULL); }
static int native_destroy(NativeRwlock *l) { return pthread_rwlock_destroy(l); }
static int native_try(NativeRwlock *l, int writer) { return writer ? pthread_rwlock_trywrlock(l) : pthread_rwlock_tryrdlock(l); }
static int native_lock(NativeRwlock *l, int writer) { return writer ? pthread_rwlock_wrlock(l) : pthread_rwlock_rdlock(l); }
static int native_timedlock(NativeRwlock *l, int writer, const GuestTime *time) {
    struct timespec deadline = {.tv_sec = (time_t)time->seconds, .tv_nsec = (long)time->nanoseconds};
    return writer ? pthread_rwlock_timedwrlock(l, &deadline) : pthread_rwlock_timedrdlock(l, &deadline);
}
static int native_unlock(NativeRwlock *l, int writer) { (void)writer; return pthread_rwlock_unlock(l); }
#endif

enum { FREE, LIVE, DEAD };
/* A cache line (or two) each: two rwlocks never share one. */
typedef struct __attribute__((aligned(64))) Rwlock {
    NativeRwlock native;
    uint32_t state;    /* FREE, LIVE, or DEAD while being destroyed */
    uint32_t holders;  /* threads holding it */
    uint32_t inflight; /* threads on their way in */
    uint32_t next_free;
    uint64_t reads, writes, unlocks; /* beside the lock's own word: no extra line touched */
} Rwlock;
enum { POOL = 4096 };
static Rwlock pool[POOL];
static HostMutex pool_lock = HOST_MUTEX_INIT; /* making and destroying only */
static uint32_t pool_used, free_head = UINT32_MAX;
static uint64_t created, destroyed; /* under pool_lock */

static int32_t error(int e) {
    if (!e) return 0;
    unsigned code;
    switch (e) {
    case EPERM: code = 1; break;
    case EDEADLK: code = 11; break;
    case ENOMEM: code = 12; break;
    case EBUSY: code = 16; break;
    case EINVAL: code = 22; break;
    case EAGAIN: code = 35; break;
    case ETIMEDOUT: code = 60; break;
    default: fprintf(stderr, "STOP: unmapped rwlock error %d\n", e); exit(21);
    }
    return (int32_t)(UINT32_C(0x80020000) | code);
}

/* A record from the pool, initialized and LIVE; NULL when none is left. */
static Rwlock *make(void) {
    host_lock(&pool_lock);
    uint32_t index;
    if (free_head != UINT32_MAX) { index = free_head; free_head = pool[index].next_free; }
    else if (pool_used < POOL) index = pool_used++;
    else { host_unlock(&pool_lock); return NULL; }
    Rwlock *r = &pool[index];
    if (native_init(&r->native)) { r->next_free = free_head; free_head = index; host_unlock(&pool_lock); return NULL; }
    r->holders = r->inflight = 0;
    __atomic_store_n(&r->state, LIVE, __ATOMIC_RELEASE);
    ++created;
    host_unlock(&pool_lock);
    return r;
}
static void unmake(Rwlock *r, int counted) {
    host_lock(&pool_lock);
    __atomic_store_n(&r->state, FREE, __ATOMIC_RELEASE);
    r->next_free = free_head; free_head = (uint32_t)(r - pool);
    if (counted) ++destroyed; else --created; /* a lost race on first use was never the guest's */
    host_unlock(&pool_lock);
}
/* The record a handle's value names, or NULL (unknown, or not LIVE). */
static Rwlock *record(uintptr_t value) {
    const uintptr_t base = (uintptr_t)pool;
    if (value < base || value >= base + sizeof pool || (value - base) % sizeof(Rwlock)) return NULL;
    Rwlock *r = (Rwlock *)value;
    return __atomic_load_n(&r->state, __ATOMIC_ACQUIRE) == LIVE ? r : NULL;
}

/* The calling thread's holds: a few entries in place, more on the heap (once, if ever). */
typedef struct { Rwlock *lock; unsigned readers, writer; } Held;
enum { HELD_INLINE = 8 };
static _Thread_local Held held_inline[HELD_INLINE];
static _Thread_local Held *held = NULL;
static _Thread_local unsigned held_count, held_capacity;
static Held *held_find(Rwlock *r) {
    Held *table = held ? held : held_inline;
    for (unsigned i = 0; i < held_count; ++i) if (table[i].lock == r) return &table[i];
    return NULL;
}
static Held *held_add(Rwlock *r) {
    if (!held) { held = held_inline; held_capacity = HELD_INLINE; }
    if (held_count == held_capacity) {
        const unsigned capacity = held_capacity * 2;
        Held *grown = malloc(sizeof(Held) * capacity);
        if (!grown) return NULL;
        memcpy(grown, held, sizeof(Held) * held_count);
        if (held != held_inline) free(held);
        held = grown; held_capacity = capacity;
    }
    held[held_count] = (Held){r, 0, 0};
    return &held[held_count++];
}
static void held_remove(Held *h) {
    *h = held[--held_count];
}

static ABI int32_t rw_init(Rwlock **out, void *const *attr, const char *name) {
    (void)name;
    if (!out || (attr && !*attr)) return error(EINVAL);
    if (attr) { fputs("STOP: non-default PS4 rwlock attributes are not implemented\n", stderr); exit(21); }
    /* POSIX allows uninitialized storage, so never read *out during init. */
    Rwlock *r = make();
    if (!r) return error(ENOMEM);
    *out = r;
    return 0;
}
/* The handle's record; a statically initialized handle (0) gets one now. */
static Rwlock *resolve(Rwlock **handle, int *e) {
    uintptr_t value = __atomic_load_n((uintptr_t *)handle, __ATOMIC_ACQUIRE);
    if (!value) {
        Rwlock *made = make();
        if (!made) { *e = ENOMEM; return NULL; }
        uintptr_t expected = 0;
        if (__atomic_compare_exchange_n((uintptr_t *)handle, &expected, (uintptr_t)made, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return made;
        unmake(made, 0); /* another thread installed one first */
        value = expected;
    }
    Rwlock *r = record(value);
    if (!r) *e = EINVAL;
    return r;
}

enum Operation { READ, WRITE, TRY_READ, TRY_WRITE, TIMED_READ, TIMED_WRITE };
static int32_t acquire(Rwlock **handle, enum Operation op, const GuestTime *time) {
    if (!handle) return error(EINVAL);
    const int writer = op == WRITE || op == TRY_WRITE || op == TIMED_WRITE;
    const int attempt = op == TRY_READ || op == TRY_WRITE;
    const int timed = op == TIMED_READ || op == TIMED_WRITE;
    int e = 0;
    Rwlock *r = resolve(handle, &e);
    if (!r) return error(e);
    Held *h = held_find(r);
    if (h && (h->writer || (writer && h->readers))) return error(attempt ? EBUSY : EDEADLK);
    if (h && h->readers == UINT_MAX) return error(EAGAIN);
    __atomic_add_fetch(&r->inflight, 1, __ATOMIC_ACQ_REL);
    if (__atomic_load_n(&r->state, __ATOMIC_ACQUIRE) != LIVE) {
        /* Destroyed meanwhile (a guest's race): as an unknown handle. */
        __atomic_sub_fetch(&r->inflight, 1, __ATOMIC_ACQ_REL);
        return error(EINVAL);
    }
    /* Timed operations first try the lock, matching POSIX timeout validation. */
    if (attempt || timed) e = native_try(&r->native, writer);
    else e = native_lock(&r->native, writer);
    if (timed && e == EBUSY) {
        if (!time || time->nanoseconds < 0 || time->nanoseconds >= 1000000000 || time->seconds < 0) e = EINVAL;
        else e = native_timedlock(&r->native, writer, time);
    }
    if (!e && !h) {
        h = held_add(r);
        if (!h) { native_unlock(&r->native, writer); e = ENOMEM; }
        else __atomic_add_fetch(&r->holders, 1, __ATOMIC_ACQ_REL);
    }
    __atomic_sub_fetch(&r->inflight, 1, __ATOMIC_ACQ_REL);
    if (!e) {
        if (writer) { h->writer = 1; __atomic_add_fetch(&r->writes, 1, __ATOMIC_RELAXED); }
        else { ++h->readers; __atomic_add_fetch(&r->reads, 1, __ATOMIC_RELAXED); }
    }
    return error(e);
}
static ABI int32_t rw_read(Rwlock **r) { return acquire(r, READ, NULL); }
static ABI int32_t rw_write(Rwlock **r) { return acquire(r, WRITE, NULL); }
static ABI int32_t rw_tryread(Rwlock **r) { return acquire(r, TRY_READ, NULL); }
static ABI int32_t rw_trywrite(Rwlock **r) { return acquire(r, TRY_WRITE, NULL); }
static ABI int32_t rw_timedread(Rwlock **r, const GuestTime *t) { return acquire(r, TIMED_READ, t); }
static ABI int32_t rw_timedwrite(Rwlock **r, const GuestTime *t) { return acquire(r, TIMED_WRITE, t); }
static ABI int32_t rw_unlock(Rwlock **handle) {
    if (!handle) return error(EINVAL);
    Rwlock *r = record(__atomic_load_n((uintptr_t *)handle, __ATOMIC_ACQUIRE));
    if (!r) return error(EINVAL);
    Held *h = held_find(r);
    if (!h || (!h->readers && !h->writer)) return error(EPERM);
    const int e = native_unlock(&r->native, h->writer);
    if (e) return error(e);
    if (h->writer) h->writer = 0; else --h->readers;
    __atomic_add_fetch(&r->unlocks, 1, __ATOMIC_RELAXED);
    if (!h->writer && !h->readers) {
        held_remove(h);
        __atomic_sub_fetch(&r->holders, 1, __ATOMIC_ACQ_REL);
    }
    return 0;
}
static ABI int32_t rw_destroy(Rwlock **handle) {
    if (!handle) return error(EINVAL);
    const uintptr_t value = __atomic_load_n((uintptr_t *)handle, __ATOMIC_ACQUIRE);
    if (!value) return 0;
    Rwlock *r = record(value);
    if (!r) return error(EINVAL);
    uint32_t live = LIVE;
    if (!__atomic_compare_exchange_n(&r->state, &live, DEAD, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return error(EINVAL);
    if (__atomic_load_n(&r->inflight, __ATOMIC_ACQUIRE) || __atomic_load_n(&r->holders, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(&r->state, LIVE, __ATOMIC_RELEASE);
        return error(EBUSY);
    }
    const int e = native_destroy(&r->native);
    if (e) { __atomic_store_n(&r->state, LIVE, __ATOMIC_RELEASE); return error(e); }
    *handle = (Rwlock *)(uintptr_t)1;
    unmake(r, 1);
    return 0;
}
uintptr_t runtime_rwlock_resolve(const char *name) {
    if (runtime_sync_legacy()) return legacy_rwlock_resolve(name);
    if (!strcmp(name, "6ULAa0fq4jA#p#J")) return (uintptr_t)rw_init;
    if (!strcmp(name, "BB+kb08Tl9A#p#J")) return (uintptr_t)rw_destroy;
    if (!strcmp(name, "Ox9i0c7L5w0#p#J")) return (uintptr_t)rw_read;
    if (!strcmp(name, "mqdNorrB+gI#p#J")) return (uintptr_t)rw_write;
    if (!strcmp(name, "XD3mDeybCnk#p#J")) return (uintptr_t)rw_tryread;
    if (!strcmp(name, "bIHoZCTomsI#p#J")) return (uintptr_t)rw_trywrite;
    if (!strcmp(name, "+L98PIbGttk#p#J")) return (uintptr_t)rw_unlock;
    if (!strcmp(name, "iPtZRWICjrM#p#J")) return (uintptr_t)rw_timedread;
    if (!strcmp(name, "adh--6nIqTk#p#J")) return (uintptr_t)rw_timedwrite;
    return 0;
}
static void totals(uint64_t *live, uint64_t *reads, uint64_t *writes, uint64_t *unlocks) {
    *live = *reads = *writes = *unlocks = 0;
    const uint32_t used = __atomic_load_n(&pool_used, __ATOMIC_RELAXED);
    for (uint32_t i = 0; i < used && i < POOL; ++i) {
        *live += __atomic_load_n(&pool[i].state, __ATOMIC_RELAXED) == LIVE;
        *reads += __atomic_load_n(&pool[i].reads, __ATOMIC_RELAXED);
        *writes += __atomic_load_n(&pool[i].writes, __ATOMIC_RELAXED);
        *unlocks += __atomic_load_n(&pool[i].unlocks, __ATOMIC_RELAXED);
    }
}
/* bbport: rwlocks alive now, and lookups (locks and unlocks) and the steps they took (one each). */
void runtime_rwlock_census(uint64_t *live, uint64_t *found, uint64_t *steps) {
    if (runtime_sync_legacy()) { legacy_rwlock_census(live, found, steps); return; }
    uint64_t reads, writes, unlocks;
    totals(live, &reads, &writes, &unlocks);
    *found = *steps = reads + writes + unlocks;
}
void runtime_rwlock_report(void) {
    if (runtime_sync_legacy()) { legacy_rwlock_report(); return; }
    uint64_t live, reads, writes, unlocks;
    totals(&live, &reads, &writes, &unlocks);
    host_lock(&pool_lock);
    printf("Runtime: rwlocks created=%llu, destroyed=%llu, reads=%llu, writes=%llu, unlocks=%llu\n",
           (unsigned long long)created, (unsigned long long)destroyed, (unsigned long long)reads,
           (unsigned long long)writes, (unsigned long long)unlocks);
    host_unlock(&pool_lock);
}
