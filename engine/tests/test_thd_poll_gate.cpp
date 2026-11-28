// SPDX-License-Identifier: GPL-3.0-or-later
// 0.4.6 beta 14 part 7: the event poller's generations (gpu/bbnet/thd/thd_poll_gate.h, bbhost's
// poll_pacing::Gate), with real threads: one poll out at a time across a stop and a start; a poller
// whose generation ended gets no turn, so it neither handles nor acks what its held poll brought; and
// the keepalive's sleep ends at once on a stop, so a stop and a start leave one keepalive.
#include "gpu/bbnet/thd/thd_poll_gate.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

using BbThd::PollGate;
using namespace std::chrono_literals;

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        ++g_fail;
        std::printf("FAIL: %s\n", what);
    }
}

double ms_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void generations() {
    PollGate g;
    check(!g.Running() && !g.Live(0) && !g.Live(1), "nothing runs before a start");
    const auto one = g.Start();
    check(g.Running() && g.Live(one), "a start makes its generation current");
    g.Stop();
    check(!g.Running() && !g.Live(one), "a stop ends it");
    const auto two = g.Start();
    check(two != one && g.Live(two) && !g.Live(one), "a new start is a new generation; the old one stays over");
    check(!g.Begin(one), "an ended generation gets no turn");
    check(g.Begin(two), "the current one does");
    g.End();
}

// A held poll of generation 1 when the game stops and starts again: generation 2's first poll waits
// until it ends, and generation 1's poller, back from it, finds itself over and takes no other turn.
void one_poll_out() {
    PollGate g;
    const auto one = g.Start();
    check(g.Begin(one), "generation 1 polls");
    g.Stop();
    const auto two = g.Start();
    std::atomic<bool> two_polled{false};
    std::atomic<int> out{1}, most{1};
    std::thread second([&] {
        if (g.Begin(two)) {
            const int now = ++out;
            if (now > most) most = now;
            two_polled = true;
            --out;
            g.End();
        }
    });
    std::this_thread::sleep_for(50ms);
    check(!two_polled, "the new poller's first poll waits for the old one's held poll");
    // The old poller back from its poll: over, so it gives up what the poll brought.
    check(!g.Live(one), "the old poller sees its generation over (no handler, no ack)");
    --out;
    g.End();
    second.join();
    check(two_polled, "then the new one polls");
    check(most == 1, "never two polls out at once");
    check(!g.Begin(one), "and the old poller ends");
}

// A stop while a poller waits for its turn: it gets none.
void stop_while_waiting() {
    PollGate g;
    const auto one = g.Start();
    check(g.Begin(one), "a poll is out");
    std::atomic<int> got{-1};
    std::thread waiter([&] { got = g.Begin(one) ? 1 : 0; });
    std::this_thread::sleep_for(20ms);
    g.Stop();
    waiter.join();
    check(got == 0, "a stop ends the wait for a turn, without one");
    g.End();
}

// The keepalive sleeps 15 s under its generation: a stop wakes it at once to end, and a stop and a
// start within the 15 s leave the old one ended and the new one sleeping.
void keepalive() {
    PollGate g;
    const auto one = g.Start();
    check(g.Sleep(one, 10), "a sleep that runs out keeps going");
    std::atomic<int> old_result{-1};
    const auto t0 = std::chrono::steady_clock::now();
    std::thread old_keepalive([&] { old_result = g.Sleep(one, 15000) ? 1 : 0; });
    std::this_thread::sleep_for(20ms);
    g.Stop();
    const auto two = g.Start();
    old_keepalive.join();
    check(old_result == 0, "the old keepalive ends");
    check(ms_since(t0) < 5000, "at once, not after its 15 s");
    check(g.Sleep(two, 10), "the new one sleeps on");
    check(!g.Sleep(one, 10), "an ended generation's sleep returns at once");
}

} // namespace

int main() {
    generations();
    one_poll_out();
    stop_while_waiting();
    keepalive();
    if (g_fail) {
        std::printf("thd-poll-gate-test: %d failed\n", g_fail);
        return 1;
    }
    std::printf("thd-poll-gate-test: ok\n");
    return 0;
}
