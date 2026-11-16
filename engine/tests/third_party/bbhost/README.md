# bbhost's NpSignaling, for one test

`np_signaling.cpp` is droogie/bbhost's `src/hle/np_signaling.cpp` at commit `a317321` (v0.2.35;
the file is unchanged since `3f9c4c0`, bbhost #217), byte for byte
(SHA-256 `2048305d1029a524f4ff86435d5ab8bdb80a1e2fbdfa09b5d0e3f78d44a00c8c`).

- **Licence:** GNU GPL version 3 or later, with bbhost's additional permission in
  `LICENSE-EXCEPTION` (copied from the same commit). Copyright its authors.
- **Use:** only `thd-signaling-test` (`tests/test_thd_signaling.cpp`) builds it, so the game's
  NpSignaling as Hunter's Workshop ports it (`gpu/bbnet/thd/thd_signaling.cpp`) can be run against
  bbhost's own code. It is never built into the engine and never shipped.
- **`shim/`:** test-only stand-ins for the bbhost headers the file includes. They declare what it
  uses; the test defines it on a fake clock and an in-memory network.
- **Updating:** copy the file again from the bbhost commit a later Hunter's Workshop port follows,
  with the new hash here, and run the test.
