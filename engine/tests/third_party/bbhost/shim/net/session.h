// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only stand-in for bbhost's net/session.h: the dispatcher, the player's name, the peer table
// and the server's address lookup, as np_signaling.cpp uses them (tests/test_thd_signaling.cpp
// defines them on its fake clock and in-memory network).
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace net {

enum class Prio { Context = 0, Request = 1, Signaling = 2, RoomEvent = 3 };
void dispatch_after(int delay_ms, Prio prio, std::function<void()> fn);
std::string online_id();

struct Peer {
    std::uint16_t member_id = 0;
    std::string online_id;
    std::uint32_t addr = 0; // network byte order
    std::uint16_t port = 0; // host byte order
    std::uint32_t local_addr = 0;
    std::uint16_t local_port = 0;
    unsigned conn_id = 0;
    int sig_status = 0;
};
bool peers_get(std::uint16_t member_id, Peer* out);
bool peers_find_online(const std::string& online_id, Peer* out);
void peers_set_conn(std::uint16_t member_id, unsigned conn_id, int sig_status);
Peer peers_provisional(const std::string& online_id, std::uint32_t addr, std::uint16_t port);
bool server_signaling_resolve(const std::string& online_id, std::uint32_t* addr, std::uint16_t* port,
                              std::string& error);

} // namespace net
