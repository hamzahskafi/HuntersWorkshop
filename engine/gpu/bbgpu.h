/* C interface of the GPU library (gpu/): shadPS4's Liverpool/Vulkan video core,
 * GnmDriver, VideoOut and kernel event queues, adapted to the native loader. */
#ifndef BBGPU_H
#define BBGPU_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    const char *title;          /* window title */
    const char *serial;         /* CUSA id, names the pipeline cache */
    const char *user_dir;       /* pipeline cache/logs directory */
    uint32_t sdk_version;       /* from the eboot's procparam */
    uint32_t psf_attributes;    /* param.sfo ATTRIBUTE */
    int32_t width, height;      /* initial window size */
} BbGpuConfig;
/* Registers kernel event queues (needed with or without graphics). */
void bbgpu_register_kernel(void);
/* Start networking after game metadata, guest TLS and linked modules are ready. */
void bbgpu_start_online(void);
/* Creates window, Vulkan device, presenter and GPU command processor. */
int bbgpu_init(const BbGpuConfig *config);
/* Function for an imported NID ("NID#lib#mod"), or 0 when the GPU library does not provide it. */
uintptr_t bbgpu_resolve(const char *scoped_nid);
/* Called first by the loader's SIGSEGV handler: 1 when a GPU page-tracking fault was handled.
 * The context is the handler's ucontext_t (Linux) or the vectored exception handler's
 * EXCEPTION_POINTERS (Windows). */
int bbgpu_handle_fault(void *ucontext, void *address);
/* BB_WRITE_LOG=1: prints the logged GPU-side writes to guest memory near the fault
 * (same context as bbgpu_handle_fault). */
void bbgpu_dump_guest_writes(void *ucontext);
/* Keyboard text entry through the game window (IME dialog). begin returns 0 when
 * no window exists; poll returns 0 typing, 1 confirmed, 2 cancelled (UTF-8 text). */
int bbgpu_text_input_begin(const char *initial_utf8, const char *prompt_utf8);
int bbgpu_text_input_poll(char *out_utf8, uint64_t size);
/* 1 while the in-game settings menu is open: the game's pad input is held neutral. */
int bbgpu_overlay_captures_input(void);
/* Patches the loaded image before the game runs (image still writable): libGnm entry hooks. */
void bbgpu_patch_image(unsigned char *image, uint64_t size);
/* BB_TIMELINE: a guest thread's wait in the runtime ended (kind 0 condition variable, 1 mutex,
 * 2 semaphore, 3 sleep; ns its length): runtime_wait_note passes it on. */
void bbgpu_timeline_wait(int kind, uint64_t ns);
/* 0.4.6 beta 18 (gpu/shim/bbport_roles.cpp): the calling thread was named. ps4_priority >= 0: a
 * game thread (FMOD's, 256-300, Audio; every other Frame); < 0: one of the runtime's own, by the
 * engine's table. 1 when its role set its host priority, 0 with BB_THREAD_ROLES=0. */
int bbgpu_thread_named(const char *name, int ps4_priority);
/* 0.4.6 beta 20 (gpu/shim/bbport_role_lock.h): a runtime lock that tells the roles who holds it.
 * After taking it exclusively: holder = bbgpu_role_locked(); before letting go: holder = NULL, then
 * bbgpu_role_unlocked() after. A thread that finds it held: bbgpu_role_contended(name, holder)
 * before it waits, bbgpu_role_acquired() once it has it (a lower-role holder is raised meanwhile and
 * the wait counted in the Roles: line). */
void *bbgpu_role_locked(void);
void bbgpu_role_unlocked(void);
void bbgpu_role_contended(const char *lock, void *holder);
void bbgpu_role_acquired(void);
/* BB_TEST_CPUS=<n>: the process keeps n logical processors, fastest first (a measurement). */
void bbgpu_test_cpus(void);
/* Number of symbols registered by the vendored libraries (diagnostics). */
unsigned bbgpu_symbol_count(void);
#ifdef __cplusplus
}
#endif
#endif
