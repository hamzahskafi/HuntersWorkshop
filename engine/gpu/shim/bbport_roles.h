// SPDX-License-Identifier: GPL-2.0-or-later
// 0.4.6 beta 18 (docs/handoffs-and-placement.md): every thread's role, declared once, and one place
// that turns a role into the system's own flags: priority, quality of service (Windows' power
// throttling: high or eco) and preferred cores (CPU sets, by one rule over the processor layout).
// Engine threads get their role from their name (one table, applied when the thread names itself);
// the game's threads from the runtime when it names them: FMOD's (PS4 priorities 256-300) are Audio,
// every other is Frame, today's treatment. BB_THREAD_ROLES=0: the priorities before beta 18 (this
// beta only).
#pragma once

#include <cstdint>

namespace BbRoles {

enum class Role : std::uint8_t {
    Unset,
    Pacing,     ///< deadline threads that set their own priority (the flip thread): high QoS
    Chain,      ///< the engine's frame chain: above normal, high QoS, the fastest cores if they hold it
    Frame,      ///< the game's frame work, the compile and copy threads: normal, high QoS, any core
    Assist,     ///< work ahead the chain uses only if it runs now (draw preparation): idle, high QoS
    Background, ///< work nobody waits for this frame: idle, eco QoS, the efficient cores
    Audio,      ///< FMOD's threads: normal (highest with "Sound threads first"), high QoS
    Service,    ///< everything with its own pace: left as the system has it
};

/// BB_THREAD_ROLES (on unless "0"; beta 18 only).
bool On();

/// The calling engine thread named itself: its role from the table. A name not in it is left as the
/// system has it and logged once.
void NamedEngine(const char* name);
/// The calling game thread was named; `ps4_priority` its PS4 priority. True when the role sets its
/// priority (the runtime's audio rule then stays out).
bool NamedGame(const char* name, int ps4_priority);

/// While a loading screen is up, and from the start up to the first frame, Background work is what
/// the player waits for: it runs at normal priority, high QoS, on any core, until play resumes.
void SetLoading(bool loading);
void FirstFrame();

/// BbThreads::MakeBackground under roles: a Background or Assist thread back at its role (for its
/// next job, unraised). False for any other thread (the caller lowers it as before).
bool LowerSelf();
/// BbThreads::SetPriority under roles: thread `handle` raised to `priority` while a frame waits on
/// its job, with high QoS and any core, until its next LowerSelf. False when the thread has no role
/// (the caller sets the priority).
bool Raise(void* handle, int priority);

/// A game thread woke from a wait in the runtime (statistics on): on which kind of core.
void NoteWake();

/// The `Roles:` line, with the frame statistics: per role, threads, CPU time in cores and the share
/// of wakes on each kind of core.
void Report(double seconds);

/// Pool sizes from the processor layout (beta 18: today's values, gathered here).
enum class Pool { Recording, Copy, Compile, DrawPrep };
unsigned PoolSize(Pool pool);

} // namespace BbRoles
