// SPDX-License-Identifier: GPL-3.0-or-later
// The Hunter's Dream backend, 0.4.6 beta 14 part 6: what the server says of this PC's sign-in, read
// from its answer to POST /auth/whoami {Token} (the status, the body and the transport's error), as
// droogie/bbhost reads it (src/net/sign_in_check.h, read_sign_in_reply, a317321). Header-only and
// free of engine headers, so tests/test_thd_sign_in.cpp holds it to bbhost's sign_in_check_test.
//
// The account service answers 200 in both cases: {ResKind 0, OnlineId, ExpiresAt, ...} for a token
// it knows, {ResKind 1, Message "no such token"} for one it does not: expired, or revoked when this
// PC was signed out elsewhere. Its NP surfaces refuse with a 401 in the same words. Anything else is
// no verdict: no answer, a 5xx or 429, a server without the account service (its framework's 404
// without ResKind), a body that is not JSON. (bbhost also reads the account's connection test and
// relay switch here, for its setup window; Hunter's Workshop shows neither.)
#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace BbThd::SignIn {

enum class Verdict {
    Valid,     // the server knows the token
    Refused,   // it does not: a new sign-in is needed
    Unchecked, // no verdict; `why` says what came back instead
};

struct Reply {
    Verdict verdict = Verdict::Unchecked;
    std::string name;      // Valid: the account's name, as the server spells it
    double expires_at = 0; // Valid: when the token expires (Unix seconds); 0 when not said
    std::string why;       // Refused: the server's Message; Unchecked: what came instead
    long status = 0;       // the HTTP status, 0 when no answer came
};

inline Reply Read(long status, const std::string& body, const std::string& transport_error) {
    using Json = nlohmann::json;
    Reply r;
    r.status = transport_error.empty() ? status : 0;
    if (!transport_error.empty() || status <= 0) {
        r.why = transport_error.empty() ? std::string("no answer") : transport_error;
        return r;
    }
    const Json v = Json::parse(body, nullptr, false);
    const auto field = [&v](const char* key) -> const Json* {
        if (!v.is_object()) return nullptr;
        const auto it = v.find(key);
        return it == v.end() ? nullptr : &*it;
    };
    const Json* kind = field("ResKind");
    if (!kind || !kind->is_number()) {
        // A server without the account service: its framework's own "not found", or a proxy's page.
        r.why = "HTTP " + std::to_string(status) + " without ResKind";
        return r;
    }
    if (kind->get<double>() != 0) {
        // The account service's refusal. A busy server (429, 5xx) or one that names no reason this
        // way is not a verdict on the token.
        if (status != 200 && status != 401) {
            r.why = "HTTP " + std::to_string(status);
            return r;
        }
        const Json* msg = field("Message");
        r.verdict = Verdict::Refused;
        r.why = msg && msg->is_string() && !msg->get<std::string>().empty() ? msg->get<std::string>() : "refused";
        return r;
    }
    const Json* id = field("OnlineId");
    if (status < 200 || status >= 300 || !id || !id->is_string() || id->get<std::string>().empty()) {
        r.why = "HTTP " + std::to_string(status) + " without OnlineId";
        return r;
    }
    r.verdict = Verdict::Valid;
    r.name = id->get<std::string>();
    if (const Json* e = field("ExpiresAt"); e && e->is_number()) r.expires_at = e->get<double>();
    return r;
}

// The game's own request answered `status` with `body`: the server refusing this PC's sign-in when
// it is a 401 in the account service's words ({ResKind, Message}); its reason then in `why`
// (bbhost's hle/http.cpp, 2602c3c).
inline bool GameReplyRefuses(long status, const std::string& body, std::string* why) {
    using Json = nlohmann::json;
    if (status != 401) return false;
    const Json v = Json::parse(body, nullptr, false);
    if (!v.is_object() || !v.contains("ResKind")) return false;
    const auto msg = v.find("Message");
    if (why) {
        *why = msg != v.end() && msg->is_string() && !msg->get<std::string>().empty() ? msg->get<std::string>()
                                                                                      : "HTTP 401";
    }
    return true;
}

} // namespace BbThd::SignIn
