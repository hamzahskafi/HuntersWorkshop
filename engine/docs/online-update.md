# The Hunter's Dream's 0.2.31 update: the plan

Planned 2026-11-15, for the tester's decision before anything is built. The source is bbhost, the
client The Hunter's Dream's own team writes, read at its newest release (v0.2.35, `a317321`,
2026-11-14) against Hunter's Workshop's online backend (`gpu/bbnet/thd/`, bbhost `566184a` of
2026-10-11 plus HW's fixes; unchanged since 0.4.4). Lantern 0.9.42 is not the source: it copied
bbhost's code as it was, and The Hunter's Dream's team gave the server's changes to bbhost alone.

## What changed

**The announcement** (The Hunter's Dream's Discord, ancat, 2026-11-10): with bbhost 0.2.31 the
server and bbhost's network side changed; co-op and PvP matchmaking need 0.2.31 or later, and
"previous versions will no longer be compatible"; ghosts, messages and bloodstains keep working.

**What bbhost changed for it, read from its code:**

- **The games greet each other** (bbhost #217, `3f9c4c0`, in 0.2.31). Before, a game counted a
  link to another player as up on a timer once the server gave it the player's address. Now two
  games run a handshake straight between them, on the game's own P2P port, before the link counts:
  - datagrams of 46 bytes starting `fe 'b' 'b' 's'`: the kind, a reason, both players' online
    ids, a nonce;
  - control messages ActivationRequest, ActivationAck, Established and Close; the handshake Offer,
    Accept, Check and CheckAck;
  - resent every 500 ms; a link not up in 30 s is dead (the timeout players see);
  - through the server's relay like the game's own traffic when the relay carries a player;
  - room links (Matching2) go up when the handshake does, 1 s after it, and go down with it;
  - connection numbers start at a random number per game (two games no longer hand out the same).
- **The NpId compares** (`sceNpCmpNpId`, `sceNpCmpOnlineId`) compare the name, then its two 8-byte
  fields, as the console does: comparing all 36 bytes made two copies of one player's id differ.
- **Character names** (`f990415`, 0.2.31): `sceNpScoreSanitizeComment` gives the text back, so
  other players see a player's character name instead of a blank.
- **Packets:** #217 lifted the 2 KB limit on P2P datagrams (the game batches up to about 5 KB);
  v0.2.35 (`e74e3ce`) sends those over 1,200 bytes as pieces of whole chunks again, as 0.2.26 did,
  because NATs that drop IP fragments lost them whole.

**What did not change:** every request to the server. bbhost v0.2.35 and HW call the same endpoints
(`/np/events/poll` and `/ack`, `/np/signaling/resolve`, `/mp/matching2/context_start`,
`create_room`, `join_room`, `leave_room`, `heartbeat`, `kick_member`, `signaling_update`, the
`/auth/...` account calls) with the same fields, handle the same six server events, and speak the
same relay (STUN BBHOST-HELLO and BBHOST-RELAY, `bbr1`). The server's own changes are not in any
client's code; what a client must do for them is the list above. bbhost's NpSignaling has not
changed since 0.2.31, so there is no later fix to it to take.

**Newer server features (0.2.32 to 0.2.35), not needed to connect:** the connection test (STUN to
the server's test ports 3479 and 3480 from the P2P port, `/auth/nat_test`, kept for the account),
the account's relay switch (`/auth/profile` `RelayDirect`), the sign-in check (`/auth/whoami`).

## Why HW breaks

HW's NpSignaling (`thd_np.cpp`) announces ESTABLISHED and ACTIVE 200 and 400 ms after the address
is known, and Matching2 (`thd_matching2.cpp`) its room links 500 or 800 ms after a create or join.
It never sends a greeting and drops the ones it gets (the P2P reader keeps only the game's packets,
hole-punch probes and STUN answers). An updated game waits for HW's greeting and drops HW's player
after 30 s. HW's P2P port reads and sends at most 2 KB: a larger packet from an updated game
(Lantern 0.9.42 sends them whole) is lost, and the game's own larger packets fail to send.

## The plan

### Needed to play with updated players

1. **NpSignaling from bbhost, unchanged.** bbhost's `src/hle/np_signaling.cpp` at `a317321`
   copied into `gpu/bbnet/vendor/bbhost/` with a README naming the commit and its SHA-256, built
   with a few headers of our own that give it what it calls: the dispatcher (`DispatchAfter`), the
   peer table (`PeersGet`, `PeersFindOnline`, `PeersSetConn`, `PeersProvisional`), the server's
   resolve, our online id, the game's callback (`CallGuest`), the log, and the P2P port's sender.
   HW has every one of these already. `thd_np.cpp` loses its own NpSignaling and registers
   bbhost's seven functions. Copied whole rather than rewritten: it is the exact code bbhost players
   run, and a later bbhost fix is a copy again.
2. **Matching2 on the handshake** (#217's `np_matching2.cpp` changes, into `thd_matching2.cpp`):
   room links up per member when its handshake is (`transport_up`), dead with it (`transport_dead`);
   the status read from the handshake; create, join and each member joining start the handshake to
   the members; LeaveRoom keeps a member's DEAD until the game closes its NpSignaling connection
   (`peer_closed`). HW's own fixes stay: the server's leave off the game's thread (0.4.4 beta 25a),
   the dead-connection check, the room left on quit (`ExitBegin`/`ExitWait`).
3. **The P2P port** (`thd_net.cpp`): the greeting datagrams taken from the reader (direct and
   relayed) to NpSignaling, and sent by it (through the relay when the peer needs it); datagrams up
   to 65,507 bytes read and sent; those over 1,200 bytes sent as pieces of whole chunks, as bbhost
   v0.2.35.
4. **The NpId compares** as #217.
5. **The sanitize call** gives the text back, cut at 255 bytes outside a UTF-8 character, as
   `f990415`: HW players' character names show for others.

### bbhost's behaviour since our base that we should take too

6. **The sign-in check** (`6f75eb7`, `2602c3c`): at the start the engine asks `/auth/whoami`; a
   refused sign-in (an expired or revoked token, a 401 on the game's own requests) keeps the game
   offline and says so once over the game, instead of a session of refused requests. The
   launcher's Online page showing the state (signed in, expired, not checked) is a UI change:
   shown to the tester first.
7. **One event poll at a time** (the rest of `2e40749`): HW's poller has generations but a stopped
   poller's held poll and a new one's can overlap, and a stop and start within 15 s leaves two
   keepalives; both get bbhost's rule.

### Not now (each its own decision later)

- The connection test and the relay switch: launcher UI; until then HW players can run the
  website's test, which the server keeps for the account as bbhost's.
- Matching Regions starting at Worldwide, the play log every 30 s, Full Phantom HP: game changes,
  not the server's.

## Our name to the server

HW names itself as Lantern does, inherited: the launcher sets `BB_CLIENT_VERSION` to
`shadPS4 Awakenchan/v0.20.0`, so the session service's requests carry the User-Agent
`shadPS4/Awakenchan/v0.20.0` and the game's own requests have it added to theirs; the account's
sign-in calls the PC `BloodBorneButPc on <PC>`. bbhost names itself `bbhost/<version>`. So the
server cannot tell HW from Lantern, nor one HW version from another. That is why the update breaks
nothing at the server, but it credits HW's traffic to Lantern, and anything the server does about
Lantern (or old Lantern versions) would reach HW. The Hunter's Dream accepts clients by name:
yharnam-pc's online was refused when it renamed itself, and stays off "until its team accepts this
client". The plan: ask The Hunter's Dream's team to accept HW by its own name
(`HuntersWorkshop/<version>`, the PC as `Hunter's Workshop on <PC>`), and change both once they
say yes; until then the name stays. The tester's call (it is a message to their team).

## Checks

- **Here:** the changed files build; tests of our own for the compares, the sanitize copy and the
  datagram pieces (bbhost's cases: one- and two-byte vports, a longer chunk alone, bytes that are
  not chunks sent whole, the 65,507-byte limit), and two NpSignalings greeting each other through
  a loopback harness (both activate, one activates, a Close, the 30 s timeout).
- **The tester's runs** (`BB_THD_TRACE=1` also turns on bbhost's handshake trace): co-op as host and
  as guest with a bbhost v0.2.35 player, with a Lantern 0.9.42 player and with another HW player;
  an invasion; a player leaving mid-session; a summon that fails (the 30 s timeout said in the log).

## Shipping

- **0.4.6:** the next beta (14), the memory work's beta 14 renumbered 15. Parts 1 to 5 have no
  switch: the old way cannot connect to updated players.
- **0.4.5 stable:** the online backend has not changed since 0.4.4, so the same change applies to
  0.4.5 as it is: a 0.4.5 hotfix with only this is possible, if the tester wants one.
- **Credits:** `licenses/SHADPS4-NETWORK.md` and `thd.h` name the bbhost commit taken.
