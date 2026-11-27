/* Kernel semaphores: 32-bit IDs, counted tokens and FIFO waiters.
 * Priority-ordered semaphores are approximated by FIFO order.
 *
 * 0.4.6 beta 18 (docs/handoffs-and-placement.md): each semaphore has a lock of its own, found
 * without a shared one, and a waiter parks on a word of its own.
 * - IDs name slots in a fixed table, with a generation in the high bits. A slot's record is made
 *   once and reused, never freed, so a lookup with a stale ID still finds a valid record, takes its
 *   lock, sees the ID changed, and answers "no such semaphore".
 * - A signal unlinks the waiters its tokens satisfy, marks each served, lets the lock go and then
 *   wakes them. A woken waiter has nothing to take back: one wake per handoff. (Before, one lock
 *   served every semaphore, and the waker held it while waking, so each woken thread blocked on it
 *   again: two wakes per handoff, every thread queued behind one lock.)
 * - The counters live in the records, under their locks, summed at the report.
 * BB_RUNTIME_SYNC=0: the implementation before beta 18 (runtime_sema_legacy.c), this beta only. */
#define _GNU_SOURCE
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#define ERR(n) ((int32_t)(UINT32_C(0x80020000)|(n)))

uintptr_t legacy_sema_resolve(const char *name);
void legacy_sema_report(void);
void legacy_sema_census(uint64_t *live, uint64_t *found, uint64_t *steps);
unsigned legacy_sema_waiters(uint32_t id);
void legacy_sema_describe(uint32_t id, char *out, size_t size);

int runtime_sync_legacy(void) {
    static int legacy = -1;
    int value = __atomic_load_n(&legacy, __ATOMIC_RELAXED);
    if (value < 0) {
        const char *env = getenv("BB_RUNTIME_SYNC");
        value = env && env[0] == '0';
        __atomic_store_n(&legacy, value, __ATOMIC_RELAXED);
    }
    return value;
}

typedef struct Waiter {
    int32_t need, result;
    volatile uint32_t served; /* 0 while queued; 1 once served, result set (the waiter's own word) */
    struct Waiter *next;
} Waiter;
typedef struct Sema {
    HostMutex lock;
    uint32_t id; /* 0 while the slot is free */
    uint32_t attr;
    int32_t count, initial, maximum;
    unsigned waiting;
    char name[32]; /* the guest's, for the freeze report */
    Waiter *first, *last;
    /* Counted under the lock; summed at the report and the census. */
    uint64_t acquired, signaled, timed_out, operations;
} Sema;

enum { SLOT_BITS = 12, SLOTS = 1 << SLOT_BITS, GENERATIONS = 1u << (32 - SLOT_BITS) };
static Sema *slots[SLOTS];            /* published once each (release), never changed after */
static uint32_t generations[SLOTS];   /* under table_lock */
static uint32_t free_slots[SLOTS], free_count, used_slots;
static HostMutex table_lock = HOST_MUTEX_INIT; /* create and delete only */
static uint64_t created, deleted;     /* under table_lock */

static int trace(void) {
    static int on = -1;
    if (on < 0) on = getenv("BB_TRACE_SEMA") != NULL;
    return on;
}
static uint64_t now_ns(void) { return host_monotonic_ns(); }

/* The semaphore `id` names, locked; NULL when there is none (never was, or deleted). */
static Sema *lookup(uint32_t id) {
    Sema *s = __atomic_load_n(&slots[id & (SLOTS - 1)], __ATOMIC_ACQUIRE);
    if (!s) return NULL;
    host_lock(&s->lock);
    if (s->id != id || !id) {
        host_unlock(&s->lock);
        return NULL;
    }
    ++s->operations;
    return s;
}

/* The waiters `s` serves with `result` from the front, as many as `count` allows (count < 0: all).
 * Each is unlinked and marked served; its word is kept in `woken` to wake once the lock is let go.
 * Returns how many; past `capacity` the rest are woken at once, under the lock. */
enum { WAKE_BATCH = 64 };
static unsigned serve(Sema *s, int all, int32_t result, volatile uint32_t **woken, unsigned capacity) {
    unsigned n = 0;
    Waiter **link = &s->first, *previous = NULL;
    while (*link) {
        Waiter *w = *link;
        if (!all && w->need > s->count) { previous = w; link = &w->next; continue; }
        if (!all) s->count -= w->need;
        *link = w->next;
        if (s->last == w) s->last = previous;
        --s->waiting;
        if (!result) ++s->acquired; /* counted here: the woken waiter takes no lock again */
        w->result = result;
        volatile uint32_t *word = &w->served;
        __atomic_store_n(&w->served, 1, __ATOMIC_RELEASE); /* w may be gone from here on */
        if (n < capacity) woken[n] = word;
        else host_unpark_one(word);
        ++n;
    }
    return n < capacity ? n : capacity;
}
static void wake(volatile uint32_t **woken, unsigned n) {
    for (unsigned i = 0; i < n; ++i) host_unpark_one(woken[i]);
}

static ABI int32_t sem_create(uint32_t *out, const char *name, uint32_t attr,
                              int32_t initial, int32_t maximum, const void *options) {
    if (!out || !name || attr > 2 || initial < 0 || maximum <= 0 || initial > maximum) return ERR(22);
    if (options) { fputs("STOP: semaphore options are not implemented\n", stderr); exit(21); }
    host_lock(&table_lock);
    uint32_t slot;
    if (free_count) slot = free_slots[--free_count];
    else if (used_slots < SLOTS) slot = used_slots++;
    else { host_unlock(&table_lock); return ERR(28); }
    Sema *s = slots[slot];
    if (!s) {
        s = calloc(1, sizeof(*s));
        if (!s) { free_slots[free_count++] = slot; host_unlock(&table_lock); return ERR(12); }
        host_mutex_init(&s->lock);
        __atomic_store_n(&slots[slot], s, __ATOMIC_RELEASE);
    }
    uint32_t generation = generations[slot] = (generations[slot] + 1) % GENERATIONS;
    if (!generation) generation = generations[slot] = 1;
    const uint32_t id = generation << SLOT_BITS | slot;
    ++created;
    host_lock(&s->lock);
    s->attr = attr; s->count = s->initial = initial; s->maximum = maximum;
    s->waiting = 0; s->first = s->last = NULL;
    snprintf(s->name, sizeof s->name, "%s", name);
    s->id = id;
    host_unlock(&s->lock);
    host_unlock(&table_lock);
    *out = id;
    if (runtime_log_level()) printf("Runtime: semaphore id=%u, attr=%u, initial=%d, max=%d\n", id, attr, initial, maximum);
    return 0;
}

static int32_t wait_count(uint32_t id, int32_t need, uint32_t *timeout, int block) {
    Sema *s = lookup(id);
    if (!s) return ERR(3);
    int32_t result = 0;
    if (need <= 0 || need > s->maximum) result = ERR(22);
    else if (s->count >= need) { s->count -= need; ++s->acquired; }
    else if (!block) result = ERR(16);
    else if (timeout && !*timeout) { result = ERR(60); ++s->timed_out; }
    else {
        /* Priority-ordered semaphores (attr 2) are served FIFO: there is no PS4 priority
           scheduler, host threads run at equal priority. */
        if (trace()) fprintf(stderr, "Runtime: blocking wait on semaphore %u (need %d, count %d)\n", id, need, s->count); /* deadlock diagnosis */
        Waiter w = {.need = need};
        if (s->last) s->last->next = &w; else s->first = &w;
        s->last = &w; ++s->waiting;
        host_unlock(&s->lock);
        const uint64_t wait_start = runtime_wait_clock();
        const int slot = runtime_block_begin(RUNTIME_BLOCK_SEMA, id);
        const uint64_t deadline = timeout ? now_ns() + (uint64_t)*timeout * 1000 : 0;
        while (!__atomic_load_n(&w.served, __ATOMIC_ACQUIRE)) {
            uint64_t left = 0;
            if (timeout) {
                const uint64_t now = now_ns();
                left = now < deadline ? deadline - now : 0;
            }
            if (!timeout || left) {
                host_park(&w.served, 0, left);
                continue;
            }
            /* The deadline passed: leave the queue unless a signal served this waiter first. */
            host_lock(&s->lock);
            if (!__atomic_load_n(&w.served, __ATOMIC_ACQUIRE)) {
                Waiter **link = &s->first, *previous = NULL;
                while (*link != &w) { previous = *link; link = &(*link)->next; }
                *link = w.next;
                if (s->last == &w) s->last = previous;
                --s->waiting; ++s->timed_out;
                w.result = ERR(60);
                __atomic_store_n(&w.served, 1, __ATOMIC_RELAXED);
            }
            host_unlock(&s->lock);
        }
        runtime_block_end(slot);
        runtime_wait_note(2, runtime_wait_clock() - wait_start);
        result = w.result;
        if (timeout) {
            const uint64_t now = now_ns();
            *timeout = result ? 0 : (uint32_t)(now >= deadline ? 0 : (deadline - now) / 1000);
        }
        return result;
    }
    host_unlock(&s->lock);
    return result;
}
static ABI int32_t sem_wait(uint32_t id, int32_t need, uint32_t *timeout) { return wait_count(id, need, timeout, 1); }
static ABI int32_t sem_poll(uint32_t id, int32_t need) { return wait_count(id, need, NULL, 0); }

static ABI int32_t sem_signal(uint32_t id, int32_t count) {
    Sema *s = lookup(id);
    if (!s) return ERR(3);
    if (count <= 0 || count > s->maximum - s->count) { host_unlock(&s->lock); return ERR(22); }
    s->count += count; ++s->signaled;
    volatile uint32_t *woken[WAKE_BATCH];
    const unsigned n = s->first ? serve(s, 0, 0, woken, WAKE_BATCH) : 0;
    host_unlock(&s->lock);
    wake(woken, n);
    return 0;
}
static ABI int32_t sem_cancel(uint32_t id, int32_t count, int32_t *waiters) {
    Sema *s = lookup(id);
    if (!s) return ERR(3);
    if (count > s->maximum) { host_unlock(&s->lock); return ERR(22); }
    const unsigned queued = s->waiting;
    volatile uint32_t *woken[WAKE_BATCH];
    const unsigned n = serve(s, 1, ERR(85), woken, WAKE_BATCH);
    s->count = count < 0 ? s->initial : count;
    host_unlock(&s->lock);
    wake(woken, n);
    if (waiters) *waiters = (int32_t)queued;
    return 0;
}
static ABI int32_t sem_delete(uint32_t id) {
    host_lock(&table_lock);
    Sema *s = lookup(id);
    if (!s) { host_unlock(&table_lock); return ERR(3); }
    s->id = 0; /* lookups with this ID fail from here on */
    volatile uint32_t *woken[WAKE_BATCH];
    const unsigned n = serve(s, 1, ERR(13), woken, WAKE_BATCH);
    host_unlock(&s->lock);
    free_slots[free_count++] = id & (SLOTS - 1);
    ++deleted;
    host_unlock(&table_lock);
    wake(woken, n);
    return 0;
}

/* The freeze report: the semaphore's name, count and waiters in order. It may run while a thread
 * stuck in the game holds the semaphore's lock, so it does not wait for it. */
void runtime_sema_describe(uint32_t id, char *out, size_t size) {
    if (runtime_sync_legacy()) { legacy_sema_describe(id, out, size); return; }
    Sema *s = __atomic_load_n(&slots[id & (SLOTS - 1)], __ATOMIC_ACQUIRE);
    if (!s || !host_trylock(&s->lock)) {
        snprintf(out, size, s ? "semaphore %u (busy)" : "semaphore %u (deleted)", id);
        return;
    }
    if (s->id != id) snprintf(out, size, "semaphore %u (deleted)", id);
    else {
        int n = snprintf(out, size, "semaphore %u '%s', count %d of %d, waiters needing", id, s->name, s->count, s->maximum);
        for (Waiter *w = s->first; w && n > 0 && (size_t)n < size; w = w->next) n += snprintf(out + n, size - (size_t)n, " %d", w->need);
    }
    host_unlock(&s->lock);
}
/* Read-only test/diagnostic snapshot, not exposed to guest code. */
unsigned runtime_sema_waiters(uint32_t id) {
    if (runtime_sync_legacy()) return legacy_sema_waiters(id);
    Sema *s = lookup(id);
    if (!s) return 0;
    const unsigned n = s->waiting;
    host_unlock(&s->lock);
    return n;
}
uintptr_t runtime_sema_resolve(const char *name) {
    if (runtime_sync_legacy()) return legacy_sema_resolve(name);
    if (!strcmp(name, "188x57JYp0g#p#J")) return (uintptr_t)sem_create;
    if (!strcmp(name, "Zxa0VhQVTsk#p#J")) return (uintptr_t)sem_wait;
    if (!strcmp(name, "4czppHBiriw#p#J")) return (uintptr_t)sem_signal;
    if (!strcmp(name, "12wOHk8ywb0#p#J")) return (uintptr_t)sem_poll;
    if (!strcmp(name, "4DM06U2BNEY#p#J")) return (uintptr_t)sem_cancel;
    if (!strcmp(name, "R1Jvn8bSCW8#p#J")) return (uintptr_t)sem_delete;
    return 0;
}
/* Sums over the records, each under its lock (the report and the 60 s census only). */
static void totals(uint64_t *live, uint64_t *operations, uint64_t *acquired, uint64_t *signaled, uint64_t *timed_out) {
    *live = *operations = *acquired = *signaled = *timed_out = 0;
    const uint32_t used = __atomic_load_n(&used_slots, __ATOMIC_RELAXED);
    for (uint32_t i = 0; i < used && i < SLOTS; ++i) {
        Sema *s = __atomic_load_n(&slots[i], __ATOMIC_ACQUIRE);
        if (!s) continue;
        host_lock(&s->lock);
        *live += s->id != 0;
        *operations += s->operations; *acquired += s->acquired;
        *signaled += s->signaled; *timed_out += s->timed_out;
        host_unlock(&s->lock);
    }
}
/* bbport: semaphores alive now, and lookups and the steps they took (one slot each). */
void runtime_sema_census(uint64_t *live, uint64_t *found, uint64_t *steps) {
    if (runtime_sync_legacy()) { legacy_sema_census(live, found, steps); return; }
    uint64_t acquired, signaled, timed_out;
    totals(live, found, &acquired, &signaled, &timed_out);
    *steps = *found;
}
void runtime_sema_report(void) {
    if (runtime_sync_legacy()) { legacy_sema_report(); return; }
    uint64_t live, operations, acquired, signaled, timed_out;
    totals(&live, &operations, &acquired, &signaled, &timed_out);
    host_lock(&table_lock);
    printf("Runtime: semaphores created=%llu, deleted=%llu, acquired=%llu, signals=%llu, timeouts=%llu\n",
           (unsigned long long)created, (unsigned long long)deleted, (unsigned long long)acquired,
           (unsigned long long)signaled, (unsigned long long)timed_out);
    host_unlock(&table_lock);
}
