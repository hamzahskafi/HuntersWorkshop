/* 0.4.6 beta 18: the runtime's semaphores and read/write locks under load, many threads at once
 * (docs/handoffs-and-placement.md). Modes:
 *   --barrier  the game's fork-join barrier: one signal of N wakes N workers, each counts in, and the
 *              signaller spins until all have; prints the rounds a second (old against new);
 *   --stress   semaphore waits with needs above 1 and timeouts against signals: every token is
 *              accounted for; cancel and delete under waiters; stale ids;
 *   --rwlock   readers and writers on a few locks (no writer beside anyone), a racing first use of a
 *              statically initialized lock, destroy while held.
 * With BB_RUNTIME_SYNC=0 the same run checks the implementation before beta 18. */
#define _GNU_SOURCE
#include "runtime.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The settings menu of the GPU library restarts through probe.c, which tests do not link. */
void runtime_restart(void) { abort(); }

typedef int32_t (ABI *Create)(uint32_t *, const char *, uint32_t, int32_t, int32_t, const void *);
typedef int32_t (ABI *CountOp)(uint32_t, int32_t);
typedef int32_t (ABI *Wait)(uint32_t, int32_t, uint32_t *);
typedef int32_t (ABI *Delete)(uint32_t);
typedef int32_t (ABI *Cancel)(uint32_t, int32_t, int32_t *);
typedef int32_t (ABI *HandleOp)(void **);
typedef int32_t (ABI *RwInit)(void **, void *const *, const char *);
#define GET(t, n) ((t)runtime_resolve(n, 0))
static Create create;
static CountOp poll_sem, signal_sem;
static Wait wait_sem;
static Delete delete_sem;
static Cancel cancel_sem;
static HandleOp rw_read, rw_write, rw_tryread, rw_trywrite, rw_unlock, rw_destroy;
static RwInit rw_init;

static void setup(void) {
    runtime_start(1);
    create = GET(Create, "188x57JYp0g#p#J");
    poll_sem = GET(CountOp, "12wOHk8ywb0#p#J");
    signal_sem = GET(CountOp, "4czppHBiriw#p#J");
    wait_sem = GET(Wait, "Zxa0VhQVTsk#p#J");
    delete_sem = GET(Delete, "R1Jvn8bSCW8#p#J");
    cancel_sem = GET(Cancel, "4DM06U2BNEY#p#J");
    rw_init = GET(RwInit, "6ULAa0fq4jA#p#J");
    rw_read = GET(HandleOp, "Ox9i0c7L5w0#p#J"); rw_write = GET(HandleOp, "mqdNorrB+gI#p#J");
    rw_tryread = GET(HandleOp, "XD3mDeybCnk#p#J"); rw_trywrite = GET(HandleOp, "bIHoZCTomsI#p#J");
    rw_unlock = GET(HandleOp, "+L98PIbGttk#p#J"); rw_destroy = GET(HandleOp, "BB+kb08Tl9A#p#J");
    assert(create && poll_sem && signal_sem && wait_sem && delete_sem && cancel_sem && rw_init &&
           rw_read && rw_write && rw_tryread && rw_trywrite && rw_unlock && rw_destroy);
}
static uint64_t now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}
static unsigned next_random(unsigned *state) {
    *state = *state * 1103515245u + 12345u;
    return *state >> 16;
}

/* ---- the fork-join barrier ------------------------------------------------------------------- */
enum { WORKERS = 5, ROUNDS = 20000 };
static uint32_t barrier_sem;
static _Atomic unsigned barrier_done;
static _Atomic int barrier_stop;
static void *barrier_worker(void *unused) {
    (void)unused;
    for (;;) {
        assert(wait_sem(barrier_sem, 1, NULL) == 0);
        if (atomic_load(&barrier_stop)) return NULL;
        atomic_fetch_add(&barrier_done, 1);
    }
}
static void barrier(void) {
    assert(create(&barrier_sem, "barrier", 1, 0, WORKERS, NULL) == 0);
    pthread_t threads[WORKERS];
    for (int i = 0; i < WORKERS; ++i) assert(pthread_create(&threads[i], NULL, barrier_worker, NULL) == 0);
    const uint64_t start = now_ns();
    for (int round = 0; round < ROUNDS; ++round) {
        atomic_store(&barrier_done, 0);
        assert(signal_sem(barrier_sem, WORKERS) == 0);
        const uint64_t limit = now_ns() + 5000000000ull;
        while (atomic_load(&barrier_done) < WORKERS) assert(now_ns() < limit); /* a lost wake */
    }
    const double seconds = (double)(now_ns() - start) / 1e9;
    atomic_store(&barrier_stop, 1);
    assert(signal_sem(barrier_sem, WORKERS) == 0);
    for (int i = 0; i < WORKERS; ++i) assert(pthread_join(threads[i], NULL) == 0);
    assert(runtime_sema_waiters(barrier_sem) == 0 && delete_sem(barrier_sem) == 0);
    printf("barrier: %d rounds of %d workers, %.0f rounds/s, %.1f us a round (%s)\n", ROUNDS, WORKERS,
           ROUNDS / seconds, seconds * 1e6 / ROUNDS, runtime_sync_legacy() ? "before beta 18" : "beta 18");
}

/* ---- semaphore stress ------------------------------------------------------------------------ */
enum { STRESS_WAITERS = 6, STRESS_SIGNALLERS = 3, STRESS_LOOPS = 4000, STRESS_MAX = 1000 };
static uint32_t stress_sem;
static _Atomic uint64_t tokens_signaled, tokens_acquired, timeouts;
static void *stress_waiter(void *seed) {
    unsigned state = (unsigned)(uintptr_t)seed;
    for (int i = 0; i < STRESS_LOOPS; ++i) {
        const int32_t need = 1 + (int32_t)(next_random(&state) % 3);
        uint32_t timeout = next_random(&state) % 4 ? 200 + next_random(&state) % 3000 : 0;
        const int32_t r = wait_sem(stress_sem, need, &timeout);
        if (r == 0) atomic_fetch_add(&tokens_acquired, (uint64_t)need);
        else { assert((uint32_t)r == 0x8002003c && timeout == 0); atomic_fetch_add(&timeouts, 1); }
    }
    return NULL;
}
static void *stress_signaller(void *seed) {
    unsigned state = (unsigned)(uintptr_t)seed;
    for (int i = 0; i < STRESS_LOOPS * 2; ++i) {
        const int32_t count = 1 + (int32_t)(next_random(&state) % 3);
        const int32_t r = signal_sem(stress_sem, count);
        if (r == 0) atomic_fetch_add(&tokens_signaled, (uint64_t)count);
        else assert((uint32_t)r == 0x80020016); /* would pass the maximum */
        if (next_random(&state) % 8 == 0) sched_yield();
    }
    return NULL;
}
typedef struct { uint32_t id; int32_t result; } Blocked;
static void *blocked_waiter(void *p) {
    Blocked *b = p;
    uint32_t timeout = 10000000;
    b->result = wait_sem(b->id, 1, &timeout);
    return NULL;
}
static void enrolled(uint32_t id, unsigned count) {
    const struct timespec nap = {0, 1000000};
    for (int i = 0; i < 5000; ++i) {
        if (runtime_sema_waiters(id) == count) return;
        nanosleep(&nap, NULL);
    }
    assert(!"waiter enrollment timed out");
}
static void stress(void) {
    assert(create(&stress_sem, "stress", 1, 0, STRESS_MAX, NULL) == 0);
    pthread_t threads[STRESS_WAITERS + STRESS_SIGNALLERS];
    for (int i = 0; i < STRESS_WAITERS; ++i)
        assert(pthread_create(&threads[i], NULL, stress_waiter, (void *)(uintptr_t)(17 + i)) == 0);
    for (int i = 0; i < STRESS_SIGNALLERS; ++i)
        assert(pthread_create(&threads[STRESS_WAITERS + i], NULL, stress_signaller, (void *)(uintptr_t)(101 + i)) == 0);
    for (int i = 0; i < STRESS_WAITERS + STRESS_SIGNALLERS; ++i) assert(pthread_join(threads[i], NULL) == 0);
    uint64_t left = 0;
    while (poll_sem(stress_sem, 1) == 0) ++left;
    printf("stress: %llu tokens signaled, %llu taken by waits, %llu left, %llu waits timed out\n",
           (unsigned long long)tokens_signaled, (unsigned long long)tokens_acquired, (unsigned long long)left,
           (unsigned long long)timeouts);
    assert(tokens_signaled == tokens_acquired + left);
    assert(runtime_sema_waiters(stress_sem) == 0 && delete_sem(stress_sem) == 0);

    /* Cancel and delete under waiters: every one returns, with the PS4's codes. */
    enum { BLOCKED = 8 };
    for (int pass = 0; pass < 2; ++pass) {
        uint32_t id;
        assert(create(&id, pass ? "delete" : "cancel", 1, 0, BLOCKED, NULL) == 0);
        Blocked waiters[BLOCKED];
        pthread_t blocked[BLOCKED];
        for (int i = 0; i < BLOCKED; ++i) {
            waiters[i] = (Blocked){id, 99};
            assert(pthread_create(&blocked[i], NULL, blocked_waiter, &waiters[i]) == 0);
        }
        enrolled(id, BLOCKED);
        int32_t n = -1;
        if (pass) assert(delete_sem(id) == 0);
        else assert(cancel_sem(id, 0, &n) == 0 && n == BLOCKED);
        for (int i = 0; i < BLOCKED; ++i) {
            assert(pthread_join(blocked[i], NULL) == 0);
            assert((uint32_t)waiters[i].result == (pass ? 0x8002000du : 0x80020055u));
        }
        if (pass) {
            assert((uint32_t)signal_sem(id, 1) == 0x80020003 && (uint32_t)poll_sem(id, 1) == 0x80020003);
            assert((uint32_t)delete_sem(id) == 0x80020003);
        } else assert(delete_sem(id) == 0);
    }
    /* Many create/delete cycles: ids stay unique against the stale ones. */
    uint32_t previous = 0;
    for (int i = 0; i < 20000; ++i) {
        uint32_t id;
        assert(create(&id, "cycle", 1, 1, 1, NULL) == 0 && id != previous && id != 0);
        if (previous) assert((uint32_t)poll_sem(previous, 1) == 0x80020003);
        assert(poll_sem(id, 1) == 0 && delete_sem(id) == 0);
        previous = id;
    }
}

/* ---- read/write locks ------------------------------------------------------------------------ */
enum { LOCKS = 3, RW_THREADS = 8, RW_LOOPS = 30000 };
static void *locks[LOCKS];
static _Atomic int readers[LOCKS], writers[LOCKS];
static _Atomic uint64_t shared_value[LOCKS];
static void *rw_worker(void *seed) {
    unsigned state = (unsigned)(uintptr_t)seed;
    for (int i = 0; i < RW_LOOPS; ++i) {
        const int k = (int)(next_random(&state) % LOCKS);
        const unsigned kind = next_random(&state) % 8;
        if (kind == 0 || kind == 1) {
            const int32_t r = kind ? rw_write(&locks[k]) : rw_trywrite(&locks[k]);
            if (r) { assert(kind == 0 && (uint32_t)r == 0x80020010); continue; }
            assert(atomic_fetch_add(&writers[k], 1) == 0 && atomic_load(&readers[k]) == 0);
            atomic_fetch_add(&shared_value[k], 1);
            assert(atomic_fetch_sub(&writers[k], 1) == 1);
            assert(rw_unlock(&locks[k]) == 0);
        } else {
            const int32_t r = kind == 2 ? rw_tryread(&locks[k]) : rw_read(&locks[k]);
            if (r) { assert(kind == 2 && (uint32_t)r == 0x80020010); continue; }
            atomic_fetch_add(&readers[k], 1);
            assert(atomic_load(&writers[k]) == 0);
            (void)atomic_load(&shared_value[k]);
            atomic_fetch_sub(&readers[k], 1);
            assert(rw_unlock(&locks[k]) == 0);
        }
    }
    return NULL;
}
static void *static_first_use(void *handle) {
    assert(rw_read(handle) == 0);
    assert(rw_unlock(handle) == 0);
    return NULL;
}
static void *holding_reader(void *handle) {
    assert(rw_read(handle) == 0);
    const struct timespec nap = {0, 50000000};
    nanosleep(&nap, NULL);
    assert(rw_unlock(handle) == 0);
    return NULL;
}
static void rwlocks(void) {
    for (int k = 0; k < LOCKS; ++k) {
        if (k) assert(rw_init(&locks[k], NULL, "stress") == 0);
        else locks[k] = NULL; /* the first one statically initialized */
    }
    pthread_t threads[RW_THREADS];
    for (int i = 0; i < RW_THREADS; ++i) assert(pthread_create(&threads[i], NULL, rw_worker, (void *)(uintptr_t)(7 + i)) == 0);
    for (int i = 0; i < RW_THREADS; ++i) assert(pthread_join(threads[i], NULL) == 0);
    for (int k = 0; k < LOCKS; ++k) assert(rw_destroy(&locks[k]) == 0 && (uintptr_t)locks[k] == 1);
    /* Racing first use of one statically initialized handle: one record, still usable after. */
    for (int round = 0; round < 200; ++round) {
        void *handle = NULL;
        pthread_t racers[RW_THREADS];
        for (int i = 0; i < RW_THREADS; ++i) assert(pthread_create(&racers[i], NULL, static_first_use, &handle) == 0);
        for (int i = 0; i < RW_THREADS; ++i) assert(pthread_join(racers[i], NULL) == 0);
        assert(handle != NULL && rw_write(&handle) == 0 && rw_unlock(&handle) == 0);
        assert(rw_destroy(&handle) == 0);
    }
    /* Destroy while another thread holds it. */
    void *held = NULL;
    assert(rw_init(&held, NULL, "held") == 0);
    pthread_t holder;
    assert(pthread_create(&holder, NULL, holding_reader, &held) == 0);
    const struct timespec nap = {0, 10000000};
    nanosleep(&nap, NULL);
    assert((uint32_t)rw_destroy(&held) == 0x80020010);
    assert(pthread_join(holder, NULL) == 0);
    assert(rw_destroy(&held) == 0);
    puts("rwlock: readers and writers, racing first use, destroy while held");
}

int main(int argc, char **argv) {
    setup();
    if (argc > 1 && !strcmp(argv[1], "--barrier")) barrier();
    else if (argc > 1 && !strcmp(argv[1], "--stress")) stress();
    else if (argc > 1 && !strcmp(argv[1], "--rwlock")) rwlocks();
    else { barrier(); stress(); rwlocks(); }
    puts("PASS: sync");
    return 0;
}
