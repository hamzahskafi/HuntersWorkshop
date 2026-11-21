// SPDX-License-Identifier: GPL-3.0-or-later
// The Hunter's Dream backend, 0.4.6 beta 14 part 8: addresses as the log prints them. Players post
// their logs in public, so every line in thd/ that names an internet address prints its first two
// parts only ("203.0.x.x:3658"): still enough to tell a LAN address (192.168.x.x, 10.x) from a public
// one, and the server's relay from a peer. Only the log: what the game asks for (inet_ntop) and what
// goes to the server stay whole. Header-only and free of engine headers (tests/test_thd_addr_text.cpp).
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace BbThd {

// "a.b.x.x" for an IPv4 address in network order.
inline std::string MaskedIpv4(std::uint32_t addr_nbo) {
    unsigned char b[4];
    std::memcpy(b, &addr_nbo, 4); // network order: the first part first, on any host
    char text[24];
    std::snprintf(text, sizeof(text), "%u.%u.x.x", b[0], b[1]);
    return text;
}

// The same for text: "a.b.c.d" or "a.b.c.d:port" becomes "a.b.x.x" or "a.b.x.x:port"; anything that
// is not a dotted IPv4 address (a host name, an empty string) comes back as it is.
inline std::string MaskedEndpoint(const std::string& text) {
    unsigned a = 0, b = 0, c = 0, d = 0;
    int used = 0;
    if (std::sscanf(text.c_str(), "%3u.%3u.%3u.%3u%n", &a, &b, &c, &d, &used) != 4 || a > 255 || b > 255 ||
        c > 255 || d > 255) {
        return text;
    }
    const std::string rest = text.substr(static_cast<std::size_t>(used));
    if (!rest.empty() && rest[0] != ':') return text; // "1.2.3.4x" or the like: not an address
    return std::to_string(a) + "." + std::to_string(b) + ".x.x" + rest;
}

} // namespace BbThd
