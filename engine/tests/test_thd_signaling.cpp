// SPDX-License-Identifier: GPL-3.0-or-later
// 0.4.6 beta 14: NpSignaling's handshake between games (gpu/bbnet/thd/thd_signaling.cpp, the port of
// droogie/bbhost #217) against bbhost's own code: tests/third_party/bbhost/np_signaling.cpp at a317321,
// built only into this test.
//
// Two players on one fake clock and an in-memory network (20 ms each way; with loss and reordering
// in one case). Each case runs three ways: bbhost's code as player A and our port as B, our port as
// both, and our port as A with bbhost's code as B. bbhost's file keeps its state in globals, so it can
// be only one of the two. Everything either player shows the outside world is recorded: the game's
// NpSignaling events and the calls' answers, Matching2's hooks, the peer table's writes, and every
// datagram on the wire with its time. The three runs must record the same, to the millisecond: our
// port in either role is then indistinguishable from bbhost's. Each case also checks what the game
// must see (ESTABLISHED, PEER_ACTIVATED, DEAD and its error, ...).
#include "gpu/bbnet/thd/thd_signaling.h"

#include "core/thunk.h"
#include "hle/net_p2p.h"
#include "hle/np.h"
#include "log.h"
#include "net/session.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

using BbThd::Signaling;

namespace {

int g_fail = 0;
#define CHECK(x)                                                                                                    \
    do {                                                                                                            \
        if (!(x)) {                                                                                                 \
            ++g_fail;                                                                                               \
            std::printf("FAIL %s line %d: %s\n", g_case.c_str(), __LINE__, #x);                                     \
        }                                                                                                           \
    } while (0)
std::string g_case;

constexpr std::uint16_t kEvDead = 0x00, kEvEstablished = 0x01, kEvPeerActivated = 0x0a, kEvPeerDeactivated = 0x0b,
                        kEvMutual = 0x0c;
constexpr int kErrConnNotFound = static_cast<int>(0x8055270E);
constexpr int kErrTerminatedByPeer = static_cast<int>(0x80552710);
constexpr int kErrTimeout = static_cast<int>(0x80552711);
constexpr int kErrTerminatedByMyself = static_cast<int>(0x80552718);

std::string Fmt(const char* format, ...) __attribute__((format(printf, 1, 2)));
std::string Fmt(const char* format, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, format);
    std::vsnprintf(buf, sizeof buf, format, ap);
    va_end(ap);
    return buf;
}

std::uint32_t Ip(unsigned last) { // 10.0.0.last, network order
    const std::uint8_t b[4] = {10, 0, 0, static_cast<std::uint8_t>(last)};
    std::uint32_t v;
    std::memcpy(&v, b, 4);
    return v;
}
std::uint16_t Swap(std::uint16_t v) { return static_cast<std::uint16_t>((v << 8) | (v >> 8)); }

// ---- the clock: both games' dispatchers, one queue in time order (ties in the order queued).
struct Clock {
    std::int64_t now = 0;
    std::uint64_t seq = 0;
    std::map<std::pair<std::int64_t, std::uint64_t>, std::function<void()>> q;
    void After(int ms, std::function<void()> fn) { q.emplace(std::make_pair(now + ms, seq++), std::move(fn)); }
    void RunUntil(std::int64_t t) {
        while (!q.empty() && q.begin()->first.first <= t) {
            auto it = q.begin();
            now = it->first.first;
            std::function<void()> fn = std::move(it->second);
            q.erase(it);
            fn();
        }
        now = t;
    }
};

struct TPeer {
    std::uint16_t member_id = 0;
    std::string online_id;
    std::uint32_t addr = 0; // network order
    std::uint16_t port = 0; // host order
};

struct World;

// One game: bbhost's code or our port, its peer table, and what it showed.
struct Player {
    World* w = nullptr;
    std::string name;
    std::uint32_t addr = 0;
    std::uint16_t port = 3658;
    bool bbhost = false;
    bool offline = false; // sends nothing and hears nothing: a peer that never answers
    std::unique_ptr<Signaling> sig;
    unsigned ctx = 0;
    std::map<std::uint16_t, TPeer> peers;
    std::vector<std::string> trace; // what the outside saw, in order
    std::vector<std::string> log;
    std::map<unsigned, unsigned> conn_names; // conn id -> 1, 2, ... in order of first sight
    struct Ev {
        unsigned conn;
        std::uint16_t event;
        int error;
    };
    std::vector<Ev> events;

    unsigned ConnName(unsigned id) {
        if (!id) return 0;
        auto it = conn_names.find(id);
        if (it != conn_names.end()) return it->second;
        const unsigned n = static_cast<unsigned>(conn_names.size()) + 1;
        conn_names[id] = n;
        return n;
    }
    void Note(const std::string& what);
    void OnEvent(unsigned c, unsigned conn, std::uint16_t event, int error) {
        Note(Fmt("event ctx %s conn %u 0x%02x error 0x%08x", c == ctx ? "own" : "other", ConnName(conn), event,
                 static_cast<unsigned>(error)));
        events.push_back({ConnName(conn), event, error});
    }

    // The game's calls.
    void Init();
    void Term();
    unsigned Activate(const std::string& peer);
    int Deactivate(unsigned conn);
    void Status(unsigned conn);
    // Matching2's and the room's.
    void Control(const std::vector<std::uint8_t>& d, std::uint32_t from_addr, std::uint16_t from_port_nbo);
    void Live(const std::string& who);
    void M2Connect(const std::string& who);
    void M2Stop(const std::string& who);
    void TransportStatus(const std::string& who);
    void PeerKnown(std::uint16_t member_id);
    void PeerDead(const std::string& who);

    // The peer table, as either side's code sees it.
    bool PeerGet(std::uint16_t member_id, TPeer* out) const {
        auto it = peers.find(member_id);
        if (it == peers.end()) return false;
        *out = it->second;
        return true;
    }
    bool PeerFind(const std::string& who, TPeer* out) const {
        for (const auto& [id, p] : peers)
            if (p.online_id == who) {
                *out = p;
                return true;
            }
        return false;
    }
    void PeerSetConn(std::uint16_t member_id, unsigned conn, int status) {
        Note(Fmt("peer table: member %u conn %u status %d", member_id, ConnName(conn), status));
    }
    TPeer PeerProvisional(const std::string& who, std::uint32_t a, std::uint16_t p) {
        TPeer out;
        if (PeerFind(who, &out)) return out;
        out.member_id = static_cast<std::uint16_t>(0x8000 + peers.size());
        out.online_id = who;
        out.addr = a;
        out.port = p;
        peers[out.member_id] = out;
        Note(Fmt("peer table: %s provisional as member %u", who.c_str(), out.member_id));
        return out;
    }
    void Send(std::uint32_t to_addr, std::uint16_t to_port, const std::uint8_t* data, std::size_t len);
};

struct World {
    Clock clock;
    Player a, b;
    std::map<std::string, TPeer> server; // the players the server can resolve
    int loss_pct = 0;                    // datagrams lost, and the rest reordered (0: none)
    std::mt19937 rng{12345};
    std::vector<std::string> wire;

    Player* At(std::uint32_t addr, std::uint16_t port) {
        if (a.addr == addr && a.port == port) return &a;
        if (b.addr == addr && b.port == port) return &b;
        return nullptr;
    }
};

World* g_world = nullptr;
Player* g_bb = nullptr; // the player bbhost's code is, if any
std::map<std::string, void*> g_fns;

void Player::Note(const std::string& what) { trace.push_back(Fmt("%6ld %s", static_cast<long>(w->clock.now), what.c_str())); }

const char* KindName(std::uint8_t k) {
    switch (k) {
    case 0x01: return "ActivationRequest";
    case 0x02: return "ActivationAck";
    case 0x03: return "Close";
    case 0x04: return "Established";
    case 0x11: return "Offer";
    case 0x12: return "Accept";
    case 0x13: return "Check";
    case 0x14: return "CheckAck";
    default: return "?";
    }
}

void Player::Send(std::uint32_t to_addr, std::uint16_t to_port, const std::uint8_t* data, std::size_t len) {
    World& W = *w;
    Player* to = W.At(to_addr, to_port);
    std::string from_id, to_id;
    if (len >= Signaling::kPacketLen) {
        from_id.assign(reinterpret_cast<const char*>(data + 6), strnlen(reinterpret_cast<const char*>(data + 6), 16));
        to_id.assign(reinterpret_cast<const char*>(data + 22), strnlen(reinterpret_cast<const char*>(data + 22), 16));
    }
    bool lost = offline || !to || to->offline || len != Signaling::kPacketLen || !Signaling::IsSignal(data, len);
    int latency = 20;
    if (!lost && W.loss_pct) {
        lost = static_cast<int>(W.rng() % 100) < W.loss_pct;
        latency = 5 + static_cast<int>(W.rng() % 60); // so later ones overtake earlier ones
    }
    W.wire.push_back(Fmt("%6ld %s -> %s:%u %s(%u) %s->%s %u B%s", static_cast<long>(W.clock.now), name.c_str(),
                         to ? to->name.c_str() : "?", to_port, len > 4 ? KindName(data[4]) : "?", len > 5 ? data[5] : 0,
                         from_id.c_str(), to_id.c_str(), static_cast<unsigned>(len), lost ? " lost" : ""));
    if (lost) return;
    std::vector<std::uint8_t> d(data, data + len);
    const std::uint32_t from_addr = addr;
    const std::uint16_t from_port = Swap(port);
    W.clock.After(latency, [to, d, from_addr, from_port] {
        if (!to->offline) to->Control(d, from_addr, from_port);
    });
}

// ---- bbhost's side of the world (the shim headers' declarations)

std::int64_t BbGameCallback(unsigned ctx, unsigned conn, std::uint16_t event, int error, void* arg) {
    static_cast<Player*>(arg)->OnEvent(ctx, conn, event, error);
    return 0;
}
template <typename F>
F BbFn(const char* name) {
    auto it = g_fns.find(name);
    if (it == g_fns.end()) {
        std::printf("bbhost's %s is not registered\n", name);
        std::exit(2);
    }
    return reinterpret_cast<F>(it->second);
}

} // namespace

void register_hle_fn(const char* name, void* fn) { g_fns[name] = fn; }
void host_log(const char* format, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, format);
    std::vsnprintf(buf, sizeof buf, format, ap);
    va_end(ap);
    if (g_bb) g_bb->log.push_back(buf);
}
void hle_net_p2p_signal(std::uint32_t addr, std::uint16_t port, const void* data, std::size_t len) {
    g_bb->Send(addr, port, static_cast<const std::uint8_t*>(data), len);
}
std::string hle_np_npid_text(const void* npid) { return static_cast<const char*>(npid); }
void hle_np_matching2_peer_closed(const std::string& who) { g_bb->Note("matching2: peer closed " + who); }
void hle_np_matching2_transport_up(const std::string& who) { g_bb->Note("matching2: transport up " + who); }
void hle_np_matching2_transport_dead(const std::string& who, int error) {
    g_bb->Note(Fmt("matching2: transport dead %s error 0x%08x", who.c_str(), static_cast<unsigned>(error)));
}

namespace net {
void dispatch_after(int delay_ms, Prio, std::function<void()> fn) { g_world->clock.After(delay_ms, std::move(fn)); }
std::string online_id() { return g_bb->name; }
static Peer ToNet(const TPeer& p) {
    Peer o;
    o.member_id = p.member_id;
    o.online_id = p.online_id;
    o.addr = p.addr;
    o.port = p.port;
    return o;
}
bool peers_get(std::uint16_t member_id, Peer* out) {
    TPeer p;
    if (!g_bb->PeerGet(member_id, &p)) return false;
    *out = ToNet(p);
    return true;
}
bool peers_find_online(const std::string& who, Peer* out) {
    TPeer p;
    if (!g_bb->PeerFind(who, &p)) return false;
    *out = ToNet(p);
    return true;
}
void peers_set_conn(std::uint16_t member_id, unsigned conn_id, int sig_status) {
    g_bb->PeerSetConn(member_id, conn_id, sig_status);
}
Peer peers_provisional(const std::string& who, std::uint32_t addr, std::uint16_t port) {
    return ToNet(g_bb->PeerProvisional(who, addr, port));
}
bool server_signaling_resolve(const std::string& who, std::uint32_t* addr, std::uint16_t* port, std::string& error) {
    auto it = g_world->server.find(who);
    if (it == g_world->server.end()) {
        error = "not signed in";
        return false;
    }
    *addr = it->second.addr;
    *port = it->second.port;
    return true;
}
} // namespace net

namespace {

Signaling::Peer ToSig(const TPeer& p) {
    Signaling::Peer o;
    o.member_id = p.member_id;
    o.online_id = p.online_id;
    o.addr = p.addr;
    o.port = p.port;
    return o;
}

// ---- the game's calls, to whichever code the player is

void Player::Init() {
    if (bbhost) {
        g_bb = this;
        BbFn<int (*)(std::uint64_t, int, int, std::uint64_t)>("sceNpSignalingInitialize")(0, 0, 0, 0);
        const int rc = BbFn<int (*)(const void*, void*, void*, unsigned*)>("sceNpSignalingCreateContext")(
            nullptr, reinterpret_cast<void*>(&BbGameCallback), this, &ctx);
        Note(Fmt("create context -> %d", rc));
        return;
    }
    Signaling::Env e;
    e.send = [this](std::uint32_t a, std::uint16_t p, const std::uint8_t* d, std::size_t n) { Send(a, p, d, n); };
    e.dispatch = [this](int ms, std::function<void()> fn) { w->clock.After(ms, std::move(fn)); };
    e.call_game = [](void*, unsigned c, unsigned conn, std::uint16_t event, int error, void* arg) {
        static_cast<Player*>(arg)->OnEvent(c, conn, event, error);
    };
    e.peer_get = [this](std::uint16_t m, Signaling::Peer* out) {
        TPeer p;
        if (!PeerGet(m, &p)) return false;
        *out = ToSig(p);
        return true;
    };
    e.peer_find = [this](const std::string& who, Signaling::Peer* out) {
        TPeer p;
        if (!PeerFind(who, &p)) return false;
        *out = ToSig(p);
        return true;
    };
    e.peer_set_conn = [this](std::uint16_t m, unsigned conn, int status) { PeerSetConn(m, conn, status); };
    e.peer_provisional = [this](const std::string& who, std::uint32_t a, std::uint16_t p) {
        return ToSig(PeerProvisional(who, a, p));
    };
    e.resolve = [this](const std::string& who, std::uint32_t* a, std::uint16_t* p, std::string& error) {
        auto it = w->server.find(who);
        if (it == w->server.end()) {
            error = "not signed in";
            return false;
        }
        *a = it->second.addr;
        *p = it->second.port;
        return true;
    };
    e.online_id = [this] { return name; };
    e.m2_up = [this](const std::string& who) { Note("matching2: transport up " + who); };
    e.m2_dead = [this](const std::string& who, int error) {
        Note(Fmt("matching2: transport dead %s error 0x%08x", who.c_str(), static_cast<unsigned>(error)));
    };
    e.m2_closed = [this](const std::string& who) { Note("matching2: peer closed " + who); };
    e.log = [this](const char* line) { log.push_back(line); };
    e.random = [] { return 0x4001u * 1234u; };
    sig = std::make_unique<Signaling>(std::move(e));
    sig->Initialize();
    const int rc = sig->CreateContext(reinterpret_cast<void*>(&BbGameCallback), this, &ctx);
    Note(Fmt("create context -> %d", rc));
}

void Player::Term() {
    if (bbhost) {
        BbFn<int (*)()>("sceNpSignalingTerminate")();
        g_bb = nullptr;
    } else if (sig) {
        sig->Terminate();
    }
}

unsigned Player::Activate(const std::string& peer) {
    unsigned conn = 0;
    const int rc = bbhost ? BbFn<int (*)(unsigned, const void*, unsigned*)>("sceNpSignalingActivateConnection")(
                                ctx, peer.c_str(), &conn)
                          : sig->Activate(ctx, peer, &conn);
    Note(Fmt("activate %s -> 0x%x conn %u", peer.c_str(), static_cast<unsigned>(rc), ConnName(conn)));
    return conn;
}

int Player::Deactivate(unsigned conn) {
    const int rc = bbhost ? BbFn<int (*)(unsigned, unsigned)>("sceNpSignalingDeactivateConnection")(ctx, conn)
                          : sig->Deactivate(ctx, conn);
    Note(Fmt("deactivate conn %u -> 0x%08x", ConnName(conn), static_cast<unsigned>(rc)));
    return rc;
}

void Player::Status(unsigned conn) {
    int st = -1;
    std::uint32_t a = 0;
    std::uint16_t p = 0;
    const int rc = bbhost ? BbFn<int (*)(unsigned, unsigned, int*, std::uint32_t*, std::uint16_t*)>(
                                "sceNpSignalingGetConnectionStatus")(ctx, conn, &st, &a, &p)
                          : sig->Status(ctx, conn, &st, &a, &p);
    Note(Fmt("status conn %u -> 0x%x: %d %08x:%04x", ConnName(conn), static_cast<unsigned>(rc), st, a, p));
}

void Player::Control(const std::vector<std::uint8_t>& d, std::uint32_t from_addr, std::uint16_t from_port_nbo) {
    if (bbhost) hle_np_signaling_control(d.data(), d.size(), from_addr, from_port_nbo);
    else sig->Control(d.data(), d.size(), from_addr, from_port_nbo);
}
void Player::Live(const std::string& who) {
    Note(Fmt("live %s -> %d", who.c_str(), bbhost ? hle_np_signaling_live(who) : sig->Live(who)));
}
void Player::M2Connect(const std::string& who) {
    Note("matching2 connect " + who);
    if (bbhost) hle_np_signaling_m2_connect(who);
    else sig->M2Connect(who);
}
void Player::M2Stop(const std::string& who) {
    Note("matching2 stop " + who);
    if (bbhost) hle_np_signaling_m2_stop(who);
    else sig->M2Stop(who);
}
void Player::TransportStatus(const std::string& who) {
    Note(Fmt("transport status %s -> %d", who.c_str(),
             bbhost ? hle_np_signaling_transport_status(who) : sig->TransportStatus(who)));
}
void Player::PeerKnown(std::uint16_t member_id) {
    Note(Fmt("peer known %u", member_id));
    if (bbhost) hle_np_signaling_peer_known(member_id);
    else sig->PeerKnown(member_id);
}
void Player::PeerDead(const std::string& who) {
    Note("peer dead " + who);
    if (bbhost) hle_np_signaling_peer_dead(who);
    else sig->PeerDead(who);
}

// ---- the cases

struct Result {
    std::vector<std::string> a, b, wire;
    std::vector<Player::Ev> a_events, b_events;
    std::vector<std::string> a_log, b_log;
};

enum class Who { Bbhost, Port };

// The room: each player knows the other as a member with its address.
void Room(World& W) {
    W.a.peers[2] = TPeer{2, W.b.name, W.b.addr, W.b.port};
    W.b.peers[1] = TPeer{1, W.a.name, W.a.addr, W.a.port};
}

struct Case {
    const char* name;
    std::function<void(World&)> setup; // before the players start
    std::function<void(World&)> run;   // drives the clock
    std::function<void(const Result&)> expect;
};

Result RunCase(const Case& c, Who wa, Who wb) {
    World W;
    g_world = &W;
    W.a.w = W.b.w = &W;
    W.a.name = "HunterA";
    W.b.name = "HunterB";
    W.a.addr = Ip(1);
    W.b.addr = Ip(2);
    W.a.bbhost = wa == Who::Bbhost;
    W.b.bbhost = wb == Who::Bbhost;
    if (c.setup) c.setup(W);
    W.a.Init();
    W.b.Init();
    c.run(W);
    Result r{W.a.trace, W.b.trace, W.wire, W.a.events, W.b.events, W.a.log, W.b.log};
    W.a.Term();
    W.b.Term();
    W.clock.q.clear();
    g_world = nullptr;
    g_bb = nullptr;
    return r;
}

bool Has(const std::vector<Player::Ev>& evs, std::uint16_t event, int error = 0) {
    for (const auto& e : evs)
        if (e.event == event && e.error == error) return true;
    return false;
}
int Count(const std::vector<Player::Ev>& evs, std::uint16_t event) {
    int n = 0;
    for (const auto& e : evs) n += e.event == event;
    return n;
}
bool Before(const std::vector<Player::Ev>& evs, std::uint16_t first, std::uint16_t then) {
    int i = -1, j = -1;
    for (int k = 0; k < static_cast<int>(evs.size()); ++k) {
        if (evs[k].event == first && i < 0) i = k;
        if (evs[k].event == then && j < 0) j = k;
    }
    return i >= 0 && j >= 0 && i < j;
}
bool Saw(const std::vector<std::string>& trace, const std::string& what) {
    for (const auto& line : trace)
        if (line.find(what) != std::string::npos) return true;
    return false;
}

void Diff(const char* what, const std::vector<std::string>& x, const std::vector<std::string>& y, const char* xn,
          const char* yn) {
    if (x == y) return;
    ++g_fail;
    std::printf("FAIL %s: %s differs (%s: %u lines, %s: %u lines)\n", g_case.c_str(), what, xn,
                static_cast<unsigned>(x.size()), yn, static_cast<unsigned>(y.size()));
    const std::size_t n = x.size() > y.size() ? x.size() : y.size();
    int shown = 0;
    for (std::size_t i = 0; i < n && shown < 12; ++i) {
        const std::string l = i < x.size() ? x[i] : "(none)", r = i < y.size() ? y[i] : "(none)";
        if (l == r) continue;
        std::printf("  %3u %-10s %s\n      %-10s %s\n", static_cast<unsigned>(i), xn, l.c_str(), yn, r.c_str());
        ++shown;
    }
}

void Dump(const char* who, const Result& r) {
    std::printf("---- %s\nA:\n", who);
    for (const auto& l : r.a) std::printf("  %s\n", l.c_str());
    std::printf("B:\n");
    for (const auto& l : r.b) std::printf("  %s\n", l.c_str());
    std::printf("wire:\n");
    for (const auto& l : r.wire) std::printf("  %s\n", l.c_str());
}

void Run(const Case& c) {
    g_case = c.name;
    const int before = g_fail;
    const Result bp = RunCase(c, Who::Bbhost, Who::Port);
    const Result pp = RunCase(c, Who::Port, Who::Port);
    const Result pb = RunCase(c, Who::Port, Who::Bbhost);
    // Our port as B beside bbhost's A, as A and B, and as A beside bbhost's B: one record.
    Diff("A's record", bp.a, pp.a, "bbhost A", "port A");
    Diff("B's record", bp.b, pp.b, "bbhost A", "port A");
    Diff("the wire", bp.wire, pp.wire, "bbhost A", "port A");
    Diff("A's record", pb.a, pp.a, "bbhost B", "port B");
    Diff("B's record", pb.b, pp.b, "bbhost B", "port B");
    Diff("the wire", pb.wire, pp.wire, "bbhost B", "port B");
    c.expect(pp);
    if (g_fail != before || std::getenv("THD_SIGNALING_TEST_VERBOSE")) {
        Dump("bbhost A, port B", bp);
        Dump("port A, port B", pp);
        Dump("port A, bbhost B", pb);
    }
    std::printf("%s %s\n", g_fail == before ? "ok  " : "FAIL", c.name);
}

std::vector<Case> Cases() {
    std::vector<Case> cs;
    cs.push_back({"A activates first, B later", Room,
                  [](World& W) {
                      W.a.Activate(W.b.name);
                      W.clock.RunUntil(3000);
                      W.b.Activate(W.a.name);
                      W.clock.RunUntil(6000);
                      W.a.Status(W.a.conn_names.begin()->first);
                      W.b.Status(W.b.conn_names.begin()->first);
                      W.a.TransportStatus(W.b.name);
                      W.b.Live(W.a.name);
                  },
                  [](const Result& r) {
                      CHECK(Has(r.a_events, kEvEstablished));
                      CHECK(Has(r.b_events, kEvPeerActivated));
                      CHECK(Has(r.b_events, kEvEstablished)); // its own activation, on the transport that is up
                      CHECK(Count(r.a_events, kEvDead) == 0 && Count(r.b_events, kEvDead) == 0);
                      CHECK(Saw(r.a, "-> 0x0: 2 0200000a:4a0e")); // status 2, B's address, port 3658 (network order)
                      CHECK(Saw(r.a, "matching2: transport up HunterB"));
                  }});
    cs.push_back({"B activates first, A later", Room,
                  [](World& W) {
                      W.b.Activate(W.a.name);
                      W.clock.RunUntil(2500);
                      W.a.Activate(W.b.name);
                      W.clock.RunUntil(5000);
                  },
                  [](const Result& r) {
                      CHECK(Has(r.b_events, kEvEstablished));
                      CHECK(Has(r.a_events, kEvPeerActivated));
                      CHECK(Has(r.a_events, kEvEstablished));
                  }});
    cs.push_back({"both activate at once", Room,
                  [](World& W) {
                      W.a.Activate(W.b.name);
                      W.b.Activate(W.a.name);
                      W.clock.RunUntil(5000);
                  },
                  [](const Result& r) {
                      CHECK(Has(r.a_events, kEvEstablished) && Has(r.b_events, kEvEstablished));
                      CHECK(Has(r.a_events, kEvMutual) && Has(r.b_events, kEvMutual));
                      CHECK(Before(r.a_events, kEvEstablished, kEvMutual));
                      CHECK(Count(r.a_events, kEvEstablished) == 1 && Count(r.b_events, kEvEstablished) == 1);
                  }});
    cs.push_back({"only the peer activates", Room,
                  [](World& W) {
                      W.a.Activate(W.b.name);
                      W.clock.RunUntil(5000);
                      W.b.Live(W.a.name);
                      W.b.TransportStatus(W.a.name);
                  },
                  [](const Result& r) {
                      CHECK(Has(r.a_events, kEvEstablished));
                      CHECK(Has(r.b_events, kEvPeerActivated));
                      CHECK(!Has(r.b_events, kEvEstablished));
                      CHECK(Saw(r.b, "transport status HunterA -> 2"));
                  }});
    cs.push_back({"a deactivate, then a second one", Room,
                  [](World& W) {
                      const unsigned conn = W.a.Activate(W.b.name);
                      W.b.Activate(W.a.name);
                      W.clock.RunUntil(4000);
                      W.a.Deactivate(conn);
                      W.clock.RunUntil(6000);
                      W.a.Deactivate(conn);
                      W.a.Live(W.b.name);
                      W.clock.RunUntil(8000);
                  },
                  [](const Result& r) {
                      CHECK(Has(r.a_events, kEvDead, kErrTerminatedByMyself));
                      CHECK(Has(r.b_events, kEvPeerDeactivated));
                      CHECK(Saw(r.a, Fmt("-> 0x%08x", static_cast<unsigned>(kErrConnNotFound))));
                      CHECK(Saw(r.a, "matching2: peer closed HunterB"));
                      CHECK(Saw(r.a, "matching2: transport dead HunterB error 0x80550e18"));
                  }});
    cs.push_back({"a peer that never answers", [](World& W) {
                      Room(W);
                      W.b.offline = true;
                  },
                  [](World& W) {
                      W.a.Activate(W.b.name);
                      W.clock.RunUntil(29000);
                      W.a.TransportStatus(W.b.name);
                      W.clock.RunUntil(35000);
                      W.a.TransportStatus(W.b.name);
                  },
                  [](const Result& r) {
                      CHECK(Has(r.a_events, kEvDead, kErrTimeout));
                      CHECK(!Has(r.a_events, kEvEstablished));
                      CHECK(Saw(r.a, "transport status HunterB -> 1"));
                      CHECK(Saw(r.a, "transport status HunterB -> 0"));
                      CHECK(Saw(r.a, "matching2: transport dead HunterB error 0x80550e11"));
                  }});
    cs.push_back({"30% of datagrams lost, the rest reordered", [](World& W) {
                      Room(W);
                      W.loss_pct = 30;
                  },
                  [](World& W) {
                      W.a.Activate(W.b.name);
                      W.clock.RunUntil(700);
                      W.b.Activate(W.a.name);
                      W.clock.RunUntil(20000);
                  },
                  [](const Result& r) {
                      CHECK(Has(r.a_events, kEvEstablished) && Has(r.b_events, kEvEstablished));
                      CHECK(Count(r.a_events, kEvDead) == 0 && Count(r.b_events, kEvDead) == 0);
                      CHECK(Saw(r.wire, " lost"));
                  }});
    cs.push_back({"the address learned late from the server", nullptr,
                  [](World& W) {
                      // B is not a room member yet (a summon): A's connection waits for the server.
                      W.b.peers[1] = TPeer{1, W.a.name, W.a.addr, W.a.port};
                      W.a.Activate(W.b.name);
                      W.clock.RunUntil(3000);
                      W.server[W.b.name] = TPeer{0, W.b.name, W.b.addr, W.b.port};
                      W.clock.RunUntil(8000);
                  },
                  [](const Result& r) {
                      CHECK(Has(r.a_events, kEvEstablished));
                      CHECK(Saw(r.a, "provisional"));
                      CHECK(Has(r.b_events, kEvPeerActivated));
                  }});
    cs.push_back({"the address from the room, after a join", nullptr,
                  [](World& W) {
                      W.b.peers[1] = TPeer{1, W.a.name, W.a.addr, W.a.port};
                      W.a.Activate(W.b.name);
                      W.clock.RunUntil(2200);
                      W.a.peers[2] = TPeer{2, W.b.name, W.b.addr, W.b.port};
                      W.a.PeerKnown(2);
                      W.clock.RunUntil(6000);
                  },
                  [](const Result& r) {
                      CHECK(Has(r.a_events, kEvEstablished));
                      CHECK(!Saw(r.a, "provisional"));
                  }});
    cs.push_back({"no address within 30 s", nullptr,
                  [](World& W) {
                      W.a.Activate(W.b.name);
                      W.clock.RunUntil(32000);
                  },
                  [](const Result& r) { CHECK(Has(r.a_events, kEvDead, kErrTimeout)); }});
    cs.push_back({"the room's members: up, a game connection on it, a member leaving", Room,
                  [](World& W) {
                      W.a.M2Connect(W.b.name);
                      W.b.M2Connect(W.a.name);
                      W.clock.RunUntil(3000);
                      W.a.TransportStatus(W.b.name);
                      const unsigned conn = W.a.Activate(W.b.name); // the transport is up: at once
                      W.clock.RunUntil(4000);
                      W.a.Status(conn);
                      W.a.M2Connect(W.b.name); // again, on the transport that is up
                      W.clock.RunUntil(6000);
                      W.a.PeerDead(W.b.name); // B left the room
                      W.clock.RunUntil(7000);
                      W.a.TransportStatus(W.b.name);
                      W.a.Live(W.b.name);
                  },
                  [](const Result& r) {
                      CHECK(Saw(r.a, "matching2: transport up HunterB"));
                      CHECK(Saw(r.b, "matching2: transport up HunterA"));
                      CHECK(Saw(r.a, "transport status HunterB -> 2"));
                      CHECK(Has(r.a_events, kEvEstablished));
                      CHECK(Has(r.a_events, kEvDead, kErrTerminatedByPeer));
                      CHECK(Saw(r.a, "live HunterB -> 0"));
                  }});
    cs.push_back({"a room member that never answers, and a stop", [](World& W) {
                      Room(W);
                      W.b.offline = true;
                  },
                  [](World& W) {
                      W.a.M2Connect(W.b.name);
                      W.clock.RunUntil(10000);
                      W.a.TransportStatus(W.b.name);
                      W.a.M2Stop(W.b.name); // no game connection on it: the transport goes
                      W.a.TransportStatus(W.b.name);
                      W.clock.RunUntil(40000);
                      W.a.M2Connect(W.b.name);
                      W.clock.RunUntil(75000);
                  },
                  [](const Result& r) {
                      CHECK(Saw(r.a, "transport status HunterB -> 1"));
                      CHECK(Saw(r.a, "transport status HunterB -> 0"));
                      CHECK(Saw(r.a, "matching2: transport dead HunterB error 0x80550e11"));
                  }});
    cs.push_back({"a deactivate before the peer's side is up", Room,
                  [](World& W) {
                      // A is up at 80 ms; B's own check round ends at 560 ms. A's Close reaches B before.
                      const unsigned conn = W.a.Activate(W.b.name);
                      W.clock.RunUntil(200);
                      W.a.Deactivate(conn);
                      W.clock.RunUntil(3000);
                      W.b.Live(W.a.name);
                      W.b.TransportStatus(W.a.name);
                  },
                  [](const Result& r) {
                      CHECK(Has(r.a_events, kEvDead, kErrTerminatedByMyself));
                      CHECK(Has(r.b_events, kEvDead, kErrTerminatedByPeer));
                      CHECK(!Has(r.b_events, kEvPeerDeactivated));
                      CHECK(Saw(r.b, "matching2: transport dead HunterA error 0x80550e10"));
                  }});
    cs.push_back({"the peer closes (terminate)", Room,
                  [](World& W) {
                      W.a.Activate(W.b.name);
                      W.b.Activate(W.a.name);
                      W.clock.RunUntil(3000);
                      // B quits: its game deactivates while A's transport is up; then A's own.
                      const unsigned bconn = W.b.conn_names.begin()->first;
                      W.b.Deactivate(bconn);
                      W.clock.RunUntil(5000);
                      W.a.Live(W.b.name);
                      W.a.PeerDead(W.b.name);
                      W.clock.RunUntil(6000);
                  },
                  [](const Result& r) {
                      CHECK(Has(r.b_events, kEvDead, kErrTerminatedByMyself));
                      CHECK(Has(r.a_events, kEvPeerDeactivated));
                      CHECK(Has(r.a_events, kEvDead, kErrTerminatedByPeer));
                  }});
    return cs;
}

} // namespace

int main() {
    hle_register_np_signaling();
    for (const Case& c : Cases()) Run(c);
    if (g_fail) {
        std::printf("thd-signaling-test: %d failed\n", g_fail);
        return 1;
    }
    std::printf("thd-signaling-test: ok\n");
    return 0;
}
