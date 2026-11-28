// SPDX-License-Identifier: GPL-3.0-or-later
// 0.4.6 beta 14: how the P2P port sends one of the game's datagrams (gpu/bbnet/thd/thd_p2p_split.h),
// with droogie/bbhost v0.2.35's cases (tests/net_p2p_test.cpp, e74e3ce). The game's datagrams are
// chunks of [kind 0-3][length u16 BE][payload]; one of more than 1,200 bytes leaves as datagrams of
// whole chunks of at most 1,200 bytes that together are the datagram (a longer chunk alone), each
// behind its own P2P header and the relay's when relayed. Bytes that are not whole chunks go whole,
// up to what UDP carries; above that nothing leaves. On Linux the pieces also cross a loopback UDP
// socket and are read back.
#include "gpu/bbnet/thd/thd_p2p_split.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace BbThd::P2pSplit;

namespace {

int g_fail = 0;
#define CHECK(x)                                                                                                    \
    do {                                                                                                            \
        if (!(x)) {                                                                                                 \
            ++g_fail;                                                                                               \
            std::printf("FAIL line %d: %s\n", __LINE__, #x);                                                        \
        }                                                                                                           \
    } while (0)

using Bytes = std::vector<std::uint8_t>;

void put16(Bytes& v, std::uint16_t x) {
    v.push_back(static_cast<std::uint8_t>(x >> 8));
    v.push_back(static_cast<std::uint8_t>(x));
}

void add_chunk(Bytes& d, std::uint8_t kind, std::size_t body, std::uint8_t fill) {
    d.push_back(kind);
    put16(d, static_cast<std::uint16_t>(body));
    for (std::size_t i = 0; i < body; ++i) d.push_back(static_cast<std::uint8_t>(fill + i));
}

// True when [p, p+n) is whole chunks of kinds 0-3.
bool whole_chunks(const std::uint8_t* p, std::size_t n, int* count) {
    std::size_t at = 0;
    *count = 0;
    while (at < n) {
        if (n - at < 3 || p[at] > 3) return false;
        const std::size_t c = 3 + ((static_cast<std::size_t>(p[at + 1]) << 8) | p[at + 2]);
        if (c > n - at) return false;
        at += c;
        ++*count;
    }
    return true;
}

struct Game {
    std::string name;
    Bytes d;
    std::vector<std::size_t> pieces; // the pieces' sizes
};

// bbhost's cases.
std::vector<Game> games() {
    std::vector<Game> out;
    // Pairs of a 254-byte chunk and a 22-byte one (7 pairs fit in 2 KB, 8 do not, 15 come to about
    // 4 KB): four pairs (1,104 bytes) to a piece.
    for (const int pairs : {7, 8, 15}) {
        Game g;
        g.name = std::to_string(pairs) + " pairs";
        for (int i = 0; i < pairs; ++i) {
            add_chunk(g.d, 1, 251, static_cast<std::uint8_t>(i));
            add_chunk(g.d, 1, 19, static_cast<std::uint8_t>(0x80 + i));
        }
        for (int left = pairs; left > 0; left -= 4)
            g.pieces.push_back(static_cast<std::size_t>(left < 4 ? left : 4) * 276);
        out.push_back(g);
    }
    {
        // A full segment (1,238 bytes and its header) is longer than a piece: it goes alone,
        // between short chunks of every kind.
        Game g;
        g.name = "long chunks alone";
        add_chunk(g.d, 0, 85, 1);
        add_chunk(g.d, 1, 1238, 2);
        add_chunk(g.d, 2, 40, 3);
        add_chunk(g.d, 3, 1238, 4);
        add_chunk(g.d, 1, 6, 5);
        g.pieces = {88, 1241, 43, 1241, 9};
        out.push_back(g);
    }
    {
        // 1,200 bytes of chunks go whole; 1,201 bytes as two pieces.
        Game g;
        g.name = "1200 bytes";
        add_chunk(g.d, 1, 597, 6);
        add_chunk(g.d, 0, 597, 7);
        g.pieces = {1200};
        out.push_back(g);
        Game h;
        h.name = "1201 bytes";
        add_chunk(h.d, 1, 597, 6);
        add_chunk(h.d, 0, 598, 7);
        h.pieces = {600, 601};
        out.push_back(h);
    }
    {
        // A piece filled to exactly 1,200 bytes, then the rest.
        Game g;
        g.name = "a full piece";
        add_chunk(g.d, 1, 397, 8);
        add_chunk(g.d, 1, 397, 9);
        add_chunk(g.d, 1, 397, 10);
        add_chunk(g.d, 2, 10, 11);
        g.pieces = {1200, 13};
        out.push_back(g);
    }
    {
        // Not whole chunks, though they start as such: they go whole.
        Game g;
        g.name = "a cut chunk";
        for (int i = 0; i < 6; ++i) add_chunk(g.d, 1, 297, static_cast<std::uint8_t>(i));
        g.d.resize(g.d.size() - 1);
        g.pieces = {g.d.size()};
        out.push_back(g);
        Game h;
        h.name = "a kind past 3";
        for (int i = 0; i < 6; ++i) add_chunk(h.d, i == 3 ? 4 : 1, 297, static_cast<std::uint8_t>(i));
        h.pieces = {h.d.size()};
        out.push_back(h);
    }
    return out;
}

std::vector<std::size_t> sizes_of(const std::vector<std::size_t>& ends) {
    std::vector<std::size_t> sizes;
    for (std::size_t i = 0, begin = 0; i < ends.size(); begin = ends[i++]) sizes.push_back(ends[i] - begin);
    return sizes;
}

void headers() {
    std::uint8_t h[kMaxP2pHeader] = {};
    CHECK(P2pHeader(40, 30, h) == 4);
    const std::uint8_t one[4] = {0xff, 0xc3, 40, 30};
    CHECK(std::memcmp(h, one, 4) == 0);
    CHECK(P2pHeader(300, 301, h) == 6);
    const std::uint8_t two[6] = {0xff, 0x83, 0x01, 0x2c, 0x01, 0x2d};
    CHECK(std::memcmp(h, two, 6) == 0);
    CHECK(P2pHeader(40, 256, h) == 6); // one vport past 255 takes the long form
}

void splits() {
    std::vector<std::size_t> ends;
    for (const Game& g : games()) {
        if (g.d.size() > kSplitPayload) {
            ChunkSplit(g.d.data(), g.d.size(), ends);
            const bool chunks = g.pieces.size() > 1;
            if (chunks) {
                CHECK(sizes_of(ends) == g.pieces);
            } else {
                int count = 0;
                CHECK(ends.empty() == !whole_chunks(g.d.data(), g.d.size(), &count));
            }
            if (chunks && sizes_of(ends) != g.pieces) std::printf("  %s\n", g.name.c_str());
        }
    }
    // Nothing at all: one end.
    ChunkSplit(nullptr, 0, ends);
    CHECK(ends.size() == 1 && ends[0] == 0);
}

// The datagrams a send produced.
struct Capture {
    std::vector<Bytes> out;
    int fail_at = -1; // the piece whose send fails
    int error = 5;
    int operator()(const std::uint8_t* d, std::size_t n) {
        if (static_cast<int>(out.size()) == fail_at) return error;
        out.emplace_back(d, d + n);
        return 0;
    }
};

const std::uint8_t kRelay[kRelayHeader] = {0xfb, 'R', 1, 2, 3, 4, 5, 6, 7, 8, 0x0e, 0x4a};

// Each datagram is [relay header][P2P header][a piece], the pieces in order are the game's bytes, and
// each piece ends where `sizes` says.
bool pieces_ok(const std::vector<Bytes>& out, const Bytes& d, const std::uint8_t* ph, std::size_t hdr,
               const std::uint8_t* relay, const std::vector<std::size_t>& sizes) {
    const std::size_t lead = relay ? kRelayHeader : 0;
    if (out.size() != sizes.size()) return false;
    Bytes joined;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const Bytes& p = out[i];
        if (p.size() != lead + hdr + sizes[i]) return false;
        if (relay && std::memcmp(p.data(), relay, kRelayHeader) != 0) return false;
        if (hdr && std::memcmp(p.data() + lead, ph, hdr) != 0) return false;
        int count = 0;
        if (out.size() > 1 && (!whole_chunks(p.data() + lead + hdr, sizes[i], &count) || (sizes[i] > kSplitPayload && count != 1)))
            return false;
        joined.insert(joined.end(), p.begin() + static_cast<std::ptrdiff_t>(lead + hdr), p.end());
    }
    return joined == d;
}

void sends() {
    std::vector<std::uint8_t> store;
    std::vector<std::size_t> ends;
    for (const bool relayed : {false, true}) {
        const std::uint8_t* relay = relayed ? kRelay : nullptr;
        for (const auto& vports : {std::pair<int, int>{40, 30}, std::pair<int, int>{300, 301}}) {
            std::uint8_t ph[kMaxP2pHeader];
            const std::size_t hdr = P2pHeader(static_cast<std::uint16_t>(vports.first),
                                              static_cast<std::uint16_t>(vports.second), ph);
            for (const Game& g : games()) {
                Capture c;
                const Sent r = SendDatagram(g.d.data(), g.d.size(), ph, hdr, relay, true, store, ends, c);
                CHECK(!r.too_large && r.error == 0);
                CHECK(r.pieces == g.pieces.size() && r.sent == r.pieces);
                const bool ok = pieces_ok(c.out, g.d, ph, hdr, relay, g.pieces);
                CHECK(ok);
                if (!ok) std::printf("  %s (relay %d, header %zu)\n", g.name.c_str(), relayed, hdr);
                std::size_t widest = 0;
                for (std::size_t s : g.pieces) widest = s > widest ? s : widest;
                CHECK(r.largest == (relayed ? kRelayHeader : 0) + hdr + widest);
                // Without the split (a plain UDP socket's way): whole.
                Capture w;
                const Sent rw = SendDatagram(g.d.data(), g.d.size(), ph, hdr, relay, false, store, ends, w);
                CHECK(rw.pieces == 1 && rw.sent == 1 && pieces_ok(w.out, g.d, ph, hdr, relay, {g.d.size()}));
            }
        }
    }

    // Bytes that are not chunks go as they are, up to what UDP carries with the headers.
    std::uint8_t ph[kMaxP2pHeader];
    const std::size_t hdr = P2pHeader(40, 30, ph);
    for (const bool relayed : {false, true}) {
        const std::uint8_t* relay = relayed ? kRelay : nullptr;
        const std::size_t most = kMaxDatagram - hdr - (relayed ? kRelayHeader : 0);
        for (const std::size_t n : {std::size_t{1}, std::size_t{64}, std::size_t{7 * 276}, std::size_t{15 * 276}, most}) {
            Bytes d(n);
            for (std::size_t i = 0; i < n; ++i) d[i] = static_cast<std::uint8_t>(i * 131 + n);
            d[0] = 0xff; // not a chunk
            Capture c;
            const Sent r = SendDatagram(d.data(), n, ph, hdr, relay, true, store, ends, c);
            CHECK(!r.too_large && r.sent == 1 && pieces_ok(c.out, d, ph, hdr, relay, {n}));
            if (n > kOneFramePayload) CHECK(r.big == 1);
        }
        // One byte more: nothing leaves.
        Bytes over(most + 1, 0x5a);
        Capture c;
        const Sent r = SendDatagram(over.data(), over.size(), ph, hdr, relay, true, store, ends, c);
        CHECK(r.too_large && r.sent == 0 && c.out.empty());
    }
    // Chunks that together pass UDP's limit are still too large: the game's datagram as a whole is
    // what UDP must carry first (bbhost's `fits`).
    {
        Bytes d;
        while (d.size() + 1003 <= kMaxDatagram) add_chunk(d, 1, 1000, 1);
        add_chunk(d, 1, 1000, 1);
        Capture c;
        const Sent r = SendDatagram(d.data(), d.size(), ph, hdr, nullptr, true, store, ends, c);
        CHECK(r.too_large && c.out.empty());
    }

    // A failed piece ends the datagram: the error, and the pieces before it counted.
    {
        const Game g = games()[2]; // 15 pairs: four pieces
        Capture c;
        c.fail_at = 2;
        Sent r = SendDatagram(g.d.data(), g.d.size(), ph, hdr, nullptr, true, store, ends, c);
        CHECK(r.pieces == 4 && r.sent == 2 && r.error == 5 && c.out.size() == 2);
        Capture first;
        first.fail_at = 0;
        r = SendDatagram(g.d.data(), g.d.size(), ph, hdr, nullptr, true, store, ends, first);
        CHECK(r.sent == 0 && r.error == 5 && first.out.empty());
    }
    // An empty datagram: the header alone.
    {
        Capture c;
        const Sent r = SendDatagram(nullptr, 0, ph, hdr, nullptr, true, store, ends, c);
        CHECK(r.sent == 1 && c.out.size() == 1 && c.out[0].size() == hdr);
    }
}

#if !defined(_WIN32)
// The pieces through the kernel: a loopback UDP socket sends what SendDatagram builds, another reads
// each back, whole, in order.
void loopback() {
    const int rx = ::socket(AF_INET, SOCK_DGRAM, 0), tx = ::socket(AF_INET, SOCK_DGRAM, 0);
    CHECK(rx >= 0 && tx >= 0);
    if (rx < 0 || tx < 0) return;
    int big = 1 << 20;
    ::setsockopt(rx, SOL_SOCKET, SO_RCVBUF, &big, sizeof big);
    sockaddr_in at{};
    at.sin_family = AF_INET;
    at.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(::bind(rx, reinterpret_cast<sockaddr*>(&at), sizeof at) == 0);
    socklen_t sl = sizeof at;
    CHECK(::getsockname(rx, reinterpret_cast<sockaddr*>(&at), &sl) == 0);
    auto send = [&](const std::uint8_t* d, std::size_t n) {
        return ::sendto(tx, d, n, 0, reinterpret_cast<const sockaddr*>(&at), sizeof at) == static_cast<ssize_t>(n) ? 0 : errno;
    };
    auto recv = [&](Bytes& out) {
        pollfd p{rx, POLLIN, 0};
        if (::poll(&p, 1, 1000) <= 0) return false;
        out.resize(65536);
        const ssize_t n = ::recv(rx, out.data(), out.size(), 0);
        if (n < 0) return false;
        out.resize(static_cast<std::size_t>(n));
        return true;
    };
    std::vector<std::uint8_t> store;
    std::vector<std::size_t> ends;
    std::uint8_t ph[kMaxP2pHeader];
    const std::size_t hdr = P2pHeader(40, 30, ph);
    for (const bool relayed : {false, true}) {
        const std::uint8_t* relay = relayed ? kRelay : nullptr;
        std::vector<Game> list = games();
        Game most;
        most.name = "the largest";
        most.d.assign(kMaxDatagram - hdr - (relayed ? kRelayHeader : 0), 0x3c);
        most.pieces = {most.d.size()};
        list.push_back(most);
        for (const Game& g : list) {
            const Sent r = SendDatagram(g.d.data(), g.d.size(), ph, hdr, relay, true, store, ends, send);
            CHECK(r.sent == g.pieces.size() && r.error == 0);
            std::vector<Bytes> got;
            for (std::size_t i = 0; i < r.sent; ++i) {
                Bytes b;
                if (!recv(b)) break;
                got.push_back(std::move(b));
            }
            const bool ok = pieces_ok(got, g.d, ph, hdr, relay, g.pieces);
            CHECK(ok);
            if (!ok) std::printf("  loopback %s (relay %d): %zu of %zu back\n", g.name.c_str(), relayed, got.size(), r.sent);
            pollfd p{rx, POLLIN, 0};
            CHECK(::poll(&p, 1, 0) == 0); // nothing more
        }
    }
    ::close(rx);
    ::close(tx);
}
#endif

} // namespace

int main() {
    headers();
    splits();
    sends();
#if !defined(_WIN32)
    loopback();
#endif
    if (g_fail) {
        std::printf("thd-p2p-split-test: %d failed\n", g_fail);
        return 1;
    }
    std::printf("thd-p2p-split-test: ok\n");
    return 0;
}
