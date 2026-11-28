// SPDX-License-Identifier: GPL-3.0-or-later
// 0.4.6 beta 14 part 6: the reading of the account service's answer to POST /auth/whoami
// (gpu/bbnet/thd/thd_sign_in.h), with droogie/bbhost's tests/sign_in_check_test.cpp cases (a317321):
// a token the service knows, one it does not (its refusal as HTTP 200 with ResKind 1, or as the NP
// surfaces' 401), and everything that is no verdict on the token (no answer, a busy server, a server
// without the account service, a body that is not the service's). Then the game's own replies: only a
// 401 in the service's words marks the sign-in refused.
#include "gpu/bbnet/thd/thd_sign_in.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace BbThd::SignIn;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        ++g_fail;
        std::printf("FAIL: %s\n", what);
    }
}

Reply read(long status, const std::string& body, const std::string& error = "") { return Read(status, body, error); }

void valid() {
    const Reply r = read(200, R"({"ResKind":0,"OnlineId":"Hunter_01","Kind":"game","ExpiresAt":1797292800.25,)"
                              R"("Verified":true,"Providers":["discord"],"ProfilePublic":true,"ShowDiscord":false,)"
                              R"("ModdedSummons":true,"NatTest":null})");
    check(r.verdict == Verdict::Valid, "a known token is valid");
    check(r.name == "Hunter_01", "the account's name as the server spells it");
    check(std::fabs(r.expires_at - 1797292800.25) < 1e-6, "its expiry");
    check(r.status == 200, "the status is kept");
    const Reply n = read(200, R"({"ResKind":0,"OnlineId":"Hunter"})");
    check(n.verdict == Verdict::Valid && n.expires_at == 0, "valid without ExpiresAt: the expiry unknown");
}

void refused() {
    const Reply r = read(200, R"({"ResKind":1,"Message":"no such token"})");
    check(r.verdict == Verdict::Refused, "the service's refusal comes as HTTP 200 with ResKind 1");
    check(r.why == "no such token", "with its Message");
    check(r.name.empty(), "and no name");
    const Reply m = read(401, R"({"ResKind":1,"Message":"bad or expired token"})");
    check(m.verdict == Verdict::Refused && m.why == "bad or expired token", "the NP surfaces' 401 is a refusal");
    const Reply b = read(401, R"({"ResKind":1,"Message":"this account is banned from the server"})");
    check(b.verdict == Verdict::Refused, "a ban is a refusal");
    const Reply q = read(200, R"({"ResKind":2})");
    check(q.verdict == Verdict::Refused && q.why == "refused", "a refusal without a Message");
}

void unchecked() {
    const Reply none = read(0, "", "Couldn't connect to server");
    check(none.verdict == Verdict::Unchecked, "no connection: no verdict");
    check(none.status == 0 && none.why == "Couldn't connect to server", "the transport's error, status 0");
    const Reply late = read(0, "", "Timeout was reached");
    check(late.verdict == Verdict::Unchecked && late.status == 0, "a timeout: no verdict");
    const Reply half = read(200, R"({"ResKind":1,"Message":"no such token"})", "Recv failure");
    check(half.verdict == Verdict::Unchecked && half.status == 0, "a transport error wins over a body");
    const Reply old = read(404, R"({"detail":"Not Found"})");
    check(old.verdict == Verdict::Unchecked, "a server without /auth/whoami (404): no verdict");
    check(old.status == 404 && old.why.find("ResKind") != std::string::npos, "said as an answer without ResKind");
    const Reply html = read(502, "<html><body>Bad Gateway</body></html>");
    check(html.verdict == Verdict::Unchecked && html.status == 502, "a proxy's page: no verdict");
    const Reply busy = read(429, R"({"ResKind":1,"Message":"too many requests"})");
    check(busy.verdict == Verdict::Unchecked, "429 in the service's words: not a refusal of the token");
    const Reply down = read(503, R"({"ResKind":1,"Message":"try again"})");
    check(down.verdict == Verdict::Unchecked, "503 in the service's words: not a refusal");
    const Reply gone = read(404, R"({"ResKind":1,"Message":"no such token"})");
    check(gone.verdict == Verdict::Unchecked, "a 404 is never a verdict on the token");
    check(read(200, "not json").verdict == Verdict::Unchecked, "a body that is not JSON");
    check(read(200, "").verdict == Verdict::Unchecked, "an empty body");
    check(read(200, "[0]").verdict == Verdict::Unchecked, "JSON that is not an object");
    check(read(200, R"({"ResKind":"0","OnlineId":"Hunter"})").verdict == Verdict::Unchecked, "ResKind not a number");
    check(read(200, R"({"ResKind":0})").verdict == Verdict::Unchecked, "ResKind 0 without OnlineId");
    check(read(200, R"({"ResKind":0,"OnlineId":""})").verdict == Verdict::Unchecked, "ResKind 0 with an empty OnlineId");
    check(read(200, R"({"ResKind":0,"OnlineId":7})").verdict == Verdict::Unchecked, "OnlineId not a string");
    check(read(500, R"({"ResKind":0,"OnlineId":"Hunter"})").verdict == Verdict::Unchecked, "ResKind 0 on a 500");
}

void game_replies() {
    std::string why;
    check(GameReplyRefuses(401, R"({"ResKind":1,"Message":"bad or expired token"})", &why) && why == "bad or expired token",
          "the game's request refused in the service's words: marked, with its Message");
    check(GameReplyRefuses(401, R"({"ResKind":1})", &why) && why == "HTTP 401", "without a Message: HTTP 401");
    check(!GameReplyRefuses(401, "<html>Unauthorized</html>", &why), "a 401 not in the service's words: not marked");
    check(!GameReplyRefuses(401, R"({"detail":"nope"})", &why), "a 401 without ResKind: not marked");
    check(!GameReplyRefuses(200, R"({"ResKind":1,"Message":"no such token"})", &why), "only a 401 marks it");
    check(!GameReplyRefuses(403, R"({"ResKind":1,"Message":"forbidden"})", &why), "a 403: not marked");
    check(!GameReplyRefuses(401, "", nullptr), "an empty 401: not marked");
}

} // namespace

int main() {
    valid();
    refused();
    unchecked();
    game_replies();
    if (g_fail) {
        std::printf("thd-sign-in-test: %d failed\n", g_fail);
        return 1;
    }
    std::printf("thd-sign-in-test: ok\n");
    return 0;
}
