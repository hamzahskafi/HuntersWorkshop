# 0.4.6 beta 14: The Hunter's Dream's 0.2.31 update

The design, written 2026-11-15 for the tester's review before anything is built. The memory work
that was beta 14 is beta 15 now ([memory-model.md](memory-model.md)).

**The source** is bbhost, the client The Hunter's Dream's own team writes, read at its newest release
(v0.2.35, `a317321`, 2026-11-14). It is compared with Hunter's Workshop's online backend,
`gpu/bbnet/thd/`: bbhost `566184a` (2026-10-11) with HW's own fixes, unchanged since 0.4.4. Lantern
0.9.42 is not the source: it copied bbhost's code as it stood, and The Hunter's Dream's team gave
the server's changes to bbhost alone.

**The tester's review (2026-11-16):** parts 1-5 agreed; parts 6-9 to be discussed; no 0.4.5 hotfix;
no sign-in display in the launcher (it would delay opening the game, and the game says it itself
once part 6 reports the sign-in refused). Part 6 below is revised to that.

**The goal:** HW players co-op, invade and duel with players on bbhost 0.2.31 or later, on Lantern
0.9.42 or later, and on this HW, as reliably as bbhost players do with each other. Nothing else in
online play changes for them. The game itself is untouched: its own code runs as before.

## 1. What changed

**The announcement** (The Hunter's Dream's Discord, ancat, 2026-11-10):
- With bbhost 0.2.31 the server and bbhost's network side changed.
- Co-op and PvP matchmaking need 0.2.31 or later: "previous versions will no longer be compatible".
- Ghosts, messages and bloodstains keep working on older versions.

**bbhost's changes for it, read from its code:**

- **The games greet each other** (bbhost #217, `3f9c4c0`, in 0.2.31).
  - **Before:** a game counted a link to another player as up on a timer, once the server had
    given it that player's address.
  - **Now:** two games run a handshake straight between them, on the game's own P2P port, before
    the link counts.
  - Its datagrams are 46 bytes starting `fe 'b' 'b' 's'`: the kind, a reason, both players' online
    ids (16 bytes each), a nonce.
  - The messages: control ActivationRequest, ActivationAck, Established and Close; the handshake
    Offer, Accept, Check and CheckAck.
  - Resent every 500 ms. A link not up within 30 s is dead: the summon timeout players see.
  - Through the server's relay, like the game's own traffic, when the relay carries a player.
  - Room links (Matching2's 0x5102) go up 1 s after a member's handshake completes, and go down
    with it.
  - Connection numbers start at a random point in each game, so two games no longer hand out the
    same ones.
- **The NpId compares** (`sceNpCmpNpId`, `sceNpCmpOnlineId`) compare the name, then the two 8-byte
  fields after it, as the console does. Comparing all 36 bytes made two copies of one player's id
  differ.
- **Character names** (`f990415`, 0.2.31): `sceNpScoreSanitizeComment` gives the text back. A
  player's character name is made by their own game, so others see it once that player updates.
- **Packets:**
  - #217 lifted the 2 KB limit on P2P datagrams; the game batches up to about 5 KB, more on a warp.
  - v0.2.35 (`e74e3ce`) sends those over 1,200 bytes as pieces of whole chunks again, as 0.2.26 did.
    Unsplit, they cross the internet as IP fragments, and NATs that drop fragments lost them whole.

**What did not change: every request to the server.** bbhost v0.2.35 and HW:
- call the same endpoints: `/np/events/poll` and `/ack`, `/np/signaling/resolve`, the
  `/mp/matching2/` calls (`context_start`, `create_room`, `join_room`, `leave_room`, `heartbeat`,
  `kick_member`, `signaling_update`) and the `/auth/` account calls, with the same fields;
- handle the same six server events;
- speak the same relay (STUN BBHOST-HELLO and BBHOST-RELAY, `bbr1`).

The server's own changes are in no client's code; what a client must do for them is the list above.
bbhost's NpSignaling has not changed since 0.2.31: there is no later fix to it to take.

**Newer server features (bbhost 0.2.32 to 0.2.35), not needed to connect:**
- the connection test: STUN from the P2P port to the server's test ports 3479 and 3480, then
  `/auth/nat_test`, kept for the account;
- the account's relay switch (`/auth/profile`, `RelayDirect`);
- the sign-in check (`/auth/whoami`), taken in part 6.

## 2. Why HW breaks

- **No greeting.** HW's NpSignaling (`thd_np.cpp`) announces ESTABLISHED and ACTIVE 200 and 400 ms
  after a peer's address is known. Matching2 (`thd_matching2.cpp`) announces room links 500 or
  800 ms after a create or join, its own included.
- **The greeting is dropped.** The P2P reader keeps only the game's packets, hole-punch probes and
  STUN answers. An updated game waits for HW's greeting and drops HW's player after 30 s.
- **2 KB packets.** HW's P2P port reads and sends at most 2 KB. Lantern 0.9.42 sends larger
  datagrams whole, and they are lost; the game's own larger datagrams fail to send.

## 3. The design

### 3.1 Parts

**Needed to play with updated players** (no switch: the old way cannot connect to them):

1. **NpSignaling, ported from bbhost** (new `thd_signaling.h/.cpp`; `thd_np.cpp` registers it).
2. **Matching2 on the handshake** (`thd_matching2.cpp`).
3. **The P2P port**: greetings, 64 KB datagrams, pieces over 1,200 bytes (`thd_net.cpp`, new
   `thd_p2p_split.h`).
4. **The NpId compares** (`thd_np.cpp`, new `thd_np_util.h`).
5. **The sanitize call**: character names (`thd_np.cpp`, `thd_np_util.h`).

**bbhost's behaviour since our base, taken with it:**

6. **The sign-in check**, and a refused sign-in keeping the game offline (`thd_session.cpp`, new
   `thd_sign_in.h`, the HTTP hook).
7. **One event poll in flight; keepalives by generation** (`thd_session.cpp`).

**HW's own, in the same change:**

8. **Players' addresses masked in the log** (all of `thd/`).
9. **One line per session** on how its links went (`thd_signaling.cpp`).

### 3.2 Part 1: NpSignaling

**A class, not file-level globals.** `BbThd::Signaling` holds bbhost's state: contexts,
transports, connections and the connection-id seed. Everything outside it comes in through
`Signaling::Env`, a struct of functions:

| `Env` member | In the game | bbhost's name |
|---|---|---|
| `send(addr, port, bytes)` | `P2pSignalSend` (part 3) | `hle_net_p2p_signal` |
| `dispatch(delay_ms, prio, fn)` | `DispatchAfter` | `net::dispatch_after` |
| `call_game(cb, ctx, conn, event, error, arg)` | `CallGuest` | `hle_call_guest` |
| `peer_get`, `peer_find`, `peer_set_conn`, `peer_provisional` | `PeersGet`, `PeersFindOnline`, `PeersSetConn`, `PeersProvisional` | `net::peers_*` |
| `resolve(online_id)` | `ServerSignalingResolve` | `net::server_signaling_resolve` |
| `online_id()` | `OnlineId` | `net::online_id` |
| `m2_up(id)`, `m2_dead(id, error)`, `m2_closed(id)` | part 2 | `hle_np_matching2_transport_up`, `_dead`, `peer_closed` |
| `log` | `Log` (addresses masked, part 8) | `host_log` |

The game has one `Signaling`. A test builds two, or one beside bbhost's own (section 5).

**The port is bbhost's `src/hle/np_signaling.cpp` at `a317321`, function for function:**
- the same states (Inactive, SendingOffer, WaitOffer, SendingAccept, WaitAccept, ConnCheck,
  Established);
- the same message kinds and reasons, and the same byte layout;
- the same 500 ms step, 30 s limit and 1 s Matching2 delay;
- the same events the game sees, and the same error codes;
- bbhost's function names kept in comments, so a later bbhost change maps line to line.

Where bbhost's behaviour looks odd, the port keeps it: the game's co-op was tested against bbhost's.
Any doubt is a register row, not a silent change.

**What the game sees** (its SocketState is the only caller):

| Call or event | Now | Beta 14 |
|---|---|---|
| ActivateConnection | a connection; ESTABLISHED at +200 ms, ACTIVE at +400 ms once the address is known | a connection; ActivationRequest and the handshake to the peer; ESTABLISHED when it completes, MUTUAL_ACTIVATED once both games activated |
| A peer's activation without ours | nothing | PEER_ACTIVATED once the handshake completes |
| GetConnectionStatus | 2 once announced | 2 only while the handshake holds; 1 while it runs |
| DeactivateConnection | marked dead | Close to the peer, DEAD (terminated by myself); a second call answers CONN_NOT_FOUND |
| The peer's Close | none | PEER_DEACTIVATED, or DEAD (terminated by peer) |
| No handshake in 30 s | none (always "up") | DEAD (timeout) |
| Connection numbers | from 1 in every game | from a random point, 1 to 0x7fff |

**Threads:**
- The game's calls return at once, as now. The server's address lookup for a player not yet in
  the room runs on the NP thread: bbhost's `activation`, on the dispatcher, as HW's lookup already
  does.
- Greetings arrive on the P2P reader's thread and are handed to the dispatcher.
- Every event reaches the game from the dispatcher, outside the class's lock.

**Lock order:** Matching2's `g.mu`, then `Signaling`'s lock, then the peer table's; never the other
way. Matching2's hooks run as dispatched calls, so the lock is never held across them.

### 3.3 Part 2: Matching2 on the handshake

bbhost #217's `np_matching2.cpp` changes, merged into HW's file:

- Create and join start the handshake to each member (`connect_signaling_for_all`); so does each
  member joining (`room_member_joined`).
- 0x5102 (ESTABLISHED) goes to the game per member, 1 s after that member's handshake completes,
  and never for ourselves. It now comes 1-2 s after a join, against 0.5-0.8 s on the timer.
- 0x5101 (DEAD) when a member's handshake gives up (TIMEOUT) or its connection closes (by us, by the
  peer). Sent once per member (`m2_dead_sent`).
- The status read comes from the member's transport: 2 only after 0x5102 went out.
- LeaveRoom: a member whose NpSignaling connection the game still holds gets its DEAD when the game
  closes that connection (`dead_on_close`); the others at once, their transports stopped.
- `forget_room` no longer resets NpSignaling: its connections are the game's to close.
- **HW's own stay:**
  - the server's leave sent off the game's thread (0.4.4 beta 25a);
  - the leaves owed to the server, sent once by the NP thread or by the exit (`ExitBegin`,
    `ExitWait`);
  - the heartbeat;
  - room-gone handling;
  - the guest invite.

### 3.4 Part 3: the P2P port

- **The reader** takes datagrams up to 65,507 bytes (was 2,048). A relay delivery is unwrapped
  first, as now.
- **Greetings in.** Datagrams starting `fe 'b' 'b' 's'` are taken before the game's header check.
  Each refreshes the hole-punch bookkeeping for its source, as a probe does, and goes to the
  dispatcher. The game never sees one.
- **Greetings out** (`P2pSignalSend`, 512 bytes at most): sent from the P2P port, so the NAT
  mapping is the game's own; through the relay, with its 12-byte header, when the peer's address
  is the relay's.
- **The game's datagrams out** (`dgram_send`, rebuilt as bbhost v0.2.35):
  - one buffer per thread with room for the relay header, the game's bytes copied once;
  - over 1,200 bytes, sent as pieces of whole chunks, each behind its own P2P header and the
    relay's when relayed;
  - bytes that are not whole chunks sent whole;
  - the call returns the datagram's length once a piece has left, and an error only when none has;
  - a datagram too large for UDP returns EMSGSIZE with the game's errno set, logged once.
- **The split rule** is a pure function in `thd_p2p_split.h` (bbhost's `chunk_split`), so it is
  tested on its own.
- **The port's counts** (the existing minute line) add: datagrams sent as pieces, and the largest
  in and out.

### 3.5 Parts 4 and 5: the compares and character names

In `thd_np_util.h`, pure functions:
- `CompareOnlineId`: `strncmp` over 16 bytes;
- `CompareNpId`: the name, then bytes 20-27 and 28-35;
- `SanitizeCopy`: the text, cut at 255 bytes but never inside a UTF-8 character, with a null.

**Answers:**
- Not equal answers `0x80550609` (NOT_MATCH) instead of `memcmp`'s sign; a missing pointer stays
  INVALID_ARGUMENT.
- `sceNpScoreSanitizeComment` (`r4oAo9in0TA`) copies the text; a missing pointer answers the score
  library's invalid argument, `0x80550704`.
- `sceNpScoreCensorComment` (`2b3TI0mDYiI`) keeps answering 0.

### 3.6 Part 6: the sign-in check (to discuss; revised 2026-11-16)

**Today:** the launcher's "Signed in as NAME" is what the PC has saved, never checked with the
server. When the server refuses that sign-in (an expired or revoked token), HW notes it in the log
and slows its poller, but `sceNpGetState` still reports signed in. So the game goes online, every
request it makes is refused all session, and nothing tells the player why.

**Proposed, as small as it can be:**
- **The game is told the truth:** while the server refuses the sign-in, `SignedIn()` answers no, so
  `sceNpGetState` reports signed out. The game then shows its own "not signed in" and "Starting
  game in offline mode" dialogs, as bbhost found (`2602c3c`). No note of ours, no launcher change.
- **The game's own requests count too:** a 401 carrying `{ResKind, Message}` marks the sign-in
  refused, as the NP requests already do.
- **Optionally, an early answer:** at `Initialize`, on a thread of its own and without waiting for
  it, the engine asks `/auth/whoami` once (5 s at most), so a refused sign-in is known before the
  game's own online login. The game's start is not delayed by it. Its answers are read by bbhost's
  rules (`thd_sign_in.h`):

| The answer | Verdict |
|---|---|
| ResKind 0 with an OnlineId | valid (logged with the name the server knows) |
| ResKind non-zero on a 200 or a 401 | refused (the server's Message, in the log) |
| no answer, 404, 429, 5xx, not JSON, ResKind 0 without an OnlineId | no verdict: nothing changes |

- **Not proposed any more:** the 15 s note over the game, and the launcher's sign-in state.

### 3.7 Part 7: one poll in flight

- A stopped poller's held poll is given up (the client stopped).
- A new poller's first poll waits for it, so two never run at once.
- The keepalive gets the poller's generation, so a stop and a start within its 15 s leave one, not
  two.

This is the rest of bbhost's `2e40749`; HW has its long polling already.

### 3.8 Part 8: addresses in the log

- **Today:** HW's online lines print other players' and the PC's public addresses whole. Testers
  post logs in public.
- **Beta 14:** they print the first two parts (`203.0.x.x:3658`): still enough to tell a LAN
  address from a public one, and the relay's from a peer's.
- One helper in `thd_internal.h`, used by every line in `thd/`, the new ones included.

### 3.9 Part 9: the session's line

When a room ends (and at exit), one line:

    Online: links this session: 3 up (handshake 180-420 ms), 1 timed out after 30 s (the peer never answered: an older client, or no route), 2 closed by the peer, 1 by us; through the relay 1; datagrams sent as pieces 412, largest 4,140 bytes out, 1,186 in

- A timeout says the peer never answered at all, or answered without completing. That tells an
  older client from a network problem.
- `BB_THD_TRACE=1` adds a line per greeting message, as bbhost's `BBHOST_NP_TRACE`.

## 4. Who connects with whom

| The other player | Before beta 14 | Beta 14 |
|---|---|---|
| bbhost 0.2.31 or later | dropped after 30 s | connects |
| Lantern 0.9.42 or later | dropped after 30 s, large packets lost | connects |
| HW beta 14 or later | (not yet) | connects |
| HW before beta 14, bbhost before 0.2.31, Lantern before 0.9.42 | connects (timers both sides) | dropped after 30 s: they must update, as the announcement says |

Ghosts, messages and bloodstains work with everyone either way.

## 5. Checks

**Here, before the tester:**
- The changed files build: Linux, and Windows through MinGW.
- **Unit tests** (new targets beside `fmod-ring-test` in `gpu/CMakeLists.txt`):
  - `thd-np-util-test`: the compares (equal, other name, other opt, other reserved, a name of 16
    characters, missing pointers); the sanitize copy (ASCII, Japanese, the limit, a cut inside a
    character, missing pointers). bbhost's `np_score_test` cases are included.
  - `thd-p2p-split-test`: the split (one- and two-byte vports, a chunk longer than 1,200 alone,
    bytes that are not chunks sent whole, a cut chunk, the 65,507-byte limit), and the pieces' bytes
    and headers through a loopback socket, direct and in the relay's framing. These are bbhost's
    `net_p2p_test` cases.
  - `thd-sign-in-test`: bbhost's `sign_in_check_test` answers (valid, refused on 200 and 401, a 404,
    a 429, a 5xx, not JSON, no ResKind, no OnlineId).
- **The handshake against bbhost's own code** (`thd-signaling-test`):
  - bbhost's `np_signaling.cpp` at `a317321` kept unchanged in `tests/third_party/bbhost/` with its
    notice. It is built only into the test, with a few test-only headers, and never shipped.
  - It runs as one player and our port as the other, through an in-memory network with a fake
    clock.
  - The cases:
    - each side activating first, and both at once;
    - only the peer activating (PEER_ACTIVATED);
    - a deactivate (Close) and a second one (CONN_NOT_FOUND);
    - a peer that never answers (DEAD at 30 s);
    - 30% of datagrams lost and reordered (still up, by the resends);
    - an address learned late from the server;
    - the room's DEAD and ESTABLISHED (0x5101, 0x5102) per member.
  - The same cases run with our port on both sides.
  - Each case checks the events, their order and their error codes, against what bbhost's side
    sees.
- `tests/test_online.py` and the switch-group checks, as for every beta.

**The tester's runs** (with `BB_THD_TRACE=1`):

| Run | With | Shows |
|---|---|---|
| Co-op, HW hosting | a bbhost 0.2.35 player | summon arrives; the session line: 1 up |
| Co-op, HW summoned | a bbhost 0.2.35 player | as above, the other way |
| Co-op | a Lantern 0.9.42 player, another HW beta 14 player | as above |
| An invasion, either way | any updated player | the invader arrives and leaves cleanly |
| A player leaving mid-session, and one quitting the game | | DEAD for that member only; the room goes on |
| A summon that fails (an older client, or the sign cancelled) | | DEAD (timeout) after 30 s, said in the session line |
| A large fight or a warp | | pieces sent; no stall |
| A character name with Player Name Display "Name" | another updated player | the name shows both ways |

## 6. Our name to the server

**How HW names itself today** (inherited from Lantern):
- The launcher sets `BB_CLIENT_VERSION` to `shadPS4 Awakenchan/v0.20.0`.
- The session service's requests carry the User-Agent `shadPS4/Awakenchan/v0.20.0`, and the
  game's own requests have it added to theirs.
- The account's sign-in calls the PC `BloodBorneButPc on <PC>`.

bbhost names itself `bbhost/<version>`.

**What that means:**
- The server cannot tell HW from Lantern, nor one HW version from another.
- It is why the update broke nothing at the server.
- But it credits HW's traffic to Lantern, and anything the server does about Lantern (or about old
  Lantern versions) reaches HW.
- The Hunter's Dream accepts clients by name: yharnam-pc's online was refused when it renamed
  itself, and stays off "until its team accepts this client".

**The design:**
- One constant in the launcher (`GameEnvironment`) and one in `HuntersDream.DeviceName`, ready to
  become `HuntersWorkshop/<version>` and `Hunter's Workshop on <PC>`.
- They change only once The Hunter's Dream's team says yes.
- **Decided (the tester, 2026-11-15):** beta 14 keeps the current name; the team is not asked yet.
- **bbhost's own name (`bbhost/<version>`) is not used.** It would present HW as The Hunter's
  Dream's own client. Their logs and counts would put HW's players and HW's bugs under bbhost's
  versions, and any rule the server applies by bbhost version would apply to HW on the strength of
  a version it does not run. Nothing requires it: the server works with any name.

## 7. Shipping

- **0.4.6 beta 14:** launchers `0.4.6-beta.14`, engine id `hunters-workshop-100`, release notes,
  `licenses/SHADPS4-NETWORK.md` and `thd.h` naming the bbhost commit ported.
- **No switch for parts 1-5:** the old way cannot connect to updated players.
  - Parts 6-9 follow bbhost or only log, so they need none either.
  - `BB_ONLINE_PAUSED=1` already keeps the game offline if something goes wrong.
- **0.4.5 stable:** no hotfix (the tester, 2026-11-16).

## 8. Risks

- **The handshake cannot be played here.** It is checked against bbhost's own code in the test, and
  only the tester's runs show it with real players and NATs.
- **Room links come up later:** 1 s after the handshake, against 0.5-0.8 s on a timer, as bbhost's
  do. The game waits for them (it did on the console).
- **A player behind a network that blocks UDP** connects through the relay only if the server
  relays them. That is the server's decision by address; the website's connection test records it.
  Unchanged from today.
- **The resends** add about 92 bytes a second per player during a handshake, nothing once up.

## 9. Not now (each its own decision later)

- **The connection test and the relay switch:** launcher UI. Until then HW players can run the
  website's test, which the server keeps for the account as bbhost's.
- **Matching Regions starting at Worldwide, the play log every 30 s, Full Phantom HP:** game
  changes, not the server's.
- **The launcher's sign-in state:** not wanted (the tester, 2026-11-16).
