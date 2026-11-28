// SPDX-License-Identifier: GPL-3.0-or-later
// sceNpSignaling with the connection handshake between games. Ported from droogie/bbhost
// src/hle/np_signaling.cpp at a317321 (v0.2.35, unchanged since #217, 3f9c4c0), function for function;
// copyright the bbhost contributors. bbhost's notes, kept:
//
// The game's SocketState FSM (0x10d8bb0) is the only caller: state 4 activates a connection to a
// peer's NpId and expects ESTABLISHED, state 5 reads the status and expects 2 with the peer's address
// and port. The events are the ones the game's handler acts on (the co-op contract): 0x01 established
// (SocketState+0xac), 0x0c mutual activated (+0xad), 0x00 dead; 0x0b peer deactivated; 0x0a peer
// activated.
//
// Two layers, as shadPS4:
//   transport   one per other player: the link to that player and its handshake state (Inactive,
//               SendingOffer, WaitOffer, SendingAccept, WaitAccept, ConnCheck, Established)
//   connection  what the game holds (conn id, per context and player); its status is its
//               transport's: Established 2, Inactive 0, else 1
//
// The messages are datagrams starting fe 'b' 'b' 's' on the P2P port (thd_net.cpp):
//   control    ActivationRequest, ActivationAck, Established, Close
//   handshake  Offer, Accept, Check, CheckAck
//
// activate          ActivationRequest, and Offer (resent every 500 ms; 30 s without getting to
//                   Established: DEAD, timeout)
// ActivationRequest the peer's game activated: its transport made (or kept), a connection for it if
//                   the game has none (PEER_ACTIVATED once established), answered ActivationAck.
//                   Establishes nothing unless the transport already is.
// Offer             answered Accept, only if we have a transport for the sender; Accept -> Check;
//                   Check -> CheckAck
// CheckAck          the transport is Established: control Established to the peer, then ESTABLISHED
//                   (connections the game activated), MUTUAL_ACTIVATED (where the peer activated
//                   too), PEER_ACTIVATED (connections only the peer activated)
// deactivate        Close(deactivate) to the peer when established, then DEAD (terminated by
//                   myself); the connection is gone after
// Close             PEER_DEACTIVATED when it is a deactivate and the transport is established, else
//                   DEAD (terminated by peer)
#include "thd_signaling.h"
#include "thd_addr_text.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <random>

namespace BbThd {
namespace {

constexpr int kErrInvalidArg = static_cast<int>(0x80552715);
constexpr int kErrConnNotFound = static_cast<int>(0x8055270E);
constexpr int kErrTerminatedByPeer = static_cast<int>(0x80552710);
constexpr int kErrTimeout = static_cast<int>(0x80552711);
constexpr int kErrTerminatedByMyself = static_cast<int>(0x80552718);
constexpr std::uint16_t kEvDead = 0x00;
constexpr std::uint16_t kEvEstablished = 0x01;
constexpr std::uint16_t kEvPeerActivated = 0x0a;
constexpr std::uint16_t kEvPeerDeactivated = 0x0b;
constexpr std::uint16_t kEvMutualActivated = 0x0c;

enum Kind : std::uint8_t {
    kActivationRequest = 0x01,
    kActivationAck = 0x02,
    kClose = 0x03,
    kEstablishedMsg = 0x04,
    kOffer = 0x11,
    kAccept = 0x12,
    kCheck = 0x13,
    kCheckAck = 0x14,
};
enum Reason : std::uint8_t { kDeactivate = 1, kTerminate = 2 };
constexpr std::size_t kFromAt = sizeof(Signaling::kMagic) + 2;
constexpr std::size_t kToAt = kFromAt + Signaling::kIdLen;
constexpr std::size_t kNonceAt = kToAt + Signaling::kIdLen;
static_assert(kNonceAt + 8 == Signaling::kPacketLen, "the greeting is 46 bytes");

std::uint64_t NowUs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

const char* KindName(std::uint8_t k) {
    switch (k) {
    case kActivationRequest: return "ActivationRequest";
    case kActivationAck: return "ActivationAck";
    case kClose: return "Close";
    case kEstablishedMsg: return "Established";
    case kOffer: return "Offer";
    case kAccept: return "Accept";
    case kCheck: return "Check";
    case kCheckAck: return "CheckAck";
    default: return "?";
    }
}

std::uint16_t Swap16(std::uint16_t v) {
    return static_cast<std::uint16_t>((v << 8) | (v >> 8));
}

} // namespace

Signaling::Signaling(Env env) : env_(std::move(env)) {}

void Signaling::Log(const char* format, ...) {
    if (!env_.log) return;
    char line[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    env_.log(line);
}

int Signaling::StatusOf(const Transport& t) {
    return t.state == State::Established ? 2 : t.state == State::Inactive ? 0 : 1;
}

// Matching2's DEAD for that player when its connection closes, after NpSignaling's own DEAD on the
// same queue (shadPS4's CloseConnectionAndDispatchDead -> QueueMatching2DeadForTransport).
void Signaling::QueueM2Dead(const std::string& who, int error) {
    env_.dispatch(0, [this, who, error] {
        if (env_.m2_dead) env_.m2_dead(who, error);
    });
}

// The next free connection id, as shadPS4's AllocateConnectionIdLocked: a random start in
// 1..0x7fff, then counting up and wrapping, so two games do not hand out the same ids (bbhost used to
// start every game at 1).
unsigned Signaling::AllocConnIdLocked() {
    if (conn_seed_ == 0) {
        unsigned draw;
        if (env_.random) {
            draw = env_.random();
        } else {
            std::random_device rd;
            draw = static_cast<unsigned>(rd());
        }
        conn_seed_ = (draw / 0x4001u) & 0x7fffu;
        if (conn_seed_ == 0) conn_seed_ = 1;
    }
    for (int tried = 0; tried <= 0xffff; ++tried) {
        const unsigned candidate = conn_seed_++;
        if (conn_seed_ == 0 || conn_seed_ > 0x7fff) conn_seed_ = 1;
        if (candidate != 0 && conn_.find(candidate) == conn_.end()) return candidate;
    }
    return 0;
}

// (ctxId, connId, event, errorCode, arg), on the dispatcher. A connection that died before its event
// went out gets only DEAD (bbhost's fire).
void Signaling::Fire(unsigned ctx, unsigned conn, std::uint16_t event, int error) {
    Ctx c;
    {
        std::lock_guard lk(mu_);
        const auto it = ctx_.find(ctx);
        if (it == ctx_.end() || !it->second.cb) return;
        if (event != kEvDead) {
            const auto ci = conn_.find(conn);
            if (ci == conn_.end() || ci->second.dead) {
                Log("np: conn %u is dead: event 0x%x not delivered", conn, event);
                return;
            }
        }
        c = it->second;
    }
    if (env_.trace) Log("np: NpSignaling event 0x%x conn %u error 0x%x", event, conn, static_cast<unsigned>(error));
    if (env_.call_game) env_.call_game(c.cb, ctx, conn, event, error, c.arg);
}

void Signaling::FireAll(const std::vector<Event>& events) {
    for (const auto& [ctx, conn, event, error] : events) Fire(ctx, conn, event, error);
}

void Signaling::QueueAll(std::vector<Event> events) {
    if (events.empty()) return;
    env_.dispatch(0, [this, events = std::move(events)] { FireAll(events); });
}

// A live (not Inactive) transport to that player, or 0.
unsigned Signaling::FindTransportLocked(const std::string& online_id) {
    for (const auto& [id, t] : tr_) {
        if (t.state != State::Inactive && t.online_id == online_id) return id;
    }
    return 0;
}

// The player's live transport, or a new one sending Offer. Inactive ones with no connection left are
// dropped.
unsigned Signaling::EnsureTransportLocked(const std::string& online_id, std::uint32_t addr, std::uint16_t port) {
    if (const unsigned id = FindTransportLocked(online_id)) {
        Transport& t = tr_[id];
        if (addr && port) {
            t.addr = addr;
            t.port = port;
        }
        return id;
    }
    for (auto it = tr_.begin(); it != tr_.end();) {
        bool used = false;
        for (const auto& [cid, c] : conn_) used |= c.transport == it->first && !c.dead;
        it = it->second.state == State::Inactive && !used ? tr_.erase(it) : std::next(it);
    }
    const unsigned id = next_tr_++;
    Transport t;
    t.online_id = online_id;
    t.addr = addr;
    t.port = port;
    Peer p;
    if (env_.peer_find && env_.peer_find(online_id, &p)) {
        t.member_id = p.member_id;
        if (!t.addr && p.addr && p.port) {
            t.addr = p.addr;
            t.port = p.port;
        }
    }
    tr_[id] = t;
    return id;
}

// The game's live connection in this context to that player, or 0.
unsigned Signaling::FindConnLocked(const std::string& online_id, unsigned ctx) {
    for (const auto& [id, c] : conn_) {
        if (!c.dead && c.online_id == online_id && (!ctx || c.ctx == ctx)) return id;
    }
    return 0;
}

// The peer's current address from the room's peer table, if it is there.
void Signaling::RefreshAddrLocked(Transport& t) {
    Peer p;
    if (t.member_id && env_.peer_get && env_.peer_get(t.member_id, &p) && p.addr && p.port) {
        t.addr = p.addr;
        t.port = p.port;
    }
}

void Signaling::SendLocked(Transport& t, std::uint8_t kind, std::uint8_t reason, std::uint64_t nonce) {
    RefreshAddrLocked(t);
    if (!t.addr || !t.port || !env_.send) return;
    std::uint8_t pkt[kPacketLen] = {};
    std::memcpy(pkt, kMagic, sizeof(kMagic));
    pkt[4] = kind;
    pkt[5] = reason;
    const std::string me = env_.online_id ? env_.online_id() : std::string();
    std::memcpy(pkt + kFromAt, me.data(), std::min(me.size(), kIdLen));
    std::memcpy(pkt + kToAt, t.online_id.data(), std::min(t.online_id.size(), kIdLen));
    std::memcpy(pkt + kNonceAt, &nonce, sizeof(nonce));
    env_.send(t.addr, t.port, pkt, sizeof(pkt));
}

// The peer table's view of the game's connections to that player.
void Signaling::SetPeerStatusLocked(unsigned tid, int status) {
    if (!env_.peer_set_conn) return;
    for (const auto& [id, c] : conn_) {
        if (c.transport == tid && !c.dead && c.locally_activated && c.member_id) env_.peer_set_conn(c.member_id, id, status);
    }
}

// The handshake step this transport is at, sent (shadPS4's StartTransportHandshakeInitiator).
void Signaling::SendHandshakeLocked(Transport& t) {
    std::uint8_t kind = kOffer;
    std::uint64_t nonce = 0;
    switch (t.state) {
    case State::SendingAccept: kind = kAccept; break;
    case State::ConnCheck:
        kind = kCheck;
        nonce = NowUs();
        break;
    case State::Established: return;
    case State::WaitOffer:
    case State::Inactive: t.state = State::SendingOffer; [[fallthrough]];
    default: kind = kOffer; break;
    }
    if (t.started_ms < 0 && t.addr && t.port) t.started_ms = NowMs(); // part 9
    SendLocked(t, kind, kDeactivate, nonce);
}

// The resend timer for a transport still in its handshake.
void Signaling::StartSteppingLocked(unsigned tid) {
    Transport& t = tr_[tid];
    if (t.stepping) return;
    t.stepping = true;
    env_.dispatch(kStepMs, [this, tid] { Step(tid); });
}

// Every 500 ms until Established: the current step again; DEAD after 30 s.
void Signaling::Step(unsigned tid) {
    std::vector<Event> events;
    {
        std::lock_guard lk(mu_);
        const auto it = tr_.find(tid);
        if (it == tr_.end()) return;
        Transport& t = it->second;
        if (t.state == State::Established || t.state == State::Inactive) {
            t.stepping = false;
            return;
        }
        if (t.addr && t.port) {
            if (t.waited_ms >= kGiveUpMs) {
                Log("np: transport %u to %s: handshake not done in %d s: DEAD (timeout)", tid, t.online_id.c_str(),
                    kGiveUpMs / 1000);
                const std::string who = t.online_id;
                ++(t.heard ? stats_.timed_out_partial : stats_.timed_out_silent); // part 9
                CloseTransportDeadLocked(tid, kErrTimeout, events); // erases the transport
                env_.dispatch(0, [this, who] {
                    if (env_.m2_dead) env_.m2_dead(who, kM2ErrTimeout);
                });
            } else {
                SendHandshakeLocked(t);
                t.waited_ms += kStepMs;
                env_.dispatch(kStepMs, [this, tid] { Step(tid); });
            }
        } else {
            env_.dispatch(kStepMs, [this, tid] { Step(tid); });
        }
    }
    FireAll(events);
}

// The transport is up: control Established to the peer, and each connection on it its events, once
// each (shadPS4's EstablishConnection).
void Signaling::EstablishLocked(unsigned tid, bool peer_activated_hint, std::vector<Event>& events) {
    Transport& t = tr_[tid];
    if (peer_activated_hint) t.peer_activated = true;
    if (!t.established_fired) {
        t.state = State::Established;
        t.established_fired = true;
        NoteUpLocked(t);
        SendLocked(t, kEstablishedMsg);
        SetPeerStatusLocked(tid, 2);
        const std::string who = t.online_id;
        env_.dispatch(kM2EstablishDelayMs, [this, who] {
            if (env_.m2_up) env_.m2_up(who);
        });
    }
    for (auto& [id, c] : conn_) {
        if (c.transport != tid || c.dead) continue;
        if (c.peer_activated && !c.locally_activated && !c.peer_activated_fired) {
            c.peer_activated_fired = true;
            events.emplace_back(c.ctx, id, kEvPeerActivated, 0);
        }
        if (c.locally_activated && !c.established_fired) {
            c.established_fired = true;
            events.emplace_back(c.ctx, id, kEvEstablished, 0);
        }
        if (c.locally_activated && c.peer_activated && !c.mutual_fired) {
            c.mutual_fired = true;
            events.emplace_back(c.ctx, id, kEvMutualActivated, 0);
        }
    }
}

// One connection closed: its transport Inactive, DEAD, and the connection gone.
void Signaling::CloseConnDeadLocked(unsigned conn_id, int error, std::vector<Event>& events) {
    const auto it = conn_.find(conn_id);
    if (it == conn_.end() || it->second.dead) return;
    Conn& c = it->second;
    const auto tr = tr_.find(c.transport);
    if (tr != tr_.end()) tr->second.state = State::Inactive;
    c.dead = true;
    if (c.member_id && env_.peer_set_conn) env_.peer_set_conn(c.member_id, conn_id, 0);
    events.emplace_back(c.ctx, conn_id, kEvDead, error);
    conn_.erase(it);
}

// The whole transport closed: every connection on it DEAD and gone.
void Signaling::CloseTransportDeadLocked(unsigned tid, int error, std::vector<Event>& events) {
    const auto tr = tr_.find(tid);
    if (tr == tr_.end() || tr->second.state == State::Inactive) return;
    tr->second.state = State::Inactive;
    std::vector<unsigned> conns;
    for (const auto& [id, c] : conn_) {
        if (c.transport == tid && !c.dead) conns.push_back(id);
    }
    for (const unsigned id : conns) {
        Conn& c = conn_[id];
        c.dead = true;
        if (c.member_id && env_.peer_set_conn) env_.peer_set_conn(c.member_id, id, 0);
        events.emplace_back(c.ctx, id, kEvDead, error);
        conn_.erase(id);
    }
    tr_.erase(tr);
}

// ActivationRequest out, then the handshake, once the peer's address is known (shadPS4's
// ProcessPendingActivations): the room's peer table, else the server. Retried every 500 ms; DEAD
// (timeout) after 30 s without one. On the dispatcher, never on the game's thread.
void Signaling::Activation(unsigned conn_id) {
    std::string peer;
    {
        std::lock_guard lk(mu_);
        const auto it = conn_.find(conn_id);
        if (it == conn_.end() || it->second.dead) return;
        Transport& t = tr_[it->second.transport];
        RefreshAddrLocked(t);
        if (!t.addr || !t.port) {
            Peer p;
            if (env_.peer_find && env_.peer_find(t.online_id, &p) && p.addr && p.port) {
                t.member_id = p.member_id;
                it->second.member_id = p.member_id;
                t.addr = p.addr;
                t.port = p.port;
            }
        }
        if (!t.addr || !t.port) peer = t.online_id;
    }
    if (!peer.empty()) {
        // Not a room member yet (the host activates its connection to the guest it summons before
        // the guest joins): the server has its address from its context_start.
        std::uint32_t addr = 0;
        std::uint16_t port = 0;
        std::string error;
        const bool ok = env_.resolve && env_.resolve(peer, &addr, &port, error);
        std::vector<Event> events;
        {
            std::lock_guard lk(mu_);
            const auto it = conn_.find(conn_id);
            if (it == conn_.end() || it->second.dead) return;
            Conn& c = it->second;
            Transport& t = tr_[c.transport];
            if (ok && !t.addr) {
                const Peer p = env_.peer_provisional ? env_.peer_provisional(peer, addr, port) : Peer{};
                t.member_id = p.member_id;
                c.member_id = p.member_id;
                t.addr = addr;
                t.port = port;
                Log("np: conn %u to %s resolved %s:%u", conn_id, peer.c_str(), MaskedIpv4(addr).c_str(), port);
            } else if (!t.addr) {
                c.resolve_waited_ms += kStepMs;
                if (c.resolve_waited_ms >= kGiveUpMs) {
                    Log("np: conn %u to %s: no address in %d s (%s): DEAD (timeout)", conn_id, peer.c_str(),
                        kGiveUpMs / 1000, error.c_str());
                    ++stats_.no_address; // part 9
                    CloseConnDeadLocked(conn_id, kErrTimeout, events);
                } else {
                    env_.dispatch(kStepMs, [this, conn_id] { Activation(conn_id); });
                }
            }
        }
        FireAll(events);
        if (!ok) return;
    }
    std::vector<Event> events;
    {
        std::lock_guard lk(mu_);
        const auto it = conn_.find(conn_id);
        if (it == conn_.end() || it->second.dead) return;
        Conn& c = it->second;
        const unsigned tid = c.transport;
        Transport& t = tr_[tid];
        if (!t.addr || !t.port) return;
        if (c.member_id && env_.peer_set_conn) env_.peer_set_conn(c.member_id, conn_id, StatusOf(t));
        SendLocked(t, kActivationRequest);
        if (c.start_handshake) {
            c.start_handshake = false;
            SendHandshakeLocked(t);
            StartSteppingLocked(tid);
        } else if (t.state == State::Established) {
            EstablishLocked(tid, false, events);
        }
    }
    FireAll(events);
}

int Signaling::Initialize() {
    Log("sceNpSignalingInitialize");
    return 0;
}

int Signaling::Terminate() {
    std::lock_guard lk(mu_);
    ctx_.clear();
    conn_.clear();
    tr_.clear();
    return 0;
}

// (const SceNpId*, callback, arg, ctxId*)
int Signaling::CreateContext(void* cb, void* arg, unsigned* ctx) {
    if (!ctx) return kErrInvalidArg;
    std::lock_guard lk(mu_);
    const unsigned id = next_ctx_++;
    ctx_[id] = Ctx{cb, arg};
    *ctx = id;
    Log("sceNpSignalingCreateContext -> %u", id);
    return 0;
}

int Signaling::DeleteContext(unsigned ctx) {
    std::lock_guard lk(mu_);
    ctx_.erase(ctx);
    for (auto it = conn_.begin(); it != conn_.end();) it = it->second.ctx == ctx ? conn_.erase(it) : std::next(it);
    return 0;
}

// (ctxId, const SceNpId* peer, connId*), as shadPS4's ActivateSig1: one live connection per context
// and player; a new or re-activated one sends ActivationRequest and starts the handshake, unless its
// transport is already up (then ESTABLISHED at once).
int Signaling::Activate(unsigned ctx, const std::string& peer, unsigned* conn_out) {
    if (!conn_out) return kErrInvalidArg;
    std::vector<Event> events;
    unsigned id = 0;
    bool queue = false;
    {
        std::lock_guard lk(mu_);
        id = FindConnLocked(peer, ctx);
        const unsigned tid = EnsureTransportLocked(peer, 0, 0);
        Transport& t = tr_[tid];
        if (!id) {
            id = AllocConnIdLocked();
            Conn c;
            c.ctx = ctx;
            c.online_id = peer;
            c.transport = tid;
            c.member_id = t.member_id;
            c.locally_activated = true;
            c.start_handshake = t.state != State::Established;
            conn_[id] = c;
            queue = true;
        } else {
            Conn& c = conn_[id];
            const bool was_local = c.locally_activated;
            c.transport = tid;
            c.locally_activated = true;
            if (!c.member_id) c.member_id = t.member_id;
            if (t.state != State::Established) {
                if (t.state == State::Inactive || t.state == State::WaitOffer) t.state = State::SendingOffer;
                c.start_handshake = true;
            }
            queue = !was_local;
        }
        Log("sceNpSignalingActivateConnection %s -> conn %u%s", peer.c_str(), id,
            t.state == State::Established ? " (transport already up)" : "");
        if (t.state == State::Established) EstablishLocked(tid, false, events);
    }
    *conn_out = id;
    if (queue) env_.dispatch(0, [this, id] { Activation(id); });
    QueueAll(std::move(events));
    return 0;
}

// As shadPS4's DeactivateSig1: Close(deactivate) to the peer when the transport is up, then DEAD, and
// the connection is gone (a second deactivate is CONN_NOT_FOUND). NpMatching2's DEAD for that player
// is kept for its LeaveRoom.
int Signaling::Deactivate(unsigned ctx, unsigned conn) {
    std::string peer;
    std::vector<Event> events;
    {
        std::lock_guard lk(mu_);
        const auto it = conn_.find(conn);
        if (it == conn_.end() || it->second.ctx != ctx) {
            Log("sceNpSignalingDeactivateConnection conn %u: not found", conn);
            return kErrConnNotFound;
        }
        Conn& c = it->second;
        Log("sceNpSignalingDeactivateConnection conn %u (%s)", conn, c.online_id.c_str());
        peer = c.online_id;
        const auto tr = tr_.find(c.transport);
        if (tr != tr_.end() && tr->second.state == State::Established && !c.dead) SendLocked(tr->second, kClose, kDeactivate);
        ++stats_.closed_by_us; // part 9
        CloseConnDeadLocked(conn, kErrTerminatedByMyself, events);
    }
    if (env_.m2_closed) env_.m2_closed(peer);
    QueueAll(std::move(events));
    QueueM2Dead(peer, kM2ErrTerminatedByMyself);
    return 0;
}

// (ctxId, connId, status*, SceNetInAddr*, SceInPort_t*): 0 inactive, 1 pending, 2 active - the
// connection's transport's.
int Signaling::Status(unsigned ctx, unsigned conn, int* st, std::uint32_t* addr, std::uint16_t* port) {
    int status = 0;
    std::uint32_t a = 0;
    std::uint16_t p = 0;
    {
        std::lock_guard lk(mu_);
        const auto it = conn_.find(conn);
        if (it != conn_.end() && it->second.ctx == ctx && !it->second.dead) {
            const auto tr = tr_.find(it->second.transport);
            if (tr != tr_.end()) {
                Transport& t = tr->second;
                status = StatusOf(t);
                if (status == 2) {
                    RefreshAddrLocked(t);
                    a = t.addr;
                    p = Swap16(t.port);
                }
            }
        }
    }
    if (st) *st = status;
    if (addr) *addr = a;
    if (port) *port = p;
    if (env_.trace) Log("sceNpSignalingGetConnectionStatus conn %u -> %d", conn, status);
    return 0;
}

// A control datagram from another game (the P2P port), on the dispatcher.
void Signaling::Control(const std::uint8_t* data, std::size_t len, std::uint32_t addr, std::uint16_t port) {
    if (len < kPacketLen || !IsSignal(data, len)) return;
    const std::uint8_t kind = data[4];
    const auto reason = static_cast<Reason>(data[5]);
    std::string from(reinterpret_cast<const char*>(data + kFromAt), kIdLen);
    from.resize(std::strlen(from.c_str()));
    if (from.empty()) return;
    std::uint64_t nonce = 0;
    std::memcpy(&nonce, data + kNonceAt, sizeof(nonce));
    const std::uint16_t from_port = Swap16(port);
    if (env_.trace) Log("np: signaling %s from %s", KindName(kind), from.c_str());
    std::vector<Event> events;
    {
        std::lock_guard lk(mu_);
        unsigned tid = FindTransportLocked(from);
        // Part 9: the peer has answered on this link; its handshake time counts from first contact.
        const auto heard = [this](Transport& t) {
            t.heard = true;
            if (t.started_ms < 0) t.started_ms = NowMs();
        };
        if (tid) heard(tr_[tid]);
        switch (kind) {
        case kActivationRequest: {
            // The peer's game activated its connection to us.
            if (!tid) tid = EnsureTransportLocked(from, addr, from_port);
            Transport& t = tr_[tid];
            t.peer_activated = true;
            t.addr = addr;
            t.port = from_port;
            if (t.state == State::Inactive) t.state = State::WaitOffer;
            const unsigned ctx = ctx_.empty() ? 0 : ctx_.begin()->first;
            unsigned cid = ctx ? FindConnLocked(from, ctx) : 0;
            if (ctx && !cid) {
                cid = AllocConnIdLocked();
                Conn c;
                c.ctx = ctx;
                c.online_id = from;
                c.transport = tid;
                c.member_id = t.member_id;
                c.peer_activated = true;
                conn_[cid] = c;
            }
            if (cid) {
                conn_[cid].peer_activated = true;
                conn_[cid].transport = tid;
            }
            if (t.state == State::Established) EstablishLocked(tid, false, events);
            else StartSteppingLocked(tid);
            SendLocked(t, kActivationAck);
            break;
        }
        case kOffer: {
            if (!tid) break; // no transport for the sender: ignored
            Transport& t = tr_[tid];
            t.addr = addr;
            t.port = from_port;
            t.peer_activated = true;
            if (t.state != State::Established) t.state = State::SendingAccept;
            SendLocked(t, kAccept);
            break;
        }
        case kAccept: {
            if (!tid) break;
            Transport& t = tr_[tid];
            t.peer_activated = true;
            t.addr = addr;
            t.port = from_port;
            if (t.state == State::SendingOffer || t.state == State::WaitAccept || t.state == State::SendingAccept ||
                t.state == State::WaitOffer) {
                t.state = State::ConnCheck;
                SendLocked(t, kCheck, kDeactivate, NowUs());
            }
            break;
        }
        case kCheck: {
            if (!tid) break;
            Transport& t = tr_[tid];
            SendLocked(t, kCheckAck, kDeactivate, nonce);
            if (t.state == State::SendingAccept || t.state == State::WaitOffer) t.state = State::ConnCheck;
            break;
        }
        case kCheckAck: {
            if (!tid) break;
            if (tr_[tid].state != State::Established) EstablishLocked(tid, true, events);
            break;
        }
        case kActivationAck: {
            if (!tid) break;
            tr_[tid].peer_activated = true;
            break;
        }
        case kEstablishedMsg: {
            if (!tid) break;
            Transport& t = tr_[tid];
            t.peer_established = true;
            if (t.state == State::Established) EstablishLocked(tid, false, events);
            break;
        }
        case kClose: {
            if (!tid) break;
            const unsigned cid = FindConnLocked(from);
            if (reason == kDeactivate && cid && tr_[tid].state == State::Established) {
                events.emplace_back(conn_[cid].ctx, cid, kEvPeerDeactivated, 0);
            } else {
                ++stats_.closed_by_peer; // part 9: the link closes (a deactivate on one that stays up does not)
                if (cid) CloseConnDeadLocked(cid, kErrTerminatedByPeer, events);
                else tr_[tid].state = State::Inactive;
                QueueM2Dead(from, kM2ErrTerminatedByPeer);
            }
            break;
        }
        default:
            break;
        }
        if (!tid) {
            if (const unsigned made = FindTransportLocked(from)) heard(tr_[made]); // made by this datagram
        }
    }
    FireAll(events);
}

bool Signaling::Live(const std::string& online_id) {
    std::lock_guard lk(mu_);
    return FindConnLocked(online_id) != 0;
}

// shadPS4's StartMatching2, for one member.
void Signaling::M2Connect(const std::string& online_id) {
    std::lock_guard lk(mu_);
    const unsigned tid = EnsureTransportLocked(online_id, 0, 0);
    Transport& t = tr_[tid];
    RefreshAddrLocked(t);
    if (t.state == State::WaitOffer) t.state = State::SendingOffer;
    if (env_.trace) Log("np: matching2 signaling to %s: transport %u status %d", online_id.c_str(), tid, StatusOf(t));
    if (t.state == State::Established) {
        const std::string who = online_id;
        env_.dispatch(kM2EstablishDelayMs, [this, who] {
            if (env_.m2_up) env_.m2_up(who);
        });
        return;
    }
    SendHandshakeLocked(t);
    StartSteppingLocked(tid);
}

int Signaling::TransportStatus(const std::string& online_id) {
    std::lock_guard lk(mu_);
    const unsigned tid = FindTransportLocked(online_id);
    return tid ? StatusOf(tr_[tid]) : 0;
}

// shadPS4's StopMatching2, for one member: the transport closes unless an NpSignaling connection of
// the game's is on it.
void Signaling::M2Stop(const std::string& online_id) {
    std::lock_guard lk(mu_);
    if (FindConnLocked(online_id)) return;
    if (const unsigned tid = FindTransportLocked(online_id)) tr_.erase(tid);
}

// A player is now a room member: its transport and connections know its peer-table entry (the
// address), and a connection waiting for it goes on.
void Signaling::PeerKnown(std::uint16_t member_id) {
    Peer p;
    if (!env_.peer_get || !env_.peer_get(member_id, &p)) return;
    std::vector<unsigned> waiting;
    {
        std::lock_guard lk(mu_);
        for (auto& [id, t] : tr_) {
            if (t.online_id != p.online_id || t.state == State::Inactive) continue;
            t.member_id = member_id;
            if (p.addr && p.port) {
                t.addr = p.addr;
                t.port = p.port;
            }
        }
        for (auto& [id, c] : conn_) {
            if (c.dead || c.online_id != p.online_id) continue;
            c.member_id = member_id;
            if (c.start_handshake) waiting.push_back(id);
        }
    }
    for (const unsigned id : waiting) env_.dispatch(0, [this, id] { Activation(id); });
}

// The player left the room for good: its connections DEAD (terminated by peer).
void Signaling::PeerDead(const std::string& online_id) {
    std::vector<Event> events;
    {
        std::lock_guard lk(mu_);
        if (const unsigned tid = FindTransportLocked(online_id)) CloseTransportDeadLocked(tid, kErrTerminatedByPeer, events);
        while (const unsigned cid = FindConnLocked(online_id)) CloseConnDeadLocked(cid, kErrTerminatedByPeer, events);
        if (!events.empty()) ++stats_.closed_by_peer; // part 9
    }
    QueueAll(std::move(events));
}

// ---- 0.4.6 beta 14 part 9: the session's line

std::int64_t Signaling::NowMs() const {
    if (env_.now_ms) return env_.now_ms();
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void Signaling::NoteUpLocked(const Transport& t) {
    const int ms = t.started_ms >= 0 ? static_cast<int>(NowMs() - t.started_ms) : 0;
    if (!stats_.up || ms < stats_.handshake_min_ms) stats_.handshake_min_ms = ms;
    if (!stats_.up || ms > stats_.handshake_max_ms) stats_.handshake_max_ms = ms;
    ++stats_.up;
    if (env_.relayed && env_.relayed(t.addr, t.port)) ++stats_.relayed;
}

Signaling::Stats Signaling::TakeStats() {
    std::lock_guard lk(mu_);
    const Stats s = stats_;
    stats_ = Stats{};
    return s;
}

void Signaling::Reset() {
    std::lock_guard lk(mu_);
    conn_.clear();
    tr_.clear();
}

} // namespace BbThd
