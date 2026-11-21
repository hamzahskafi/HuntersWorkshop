// SPDX-License-Identifier: GPL-3.0-or-later
// The Hunter's Dream backend: sceNpSignaling's imports (the class in thd_signaling.cpp), and the NP
// manager, auth, lookup, score and WebAPI imports answered here (The Hunter's Dream has no PSN behind
// it).
// Adapted from droogie/bbhost src/hle/system.cpp (566184a; the compares and the sanitize call from
// a317321); NpSignaling is thd_signaling.cpp, from bbhost's np_signaling.cpp at a317321.
#include "thd_internal.h"
#include "thd_np_util.h"
#include "thd_signaling.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "core/libraries/libs.h"

namespace BbThd {
namespace {

// ---- sceNpSignaling: bbhost's, with the greeting between games (thd_signaling.cpp) ------------------
// 0.4.6 beta 14: the timer that announced ESTABLISHED and ACTIVE 200 and 400 ms after a peer's
// address was known is gone (players on bbhost 0.2.31 and later wait for the greeting, and dropped
// ours after 30 s). One Signaling for the game, its outside world the backend's.

Signaling::Peer ToSignaling(const Peer& p) {
    Signaling::Peer out;
    out.member_id = p.member_id;
    out.online_id = p.online_id;
    out.addr = p.addr;
    out.port = p.port;
    return out;
}

Signaling& Sig() {
    static Signaling* const s = new Signaling([] {
        Signaling::Env e;
        e.send = [](std::uint32_t addr, std::uint16_t port, const std::uint8_t* data, std::size_t len) {
            P2pSignalSend(addr, port, data, len);
        };
        e.dispatch = [](int delay_ms, std::function<void()> fn) { DispatchAfter(delay_ms, Prio::Signaling, std::move(fn)); };
        e.call_game = [](void* cb, unsigned ctx, unsigned conn, std::uint16_t event, int error, void* arg) {
            CallGuest(cb, static_cast<std::int64_t>(ctx), static_cast<std::int64_t>(conn), static_cast<std::int64_t>(event),
                      static_cast<std::int64_t>(error), arg);
        };
        e.peer_get = [](std::uint16_t member_id, Signaling::Peer* out) {
            Peer p;
            if (!PeersGet(member_id, &p)) return false;
            *out = ToSignaling(p);
            return true;
        };
        e.peer_find = [](const std::string& online_id, Signaling::Peer* out) {
            Peer p;
            if (!PeersFindOnline(online_id, &p)) return false;
            *out = ToSignaling(p);
            return true;
        };
        e.peer_set_conn = [](std::uint16_t member_id, unsigned conn, int status) { PeersSetConn(member_id, conn, status); };
        e.peer_provisional = [](const std::string& online_id, std::uint32_t addr, std::uint16_t port) {
            return ToSignaling(PeersProvisional(online_id, addr, port));
        };
        e.resolve = [](const std::string& online_id, std::uint32_t* addr, std::uint16_t* port, std::string& error) {
            return ServerSignalingResolve(online_id, addr, port, error);
        };
        e.online_id = [] { return OnlineId(); };
        e.m2_up = [](const std::string& online_id) { Matching2TransportUp(online_id); };
        e.m2_dead = [](const std::string& online_id, int error) { Matching2TransportDead(online_id, error); };
        e.m2_closed = [](const std::string& online_id) { Matching2PeerClosed(online_id); };
        e.log = [](const char* line) { Log("%s", line); };
        e.now_ms = [] { return NowMs(); };
        e.relayed = [](std::uint32_t addr, std::uint16_t port) { return P2pRelayed(addr, port); };
        e.trace = Trace();
        return e;
    }());
    return *s;
}

s32 PS4_SYSV_ABI sig_init(u64, int, int, u64) { return Sig().Initialize(); }
s32 PS4_SYSV_ABI sig_term() { return Sig().Terminate(); }
// (const SceNpId*, callback, arg, ctxId*)
s32 PS4_SYSV_ABI sig_create_ctx(const void*, void* cb, void* arg, unsigned* ctx) { return Sig().CreateContext(cb, arg, ctx); }
s32 PS4_SYSV_ABI sig_delete_ctx(unsigned ctx) { return Sig().DeleteContext(ctx); }
// (ctxId, const SceNpId* peer, connId*)
s32 PS4_SYSV_ABI sig_activate(unsigned ctx, const void* npid, unsigned* conn_out) {
    return Sig().Activate(ctx, NpIdText(npid), conn_out);
}
s32 PS4_SYSV_ABI sig_deactivate(unsigned ctx, unsigned conn) { return Sig().Deactivate(ctx, conn); }
// (ctxId, connId, status*, SceNetInAddr*, SceInPort_t*)
s32 PS4_SYSV_ABI sig_status(unsigned ctx, unsigned conn, int* st, std::uint32_t* addr, std::uint16_t* port) {
    return Sig().Status(ctx, conn, st, addr, port);
}

// ---- NP manager: signed in as the account, no PSN behind it -----------------------------------------

constexpr int kNpErrInvalidArg = static_cast<int>(0x80550003);
std::atomic<int> g_np_req{1};

s32 PS4_SYSV_ABI np_ok() { return 0; }
s32 PS4_SYSV_ABI np_register_cb(void*, void*) { return 0; }
s32 PS4_SYSV_ABI np_get_state(int, int* state) {
    if (!state) return kNpErrInvalidArg;
    // Signed out without an account: the game plays offline rather than under a made-up name. And
    // signed out while the server refuses the account's sign-in (0.4.6 beta 14, bbhost 2602c3c): the
    // game then shows its own "not signed in" and offline dialogs instead of going online to be refused
    // all session.
    *state = SignedIn() && !Refused() ? 2 : 1;
    return 0;
}
s32 PS4_SYSV_ABI np_online_id(int, void* id) {
    if (!id) return kNpErrInvalidArg;
    const std::string cur = OnlineId();
    std::memset(id, 0, 20);
    std::memcpy(id, cur.c_str(), std::min<std::size_t>(cur.size(), 16));
    return 0;
}
// The same handle as the online id: the session service matches peers by it.
s32 PS4_SYSV_ABI np_get_npid(int, void* id) {
    if (!id) return kNpErrInvalidArg;
    FillNpId(id, OnlineId().c_str());
    return 0;
}
s32 PS4_SYSV_ABI np_presence_status(int, int* st) {
    if (st) *st = 1;
    return 0;
}
// 0.4.6 beta 14: by the name, then opt and reserved (thd_np_util.h, bbhost #217), not memcmp.
s32 PS4_SYSV_ABI np_cmp_online(const void* a, const void* b) { return NpUtil::CompareOnlineId(a, b); }
s32 PS4_SYSV_ABI np_cmp_npid(const void* a, const void* b) { return NpUtil::CompareNpId(a, b); }
s32 PS4_SYSV_ABI np_create_request(const void*) { return g_np_req.fetch_add(1); }
s32 PS4_SYSV_ABI np_request_done(int) { return 0; }
s32 PS4_SYSV_ABI np_poll_async(int, int* result) {
    if (result) *result = 0;
    return 0; // finished
}
s32 PS4_SYSV_ABI np_check_availability(int, const void*, void*) { return 0; }
s32 PS4_SYSV_ABI np_parental(int, const void*, std::int8_t* age, std::uint8_t* info) {
    if (age) *age = 25;
    if (info) std::memset(info, 0, 3);
    return 0;
}
s32 PS4_SYSV_ABI np_check_plus(int, const void*, std::uint8_t* result) {
    if (result) result[0] = 1;
    return 0;
}
s32 PS4_SYSV_ABI np_plus_notify(int, u64) { return 0; }
// The game server binds the session to the account by X-BB-Token, not by this code.
s32 PS4_SYSV_ABI npauth_get_code(int, const void*, char* code, int* issuer) {
    if (code) {
        std::memset(code, 0, 128);
        std::strncpy(code, "DUMMY", 127);
    }
    if (issuer) *issuer = 10;
    return 0;
}

// ---- lookup ---------------------------------------------------------------------------------------------

std::atomic<int> g_lookup_id{1};
s32 PS4_SYSV_ABI lookup_title_ctx(const void*) { return g_lookup_id.fetch_add(1); }
s32 PS4_SYSV_ABI lookup_async(int, const void*) { return g_lookup_id.fetch_add(1); }
s32 PS4_SYSV_ABI lookup_npid(int, const void* online, void* npid, void*) {
    if (!online || !npid) return kNpErrInvalidArg;
    char name[17]{};
    std::memcpy(name, online, 16);
    FillNpId(npid, name);
    return 0;
}

// ---- score: no leaderboard server ------------------------------------------------------------------

constexpr int kScoreErrOffline = static_cast<int>(0x80550605u);
std::atomic<int> g_score_id{1};
s32 PS4_SYSV_ABI score_title_ctx(const void*, int) { return g_score_id.fetch_add(1); }
s32 PS4_SYSV_ABI score_create_request(int) { return g_score_id.fetch_add(1); }
s32 PS4_SYSV_ABI score_set_pc_id(int, int) { return 0; }
s32 PS4_SYSV_ABI score_offline() { return kScoreErrOffline; }
// sceNpScoreCensorComment(reqId, comment, option): nothing to censor (the server has no word filter).
s32 PS4_SYSV_ABI score_censor(int, const char* comment, void*) { return comment ? 0 : NpUtil::kScoreErrInvalidArg; }
// sceNpScoreSanitizeComment(reqId, comment, sanitized, option): the text back, so the character's name
// reaches the other players (0.4.6 beta 14, bbhost f990415; it answered 0 with the buffer empty).
s32 PS4_SYSV_ABI score_sanitize(int, const char* comment, char* sanitized, void*) {
    static std::atomic<bool> logged{false};
    if (!logged.exchange(true)) Log("np: sceNpScoreSanitizeComment gives the text back as it came (the server has no word filter)");
    return NpUtil::SanitizeCopy(comment, sanitized);
}

// ---- WebAPI: answered here, never sent anywhere -------------------------------------------------------

constexpr int kWebApiInvalid = static_cast<int>(0x80552902);
constexpr int kWebApiNoReq = static_cast<int>(0x80552906);
constexpr char kWebApiJsonType[] = "application/json; charset=utf-8";

struct WebApiReq {
    int status = 200;
    std::string body;
    std::size_t read_off = 0;
};
std::mutex g_web_mu;
std::atomic<int> g_web_id{1};
std::int64_t g_web_req = 1;
std::unordered_map<std::int64_t, WebApiReq> g_web_reqs;

std::string webapi_body_for_path(const char* path) {
    if (path && std::strstr(path, "friendList")) return "{\"totalResults\":0,\"friendList\":[]}";
    if (path && std::strstr(path, "blockList")) return "{\"totalResults\":0,\"blockList\":[]}";
    return "{\"totalResults\":0}";
}
s32 PS4_SYSV_ABI webapi_init(int, u64) { return g_web_id.fetch_add(1); }
s32 PS4_SYSV_ABI webapi_create_ctx(int, const void*) { return g_web_id.fetch_add(1); }
s32 PS4_SYSV_ABI webapi_create_filter(int, const void*, u64) { return g_web_id.fetch_add(1); }
s32 PS4_SYSV_ABI webapi_reg_push(int, int, void*, void*) { return g_web_id.fetch_add(1); }
s32 PS4_SYSV_ABI webapi_create_req(int, const char*, const char* path, int, const void*, std::int64_t* req_id) {
    if (!path || !req_id) return kWebApiInvalid;
    std::lock_guard lk(g_web_mu);
    const std::int64_t id = g_web_req++;
    g_web_reqs[id] = WebApiReq{200, webapi_body_for_path(path), 0};
    *req_id = id;
    return 0;
}
s32 PS4_SYSV_ABI webapi_delete_req(std::int64_t req) {
    std::lock_guard lk(g_web_mu);
    g_web_reqs.erase(req);
    return 0;
}
s32 PS4_SYSV_ABI webapi_send(std::int64_t req, const void*, u64) {
    std::lock_guard lk(g_web_mu);
    return g_web_reqs.count(req) ? 0 : kWebApiNoReq;
}
s32 PS4_SYSV_ABI webapi_status(std::int64_t req, int* code) {
    if (!code) return kWebApiInvalid;
    std::lock_guard lk(g_web_mu);
    const auto it = g_web_reqs.find(req);
    if (it == g_web_reqs.end()) return kWebApiNoReq;
    *code = it->second.status;
    return 0;
}
s32 PS4_SYSV_ABI webapi_read(std::int64_t req, void* data, u64 size) {
    std::lock_guard lk(g_web_mu);
    const auto it = g_web_reqs.find(req);
    if (it == g_web_reqs.end()) return kWebApiNoReq;
    WebApiReq& r = it->second;
    if (!data || size == 0 || r.read_off >= r.body.size()) return 0;
    const std::size_t n = std::min<std::size_t>(static_cast<std::size_t>(size), r.body.size() - r.read_off);
    std::memcpy(data, r.body.data() + r.read_off, n);
    r.read_off += n;
    return static_cast<int>(n);
}
bool is_content_type(const char* name) {
    return std::strcmp(name, "Content-Type") == 0 || std::strcmp(name, "content-type") == 0;
}
s32 PS4_SYSV_ABI webapi_hdr_len(std::int64_t req, const char* name, u64* len) {
    if (!name || !len) return kWebApiInvalid;
    std::lock_guard lk(g_web_mu);
    if (!g_web_reqs.count(req)) return kWebApiNoReq;
    if (!is_content_type(name)) return kWebApiInvalid;
    *len = sizeof(kWebApiJsonType);
    return 0;
}
s32 PS4_SYSV_ABI webapi_hdr_val(std::int64_t req, const char* name, char* buf, u64 n) {
    if (!name || !buf || n == 0) return kWebApiInvalid;
    std::lock_guard lk(g_web_mu);
    if (!g_web_reqs.count(req)) return kWebApiNoReq;
    if (!is_content_type(name)) return kWebApiInvalid;
    std::snprintf(buf, static_cast<std::size_t>(n), "%s", kWebApiJsonType);
    return 0;
}
s32 PS4_SYSV_ABI webapi_parse_npid(const char* json, void* npid) {
    if (!json || !npid) return kWebApiInvalid;
    const char* s = json;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') ++s;
    char online[17]{};
    auto take_quoted = [&](const char* p) {
        const char* q = std::strchr(p, '"');
        if (!q) return false;
        ++q;
        const char* e = std::strchr(q, '"');
        if (!e) return false;
        const std::size_t n = static_cast<std::size_t>(e - q);
        if (n == 0 || n >= sizeof(online)) return false;
        std::memcpy(online, q, n);
        return true;
    };
    if (*s == '{') {
        const char* key = std::strstr(s, "onlineId");
        if (!key) key = std::strstr(s, "\"data\"");
        // The value after the key's colon.
        const char* colon = key ? std::strchr(key, ':') : nullptr;
        if (!colon || !take_quoted(colon)) return kWebApiInvalid;
    } else if (*s == '"') {
        if (!take_quoted(s)) return kWebApiInvalid;
    } else {
        std::snprintf(online, sizeof(online), "%s", s);
    }
    FillNpId(npid, online);
    return 0;
}

} // namespace

// 0.4.6 beta 14 part 9: what the links did since the last line, in one line. A timeout says whether
// the peer never answered (an older client, or no route to it) or answered without finishing (the
// network between the two), the rest what the tester's runs and a player's report need first.
void OnlineSessionReport(const char* when) {
    const Signaling::Stats st = Sig().TakeStats();
    const P2pSession p2p = P2pTakeSession();
    if (!st.Any() && !p2p.split && !p2p.largest_out && !p2p.largest_in) return;
    std::string links;
    char part[192];
    const auto add = [&links](const char* text) {
        if (!links.empty()) links += ", ";
        links += text;
    };
    if (st.up) {
        if (st.handshake_min_ms == st.handshake_max_ms) {
            std::snprintf(part, sizeof(part), "%d up (handshake %d ms)", st.up, st.handshake_min_ms);
        } else {
            std::snprintf(part, sizeof(part), "%d up (handshake %d-%d ms)", st.up, st.handshake_min_ms,
                          st.handshake_max_ms);
        }
        add(part);
    }
    if (st.timed_out_silent) {
        std::snprintf(part, sizeof(part),
                      "%d timed out after 30 s (the peer never answered: an older client, or no route)",
                      st.timed_out_silent);
        add(part);
    }
    if (st.timed_out_partial) {
        std::snprintf(part, sizeof(part),
                      "%d timed out after 30 s (the peer answered but the handshake did not finish: the network)",
                      st.timed_out_partial);
        add(part);
    }
    if (st.no_address) {
        std::snprintf(part, sizeof(part), "%d with no address in 30 s (the server did not know the player)",
                      st.no_address);
        add(part);
    }
    if (st.closed_by_peer) {
        std::snprintf(part, sizeof(part), "%d closed by the peer", st.closed_by_peer);
        add(part);
    }
    if (st.closed_by_us) {
        std::snprintf(part, sizeof(part), "%d by us", st.closed_by_us);
        add(part);
    }
    if (links.empty()) links = "none";
    Log("Online: links this session (%s): %s; through the relay %d; datagrams sent as pieces %llu, largest %u bytes "
        "out, %u in",
        when, links.c_str(), st.relayed, static_cast<unsigned long long>(p2p.split),
        static_cast<unsigned>(p2p.largest_out), static_cast<unsigned>(p2p.largest_in));
}

void SignalingPeerKnown(std::uint16_t member_id) { Sig().PeerKnown(member_id); }
void SignalingPeerDead(const std::string& online_id) { Sig().PeerDead(online_id); }
void SignalingControl(const std::uint8_t* data, std::size_t len, std::uint32_t addr, std::uint16_t port_nbo) {
    Sig().Control(data, len, addr, port_nbo);
}
bool SignalingLive(const std::string& online_id) { return Sig().Live(online_id); }
void SignalingM2Connect(const std::string& online_id) { Sig().M2Connect(online_id); }
void SignalingM2Stop(const std::string& online_id) { Sig().M2Stop(online_id); }
int SignalingTransportStatus(const std::string& online_id) { return Sig().TransportStatus(online_id); }

void RegisterNp(Core::Loader::SymbolsResolver* sym) {
#define SIG(nid, fn) LIB_FUNCTION(nid, "libSceNpSignaling", 1, "libSceNpSignaling", fn)
    SIG("3KOuC4RmZZU", sig_init);
    SIG("NPhw0UXaNrk", sig_term);
    SIG("5yYjEdd4t8Y", sig_create_ctx);
    SIG("hx+LIg-1koI", sig_delete_ctx);
    SIG("0UvTFeomAUM", sig_activate);
    SIG("6UEembipgrM", sig_deactivate);
    SIG("bD-JizUb3JM", sig_status);
#undef SIG
#define NP(nid, fn) LIB_FUNCTION(nid, "libSceNpManager", 1, "libSceNpManager", fn)
    NP("VfRSmPmj8Q8", np_register_cb);        // sceNpRegisterStateCallback
    NP("mjjTXh+NHWY", np_ok);                 // sceNpUnregisterStateCallback
    NP("uFJpaKNBAj4", np_register_cb);        // sceNpRegisterGamePresenceCallback
    NP("GImICnh+boA", np_register_cb);        // sceNpRegisterPlusEventCallback
    NP("xViqJdDgKl0", np_ok);                 // sceNpUnregisterPlusEventCallback
    NP("3Zl8BePTh9Y", np_ok);                 // sceNpCheckCallback
    NP("Ec63y59l9tw", np_ok);                 // sceNpSetNpTitleId
    NP("A2CQ3kgSopQ", np_ok);                 // sceNpSetContentRestriction
    NP("Gaxrp3EWY-M", np_plus_notify);        // sceNpNotifyPlusFeature
    NP("eQH7nWPcAgc", np_get_state);          // sceNpGetState
    NP("XDncXQIJUSk", np_online_id);          // sceNpGetOnlineId
    NP("p-o74CnoNzY", np_get_npid);           // sceNpGetNpId
    NP("IPb1hd1wAGc", np_presence_status);    // sceNpGetGamePresenceStatus
    NP("eiqMCt9UshI", np_create_request);     // sceNpCreateAsyncRequest
    NP("S7QTn72PrDw", np_request_done);       // sceNpDeleteRequest
    NP("OzKvTvg3ZYU", np_request_done);       // sceNpAbortRequest
    NP("uqcPJLWL08M", np_poll_async);         // sceNpPollAsync
    NP("2rsFmlGWleQ", np_check_availability); // sceNpCheckNpAvailability
    NP("ilwLM4zOmu4", np_parental);           // sceNpGetParentalControlInfo
    NP("r6MyYJkryz8", np_check_plus);         // sceNpCheckPlus
#undef NP
    LIB_FUNCTION("i8UmXTSq7N4", "libSceNpCommon", 1, "libSceNpCommon", np_cmp_npid);
    LIB_FUNCTION("dj+O5aD2a0Q", "libSceNpCommon", 1, "libSceNpCommon", np_cmp_online);
#define AUTH(nid, fn) LIB_FUNCTION(nid, "libSceNpAuth", 1, "libSceNpAuth", fn)
    AUTH("N+mr7GjTvr8", np_create_request);
    AUTH("H8wG9Bk-nPc", np_request_done);
    AUTH("gjSyfzSsDcE", np_poll_async);
    AUTH("KxGkOrQJTqY", npauth_get_code);
#undef AUTH
#define LOOKUP(nid, fn) LIB_FUNCTION(nid, "libSceNpManager", 1, "libSceNpManager", fn)
    LOOKUP("8533Q+LU7EQ", lookup_title_ctx);
    LOOKUP("mtqDK9zkoIE", np_request_done);
    LOOKUP("JA4+sS39GMs", lookup_async);
    LOOKUP("wLaxchvEEnk", np_request_done);
    LOOKUP("eYz4v5Uek9U", np_request_done);
    LOOKUP("V4EVrruHuy8", np_poll_async);
    LOOKUP("T6tnM1Uti4g", lookup_npid);
#undef LOOKUP
#define SCORE(nid, fn) LIB_FUNCTION(nid, "libSceNpScore", 1, "libSceNpScore", fn)
    SCORE("KnNA1TEgtBI", score_title_ctx);
    SCORE("G0pE+RNCwfk", np_request_done);
    SCORE("gW8qyjYrUbk", score_create_request);
    SCORE("dK8-SgYf6r4", np_request_done);
    SCORE("1i7kmKbX6hk", np_request_done);
    SCORE("bygbKdHmjn4", score_set_pc_id);
    SCORE("ETS-uM-vH9Q", score_offline); // GetRankingByNpIdPcId
    SCORE("KBHxDjyk-jA", score_offline); // GetRankingByRange
    SCORE("zKoVok6FFEI", score_offline); // GetGameData
    SCORE("zT0XBtgtOSI", score_offline); // RecordScore
    SCORE("LoVMVrijVOk", score_offline); // GetBoardInfo
    SCORE("bcoVwcBjQ9E", score_offline); // RecordGameData
    SCORE("2b3TI0mDYiI", score_censor);   // sceNpScoreCensorComment
    SCORE("r4oAo9in0TA", score_sanitize); // sceNpScoreSanitizeComment
#undef SCORE
#define WEB(nid, fn) LIB_FUNCTION(nid, "libSceNpWebApi", 1, "libSceNpWebApi", fn)
    WEB("G3AnLNdRBjE", webapi_init);
    WEB("asz3TtIqGF8", np_request_done);      // Terminate
    WEB("x1Y7yiYSk7c", webapi_create_ctx);
    WEB("XUjdsSTTZ3U", np_request_done);      // DeleteContext
    WEB("y5Ta5JCzQHY", webapi_create_filter);
    WEB("zE+R6Rcx3W0", np_ok);                // DeletePushEventFilter
    WEB("PfSTDCgNMgc", webapi_reg_push);
    WEB("qK4o2656W4w", np_ok);                // UnregisterPushEventCallback
    WEB("rdgs5Z1MyFw", webapi_create_req);
    WEB("noQgleu+KLE", webapi_delete_req);
    WEB("JzhYTP2fG18", webapi_delete_req);    // AbortRequest
    WEB("kVbL4hL3K7w", webapi_send);
    WEB("k210oKgP80Y", webapi_status);
    WEB("CQtPRSF6Ds8", webapi_read);
    WEB("743ZzEBzlV8", webapi_hdr_len);
    WEB("VwJ5L0Higg0", webapi_hdr_val);
    WEB("or0e885BlXo", webapi_parse_npid);
#undef WEB
}

} // namespace BbThd
