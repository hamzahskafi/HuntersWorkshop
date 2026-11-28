// SPDX-License-Identifier: GPL-3.0-or-later
// 0.4.6 beta 14 part 8: addresses as the log prints them (gpu/bbnet/thd/thd_addr_text.h): the first two
// parts of an IPv4 address, with its port; anything that is not one left as it is.
#include "gpu/bbnet/thd/thd_addr_text.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

using namespace BbThd;

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        ++g_fail;
        std::printf("FAIL: %s\n", what);
    }
}

std::uint32_t nbo(unsigned a, unsigned b, unsigned c, unsigned d) {
    const unsigned char bytes[4] = {static_cast<unsigned char>(a), static_cast<unsigned char>(b),
                                    static_cast<unsigned char>(c), static_cast<unsigned char>(d)};
    std::uint32_t v;
    std::memcpy(&v, bytes, 4);
    return v;
}

} // namespace

int main() {
    check(MaskedIpv4(nbo(203, 0, 113, 7)) == "203.0.x.x", "a public address: its first two parts");
    check(MaskedIpv4(nbo(192, 168, 1, 20)) == "192.168.x.x", "a LAN address still reads as one");
    check(MaskedIpv4(nbo(10, 4, 5, 6)) == "10.4.x.x", "10.x too");
    check(MaskedIpv4(0) == "0.0.x.x", "none");
    check(MaskedEndpoint("203.0.113.7:3658") == "203.0.x.x:3658", "with its port");
    check(MaskedEndpoint("203.0.113.7") == "203.0.x.x", "without one");
    check(MaskedEndpoint("192.168.1.20:5093") == "192.168.x.x:5093", "a LAN endpoint");
    check(MaskedEndpoint("thehuntersdream.com:3478") == "thehuntersdream.com:3478", "a host name stays");
    check(MaskedEndpoint(":0") == ":0", "no address yet stays");
    check(MaskedEndpoint("") == "", "empty stays");
    check(MaskedEndpoint("1.2.3.4x") == "1.2.3.4x", "not an address: as it is");
    check(MaskedEndpoint("300.1.2.3:1") == "300.1.2.3:1", "out of range: as it is");
    check(MaskedEndpoint("1.2.3:4") == "1.2.3:4", "three parts: as it is");
    if (g_fail) {
        std::printf("thd-addr-text-test: %d failed\n", g_fail);
        return 1;
    }
    std::printf("thd-addr-text-test: ok\n");
    return 0;
}
