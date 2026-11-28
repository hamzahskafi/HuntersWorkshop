# Online: shadPS4's HTTP and SSL, and The Hunter's Dream backend

## shadPS4's HTTP and SSL libraries

`gpu/bbnet/vendor/shadps4/core/libraries/network/` holds shadPS4's own `http` and `ssl2` libraries,
unchanged except where marked `bbport`:
https://github.com/shadps4-emu/shadPS4/tree/v.0.20.0
(529ceb32f5546b820588f194b903cc39d08015fe), `src/core/libraries/network`. These portions are
GPL-2.0-or-later, with their original notices. `third_party/httplib/httplib.h` retains its embedded
license notice (`CPPHTTPLIB.txt`). The game's sceHttp and sceSsl imports call them; sceHttp has two
`bbport` hooks for The Hunter's Dream: `*.example.com` host-override keys and the account token and
ruleset headers.

The rest of shadPS4's network code (its sceNet, NetCtl and NP libraries and the shadNet client) was
removed in 0.4.4, with the shadNet online path: online play is The Hunter's Dream's, and
nothing called it any more. `bbnet_glue.cpp` registers what is left; `include/` holds the few
declarations these files need from the rest of shadPS4. Online play is off unless `BB_ONLINE=1`,
which only The Hunter's Dream's start sets.

## The Hunter's Dream backend (`thd/`)

`thd/` is the co-op pool for The Hunter's Dream (`online_pool=huntersdream`, which sets
`BB_NET_BACKEND=thd`). It adapts droogie/bbhost's network side
(https://github.com/droogie/bbhost/tree/566184a74f071166299bb656c4af479e5bbe2771):
`src/hle/net.cpp`, `src/hle/np_matching2.cpp`, `src/hle/np_signaling.cpp`, the NP parts of
`src/hle/system.cpp`, and `src/net/{http,account,session,stun}.cpp`. These portions are
GPL-3.0-or-later; copyright remains with the bbhost contributors. Its sceNet, NetCtl and NP
functions answer every network import the game has.

Since 0.4.6 beta 14 these parts follow bbhost v0.2.35
(https://github.com/droogie/bbhost/tree/a317321): NpSignaling's handshake between games
(`src/hle/np_signaling.cpp`, bbhost #217) ported as `thd_signaling.h/.cpp`, Matching2 on it
(`src/hle/np_matching2.cpp`), the P2P port's greetings, datagram size and pieces
(`src/hle/net.cpp`, `e74e3ce`; `thd_net.cpp`, `thd_p2p_split.h`), and the NpId compares and the
score library's sanitize call (`src/hle/system.cpp`, `src/hle/np_score.h`; `thd_np_util.h`), the
sign-in check's reading and a refused sign-in reported to the game (`src/net/sign_in_check.h`,
`2602c3c`; `thd_sign_in.h`), and the event poller's generations (`src/net/poll_pacing.h`, `2e40749`;
`thd_poll_gate.h`).
bbhost's `np_signaling.cpp` at that commit is also kept unchanged in
`tests/third_party/bbhost/` with bbhost's additional permission (`LICENSE-EXCEPTION`), built only
into the test that runs it against the port and never shipped.
