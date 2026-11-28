// SPDX-License-Identifier: GPL-3.0-or-later
// The Hunter's Dream backend, 0.4.6 beta 14: how one of the game's P2P datagrams leaves the P2P
// port, as droogie/bbhost v0.2.35 sends it (e74e3ce, src/hle/net.cpp chunk_split and dgram_send).
// Header-only and free of engine headers, so tests/test_thd_p2p_split.cpp holds it to bbhost's
// net_p2p_test cases and sends the pieces through a loopback socket.
//
// The game's lower network layer batches its messages into one datagram as chunks, [kind 0-3]
// [length u16 BE][payload]: a busy frame or a warp comes to several KB. Larger than one 1500-byte
// frame, such a datagram crosses the internet as IP fragments, which some NATs and firewalls drop,
// and the game's transport then brings the chunks over a few at a time, seconds late. So one of
// more than kSplitPayload bytes leaves as pieces of whole chunks (a longer chunk alone), each behind
// its own P2P header, and the relay's when it goes through the relay; each fits one frame. The
// receiving game parses each datagram's chunks on their own, so it gets what the one datagram held.
// Bytes that are not whole chunks leave whole, up to what UDP carries (kMaxDatagram).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace BbThd::P2pSplit {

constexpr std::size_t kMaxDatagram = 65507;    // the largest UDP payload over IPv4
constexpr std::size_t kOneFramePayload = 1472; // 1500 less the IPv4 and UDP headers
constexpr std::size_t kSplitPayload = 1200;    // a piece and every header in one frame, with room for a tunnel's
constexpr std::size_t kRelayHeader = 12;       // [0xfb]['R'][token 8][port u16 BE]
constexpr std::size_t kMaxP2pHeader = 6;

// The PS4 kernel's header on a P2P datagram: [0xff][flags][src vport][dst vport], one-byte vports
// with flag 0x40 when both fit (Bloodborne's are 40 and 30), two-byte ones otherwise. Its length.
inline std::size_t P2pHeader(std::uint16_t src_vport, std::uint16_t dst_vport, std::uint8_t out[kMaxP2pHeader]) {
    constexpr std::uint8_t kMagic = 0xff, kFlagP2p = 0x80, kFlagByteVports = 0x40, kType = 0x03;
    out[0] = kMagic;
    if (src_vport < 256 && dst_vport < 256) {
        out[1] = kFlagP2p | kFlagByteVports | kType;
        out[2] = static_cast<std::uint8_t>(src_vport);
        out[3] = static_cast<std::uint8_t>(dst_vport);
        return 4;
    }
    out[1] = kFlagP2p | kType;
    out[2] = static_cast<std::uint8_t>(src_vport >> 8);
    out[3] = static_cast<std::uint8_t>(src_vport);
    out[4] = static_cast<std::uint8_t>(dst_vport >> 8);
    out[5] = static_cast<std::uint8_t>(dst_vport);
    return 6;
}

// Where the game's datagram d[0, n) splits into pieces of whole chunks of at most kSplitPayload
// bytes: each piece's end in `ends`, the last n. Empty when the bytes are not whole chunks; they
// then go as they are. (bbhost's chunk_split.)
inline void ChunkSplit(const std::uint8_t* d, std::size_t n, std::vector<std::size_t>& ends) {
    ends.clear();
    std::size_t at = 0, start = 0;
    while (at < n) {
        if (n - at < 3 || d[at] > 3) {
            ends.clear();
            return;
        }
        const std::size_t chunk = 3 + ((static_cast<std::size_t>(d[at + 1]) << 8) | d[at + 2]);
        if (chunk > n - at) {
            ends.clear();
            return;
        }
        if (at > start && at + chunk - start > kSplitPayload) {
            ends.push_back(at);
            start = at;
        }
        at += chunk;
    }
    ends.push_back(n);
}

// What became of one datagram.
struct Sent {
    std::size_t pieces = 0;  // the datagrams it was to leave as (1 unless split)
    std::size_t sent = 0;    // of them, those that left
    std::size_t largest = 0; // the largest on the wire, headers included
    std::size_t big = 0;     // of those that left, larger than one frame
    int error = 0;           // the error of the piece that failed (send's answer), 0 when none did
    bool too_large = false;  // nothing left: larger than UDP carries with its headers
};

// Sends the game's bytes[0, len) behind the P2P header ph[0, hdr) (hdr 0 on a plain UDP socket) and,
// when `relay` is set, its 12 bytes in front of that: in pieces when `split` and the bytes are whole
// chunks of more than kSplitPayload. `store` holds kRelayHeader + kMaxDatagram bytes: the game's
// bytes are copied in once and each piece's headers are written just in front of its bytes, over the
// end of the piece before, which has left by then. send(data, n) answers 0, or its error; a piece
// that fails ends the datagram (a later piece's loss is a loss on the way, as a fragment's would be:
// the game resends what it held).
template <typename SendFn>
Sent SendDatagram(const std::uint8_t* bytes, std::size_t len, const std::uint8_t* ph, std::size_t hdr,
                  const std::uint8_t* relay, bool split, std::vector<std::uint8_t>& store,
                  std::vector<std::size_t>& ends, SendFn&& send) {
    Sent r;
    const std::size_t lead = relay ? kRelayHeader : 0;
    const bool fits = len <= kMaxDatagram - hdr; // the game's bytes and the P2P header in what UDP carries
    ends.clear();
    if (split && bytes && len > kSplitPayload && fits) ChunkSplit(bytes, len, ends);
    if (ends.empty()) ends.push_back(len);
    r.pieces = ends.size();
    std::size_t widest = 0;
    for (std::size_t i = 0, begin = 0; i < ends.size(); begin = ends[i++])
        if (ends[i] - begin > widest) widest = ends[i] - begin;
    r.largest = lead + hdr + widest;
    if (!fits || r.largest > kMaxDatagram) {
        r.too_large = true;
        return r;
    }
    if (store.size() < kRelayHeader + kMaxDatagram) store.resize(kRelayHeader + kMaxDatagram);
    if (bytes && len) std::memcpy(store.data() + kRelayHeader + hdr, bytes, len);
    for (std::size_t i = 0, begin = 0; i < ends.size(); begin = ends[i++]) {
        std::uint8_t* const out = store.data() + (kRelayHeader - lead) + begin; // its bytes at out + lead + hdr
        const std::size_t total = lead + hdr + (ends[i] - begin);
        if (relay) std::memcpy(out, relay, kRelayHeader);
        if (hdr) std::memcpy(out + lead, ph, hdr);
        if (const int e = send(static_cast<const std::uint8_t*>(out), total)) {
            r.error = e;
            break;
        }
        ++r.sent;
        if (total > kOneFramePayload) ++r.big;
    }
    return r;
}

} // namespace BbThd::P2pSplit
