// SPDX-License-Identifier: GPL-3.0-or-later
// The Hunter's Dream backend: sceNpSignaling with the connection handshake between games that bbhost
// 0.2.31 introduced (droogie/bbhost #217) and every client of The Hunter's Dream speaks since its
// update of 2026-11-10. Ported from droogie/bbhost src/hle/np_signaling.cpp at a317321 (v0.2.35,
// unchanged since 3f9c4c0) function for function: its states, message kinds, byte layout, timings
// and events; bbhost's names are kept in comments. See docs/online-update.md.
//
// A class with its outside world passed in (Env), so a test can run two of them, or one beside
// bbhost's own file, through an in-memory network (tests/test_thd_signaling.cpp). It includes no
// engine header.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>

namespace BbThd {

class Signaling {
public:
    // A room member or a player resolved through the server (the peer table's entry).
    struct Peer {
        std::uint16_t member_id = 0;
        std::string online_id;
        std::uint32_t addr = 0; // network order
        std::uint16_t port = 0; // host order
    };
    // Everything outside NpSignaling. In the game: thd_np.cpp's SignalingEnv.
    struct Env {
        // A control datagram to addr (network order):port (host order), from the P2P port; through
        // the relay when the peer is behind it (bbhost's hle_net_p2p_signal).
        std::function<void(std::uint32_t addr, std::uint16_t port, const std::uint8_t* data, std::size_t len)> send;
        // Runs fn on the dispatcher after delay_ms (bbhost's net::dispatch_after, Prio::Signaling).
        std::function<void(int delay_ms, std::function<void()> fn)> dispatch;
        // The game's NpSignaling handler: (ctxId, connId, event, errorCode, arg) (hle_call_guest).
        std::function<void(void* cb, unsigned ctx, unsigned conn, std::uint16_t event, int error, void* arg)> call_game;
        std::function<bool(std::uint16_t member_id, Peer* out)> peer_get;          // net::peers_get
        std::function<bool(const std::string& online_id, Peer* out)> peer_find;   // net::peers_find_online
        std::function<void(std::uint16_t member_id, unsigned conn, int status)> peer_set_conn;
        std::function<Peer(const std::string& online_id, std::uint32_t addr, std::uint16_t port)> peer_provisional;
        // The server's address for a player not in the room yet (net::server_signaling_resolve).
        std::function<bool(const std::string& online_id, std::uint32_t* addr, std::uint16_t* port,
                           std::string& error)>
            resolve;
        std::function<std::string()> online_id;
        // Matching2's side (hle_np_matching2_transport_up, _transport_dead, _peer_closed).
        std::function<void(const std::string& online_id)> m2_up;
        std::function<void(const std::string& online_id, int error)> m2_dead;
        std::function<void(const std::string& online_id)> m2_closed;
        std::function<void(const char* line)> log;
        // The first connection id's draw (std::random_device when unset).
        std::function<unsigned()> random;
        // 0.4.6 beta 14 part 9, the session's line (Stats): a clock in milliseconds (steady_clock
        // when unset), and whether a peer's address is the server's relay (never when unset).
        std::function<std::int64_t()> now_ms;
        std::function<bool(std::uint32_t addr, std::uint16_t port)> relayed;
        bool trace = false; // BB_THD_TRACE=1 (bbhost's BBHOST_NP_TRACE)
    };

    // The datagram: magic, kind, reason, the sender's and the receiver's online ids, a nonce.
    static constexpr std::uint8_t kMagic[4] = {0xfe, 'b', 'b', 's'};
    static constexpr std::size_t kIdLen = 16;
    static constexpr std::size_t kPacketLen = sizeof(kMagic) + 2 + kIdLen + kIdLen + 8; // 46
    static constexpr int kStepMs = 500;
    static constexpr int kGiveUpMs = 30000;
    static constexpr int kM2EstablishDelayMs = 1000;
    // Matching2's errors for its DEAD (0x5101).
    static constexpr int kM2ErrTimeout = static_cast<int>(0x80550E11);
    static constexpr int kM2ErrTerminatedByPeer = static_cast<int>(0x80550E10);
    static constexpr int kM2ErrTerminatedByMyself = static_cast<int>(0x80550E18);
    static bool IsSignal(const std::uint8_t* data, std::size_t len) {
        return len >= sizeof(kMagic) && data[0] == kMagic[0] && data[1] == kMagic[1] && data[2] == kMagic[2] &&
               data[3] == kMagic[3];
    }

    explicit Signaling(Env env);

    // 0.4.6 beta 14 part 9: what the links did since the last TakeStats, for the session's line. Only
    // counted: nothing here changes what is sent or fired.
    struct Stats {
        int up = 0;                // links up (each player's transport, once)
        int handshake_min_ms = 0;  // their handshakes, from the first greeting either way to up
        int handshake_max_ms = 0;
        int relayed = 0;           // of them, through the server's relay
        int timed_out_silent = 0;  // handshakes given up after 30 s: the peer never answered
        int timed_out_partial = 0; // the peer answered, but the handshake did not finish
        int no_address = 0;        // no address for the peer within 30 s (the server did not know it)
        int closed_by_peer = 0;    // closed by the other player (its Close that ends the link, or it left the room)
        int closed_by_us = 0;      // closed by this game (its deactivate)
        bool Any() const {
            return up || timed_out_silent || timed_out_partial || no_address || closed_by_peer || closed_by_us;
        }
    };
    Stats TakeStats();

    // The game's calls (sceNpSignaling*).
    int Initialize();
    int Terminate();
    int CreateContext(void* cb, void* arg, unsigned* ctx);
    int DeleteContext(unsigned ctx);
    int Activate(unsigned ctx, const std::string& peer, unsigned* conn_out);
    int Deactivate(unsigned ctx, unsigned conn);
    int Status(unsigned ctx, unsigned conn, int* status, std::uint32_t* addr, std::uint16_t* port);

    // A control datagram from the P2P port, from addr (network order):port (network order), on the
    // dispatcher (hle_np_signaling_control).
    void Control(const std::uint8_t* data, std::size_t len, std::uint32_t addr, std::uint16_t port);

    // Matching2's (hle_np_signaling_live, _m2_connect, _m2_stop, _transport_status).
    bool Live(const std::string& online_id);
    void M2Connect(const std::string& online_id);
    void M2Stop(const std::string& online_id);
    int TransportStatus(const std::string& online_id);

    // The room's (hle_np_signaling_peer_known, _peer_dead, _reset).
    void PeerKnown(std::uint16_t member_id);
    void PeerDead(const std::string& online_id);
    void Reset();

private:
    enum class State { Inactive, SendingOffer, WaitOffer, SendingAccept, WaitAccept, ConnCheck, Established };
    struct Ctx {
        void* cb = nullptr;
        void* arg = nullptr;
    };
    struct Transport {
        std::string online_id;
        std::uint16_t member_id = 0; // the peer table's entry, 0 until known
        std::uint32_t addr = 0;      // network order
        std::uint16_t port = 0;      // host order
        State state = State::SendingOffer;
        bool peer_activated = false;
        bool peer_established = false;
        bool established_fired = false;
        bool stepping = false; // the resend timer is running
        int waited_ms = 0;     // handshake time so far
        std::int64_t started_ms = -1; // part 9: the first greeting sent or heard
        bool heard = false;           // part 9: the peer has sent something on it
    };
    struct Conn {
        unsigned ctx = 0;
        std::string online_id;
        unsigned transport = 0;
        std::uint16_t member_id = 0;
        bool locally_activated = false; // the game activated it
        bool peer_activated = false;    // the peer's game activated its own (its ActivationRequest)
        bool established_fired = false;
        bool mutual_fired = false;
        bool peer_activated_fired = false;
        bool dead = false;
        bool start_handshake = false; // waiting for the peer's address, then handshake
        int resolve_waited_ms = 0;
    };
    using Event = std::tuple<unsigned, unsigned, std::uint16_t, int>; // ctx, conn, event, error

    void Log(const char* format, ...) __attribute__((format(printf, 2, 3)));
    void Fire(unsigned ctx, unsigned conn, std::uint16_t event, int error);
    void FireAll(const std::vector<Event>& events);
    void QueueAll(std::vector<Event> events);
    void QueueM2Dead(const std::string& who, int error);
    unsigned AllocConnIdLocked();
    unsigned FindTransportLocked(const std::string& online_id);
    unsigned EnsureTransportLocked(const std::string& online_id, std::uint32_t addr, std::uint16_t port);
    unsigned FindConnLocked(const std::string& online_id, unsigned ctx = 0);
    void RefreshAddrLocked(Transport& t);
    void SendLocked(Transport& t, std::uint8_t kind, std::uint8_t reason = 1, std::uint64_t nonce = 0);
    void SetPeerStatusLocked(unsigned tid, int status);
    void SendHandshakeLocked(Transport& t);
    void StartSteppingLocked(unsigned tid);
    void Step(unsigned tid);
    void EstablishLocked(unsigned tid, bool peer_activated_hint, std::vector<Event>& events);
    void CloseConnDeadLocked(unsigned conn_id, int error, std::vector<Event>& events);
    void CloseTransportDeadLocked(unsigned tid, int error, std::vector<Event>& events);
    void Activation(unsigned conn_id);
    static int StatusOf(const Transport& t);
    std::int64_t NowMs() const;
    void NoteUpLocked(const Transport& t);

    Env env_;
    std::mutex mu_;
    std::map<unsigned, Ctx> ctx_;
    std::map<unsigned, Transport> tr_;   // by transport id
    std::map<unsigned, Conn> conn_;      // by connection id
    unsigned next_ctx_ = 1;
    unsigned next_tr_ = 1;
    unsigned conn_seed_ = 0; // 0 until the first connection: then a random start
    Stats stats_;
};

} // namespace BbThd
