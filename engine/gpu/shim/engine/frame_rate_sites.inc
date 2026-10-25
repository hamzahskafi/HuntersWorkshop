// SPDX-License-Identifier: GPL-3.0-or-later
// The frame-rate corrections at 60 FPS and above (frame_rate.cpp): what Kyo's "60 FPS++" (1.0, as
// patches/Bloodborne.xml ships it, the base at every rate) leaves at the game's 30 FPS timing or gets wrong, and
// what the rates above 60 need on top of it. Adapted from bbhost (droogie/bbhost 566184a:
// src/engine/frame_rate.cpp, frame_rate_dt_loads.inc and tools/fps_sites.py), which also collects Kyo's 1.3
// lists and yogesh1239's high-FPS findings. Addresses are the patch lists' (the ELF's VA + 0x400000).
// tools/re/frame_rate_check.py checks every entry against a 1.09 eboot.elf and the list.

// FRAME_SITE(group, address, the game's bytes, the 60 FPS++ list's bytes there or "", ours): code that steps
// by a fixed 1/30 or 1/60 made to read the frame's time, the 60 FPS++ cave's slot (0x575a908); then the list's
// sound cap undone (ours the game's) and, above 60 only, the cloth caves.
// the target bank refresh: its accumulator steps by the frame (SprjTargetBankManager::Update).
FRAME_SITE(Targeting, 0x16a4416, "c5fa58055e136803", "", "c5fa5805ea640b04")
// the ragdoll/clip drive, path B (sub_1915ec0): 1/30 s a call, two calls a frame at 60.
FRAME_SITE(Clips, 0x1915fab, "c5fa1005b1034103", "c5fa1005bd314103", "c5fa10055549e403")
// the ragdoll/clip drive, path C.
FRAME_SITE(Clips, 0x1916030, "c5fa10052c034103", "", "c5fa1005d048e403")
// the ragdoll/clip drive, path A: its 1/30 stays the frame count's seconds, the step is the frame's.
FRAME_SITE(Clips, 0x1915f65, "c5fa2ac9c5fa1005f3034103c5f259c8488b7b208b80e1010000a8040f94c00fb6c84c89f64c89fa4883c408", "", "488b7b20c5fa2ac9c5f2590def034103c5fa10058b49e40331c9f680e1010000040f94c14c89f64c89fa5890")
// the Havok AI world step (enemy steering): its step-info's dt from the frame.
FRAME_SITE(Physics, 0x222bcbf, "488d0d6a404c034883c11048898d40feffffc78548feffff8988083d", "488d0d6a404c034883c11048898d40feffffc78548feffff8988883c", "488d0d7a404c0348898d40feffff8b0d35ec5203898d48feffff6690")
// the Havok physics world step (rigid bodies, ragdolls, breakables): its dt from the frame.
FRAME_SITE(Physics, 0x1bf9b69, "48c745c000000000c745b88988083d", "48c745c000000000c745b88988883c", "8b05990db6038945b8660f1f440000")
// grass sway's second path (the list already points the first at the frame's time).
FRAME_SITE(Grass, 0x2713d8d, "c5fa1025db606102", "c5fa1025736b0403", "c5fa1025736b0403")
// the sound thread's sleep: the game's call back in place of the list's cap (16 ms, a cave in the loop); the
// thread's update time (FRAME_COUNT below) is one frame instead.
FRAME_SITE(Sound, 0x2c0137d, "e83ea788ff", "e84affffff", "e83ea788ff")
// The cloth above 60, from Kyo's "90 FPS++" (bbhost's ninety_fps_sites.inc): with one substep a frame (the 60
// list's 0xb6e67a) the cloth solver runs at the frame rate against the 60 its part files were made for, so
// each of the six constraint operators gets a cave (in a dead profiling block: its guard's jae made a jmp)
// that multiplies the stiffness it is called with by a scale, (60 dt)^2 as in Kyo's 1.3 lists. The jump to
// each cave, then the cave.
FRAME_SITE(Cloth, 0xb20dee, "c5f82945a0", "", "e921000000")
FRAME_SITE(Cloth, 0xb20e12, "7317488d050def08044889010f318941084883c11048894b08", "", "eb17c5f85905e4ae2104c5f82945a0e9cdffffff9090909090")
FRAME_SITE(Cloth, 0xb2b2fe, "c5f82945a0", "", "e921000000")
FRAME_SITE(Cloth, 0xb2b322, "7317488d05e85d08044889010f318941084883c11048894b08", "", "eb17c5f85905d4092104c5f82945a0e9cdffffff9090909090")
FRAME_SITE(Cloth, 0xb30ade, "c5f82945a0", "", "e921000000")
FRAME_SITE(Cloth, 0xb30b02, "7317488d05350808044889010f318941084883c11048894b08", "", "eb17c5f85905f4b12004c5f82945a0e9cdffffff9090909090")
FRAME_SITE(Cloth, 0xb38711, "c5f82945b0", "", "e929000000")
FRAME_SITE(Cloth, 0xb3873d, "7317488d058c9307044889010f318941084883c11049894d08", "", "eb17c5f85905b9352004c5f82945b0e9c5ffffff9090909090")
FRAME_SITE(Cloth, 0xb41f4e, "c5f8294580", "", "e921000000")
FRAME_SITE(Cloth, 0xb41f72, "7317488d0505fd06044889010f318941084883c11048894b08", "", "eb17c5f85905849d1f04c5f8294580e9cdffffff9090909090")
FRAME_SITE(Cloth, 0xb420ee, "c5f82945a0", "", "e921000000")
FRAME_SITE(Cloth, 0xb42112, "7317488d0595fb06044889010f318941084883c11048894b08", "", "eb17c5f85905e49b1f04c5f82945a0e9cdffffff9090909090")

// FRAME_LOAD(group, instruction, length, displacement offset, constant, kind, lanes, the instruction's bytes,
// the constant's value in each lane): a value the game means "a frame" by, read from a slot of ours that
// frame_rate.cpp rewrites every frame from the frame's time; the constant itself stays as it is. An
// instruction one of the group's sites writes (the cloth caves') is checked against the site's bytes.
// the cloth caves' stiffness scale (vmulps xmm0, xmm0, [0x4d3bd00]: the tail of an assert message's file
// name, "DLLightMutex.cpp", which the 90 list overwrites with its scale), (60 dt)^2 at most 1 every frame.
FRAME_LOAD(Cloth, 0xb20e14, 8, 4, 0x4d3bd00, Cloth, 4, "c5f85905e4ae2104", 0x694c4c44, 0x4d746867, 0x78657475, 0x7070632e)
FRAME_LOAD(Cloth, 0xb2b324, 8, 4, 0x4d3bd00, Cloth, 4, "c5f85905d4092104", 0x694c4c44, 0x4d746867, 0x78657475, 0x7070632e)
FRAME_LOAD(Cloth, 0xb30b04, 8, 4, 0x4d3bd00, Cloth, 4, "c5f85905f4b12004", 0x694c4c44, 0x4d746867, 0x78657475, 0x7070632e)
FRAME_LOAD(Cloth, 0xb3873f, 8, 4, 0x4d3bd00, Cloth, 4, "c5f85905b9352004", 0x694c4c44, 0x4d746867, 0x78657475, 0x7070632e)
FRAME_LOAD(Cloth, 0xb41f74, 8, 4, 0x4d3bd00, Cloth, 4, "c5f85905849d1f04", 0x694c4c44, 0x4d746867, 0x78657475, 0x7070632e)
FRAME_LOAD(Cloth, 0xb42114, 8, 4, 0x4d3bd00, Cloth, 4, "c5f85905e49b1f04", 0x694c4c44, 0x4d746867, 0x78657475, 0x7070632e)
// movement controller momentum, 0.95 a frame on the body's X/Z (chr_motion_physics_update).
FRAME_LOAD(Jump, 0x1e4abca, 8, 4, 0x4d0b5b0, Keep, 4, "c5f85905de09ec02", 0x3f733333, 0x3f733333, 0x3f733333, 0x3f733333)
// sprint wall test: blocked, x0.8 a frame.
FRAME_LOAD(Sprint, 0x1914c4d, 8, 4, 0x4d26334, Keep, 1, "c5fa5905df164103", 0x3f4ccccd)
// sprint wall test: free, x1.2 a frame.
FRAME_LOAD(Sprint, 0x1914c77, 8, 4, 0x4d26338, Keep, 1, "c5fa5905b9164103", 0x3f99999a)
// CSSlopeCtrl: slope fall damping 0.65 a frame off the slope (.data tunable).
FRAME_LOAD(Slope, 0x1e46bd6, 8, 4, 0x5528374, Keep, 1, "c5fa100596176e03", 0x3f266666)
// CSSlopeCtrl: slope fall speed 1.0 a frame times the pull (.data tunable).
FRAME_LOAD(Slope, 0x1e46c52, 8, 4, 0x5528370, Amount, 1, "c5fa100516176e03", 0x3f800000)
// SprjWorldAiManager::TickLua: the AI Lua state's full GC timer, -1/30 a call (every 0.5 s at 30).
FRAME_LOAD(Timers, 0x16dc848, 8, 4, 0x4d25940, Amount, 1, "c5fa5805f0906403", 0xbd088889)
// fade in, +0.05 a frame.
FRAME_LOAD(Timers, 0x1d3251e, 8, 4, 0x4d280ac, Amount, 1, "c5fa5805865bff02", 0x3d4ccccd)
// fade out, -0.05 a frame.
FRAME_LOAD(Timers, 0x1d3259b, 8, 4, 0x4d280c0, Amount, 1, "c5fa58051d5bff02", 0xbd4ccccd)
// attack hitbox velocity: movement this frame x 30, a 4-lane vector.
FRAME_LOAD(Hitbox, 0x1c60cc3, 8, 4, 0x4d02f10, Rate, 4, "c5f8590545220a03", 0x41f00000, 0x41f00000, 0x41f00000, 0x41f00000)
// the same in sub_1c60fb0.
FRAME_LOAD(Hitbox, 0x1c61281, 8, 4, 0x4d02f70, Rate, 4, "c5f85905e71c0a03", 0x41f00000, 0x41f00000, 0x41f00000, 0x41f00000)
// targeting load-spreading ring: delay x 30 picks a slot (x), an overflow adds k/30 (/).
FRAME_LOAD(Targeting, 0x16ab623, 8, 4, 0x4d25840, Rate, 1, "c5fa590d15a26703", 0x41f00000)
// targeting load-spreading ring: delay x 30 picks a slot (x), an overflow adds k/30 (/).
FRAME_LOAD(Targeting, 0x16ab6cf, 8, 4, 0x4d25840, Rate, 1, "c5f25e0d69a16703", 0x41f00000)
// the sprint's wall test: this frame's distance x 30 against 1 unit a second.
FRAME_LOAD(Sprint, 0x1914c24, 8, 4, 0x4d2632c, Rate, 1, "c5fa590500174103", 0x41f00000)
// NetAIManipulator: a remote enemy's movement this frame x 30 against 2.5.
FRAME_LOAD(Online, 0x1923a12, 8, 4, 0x4d26440, Rate, 1, "c5fa5905262a4003", 0x41f00000)
// NetworkManipulator: a remote player's movement this frame x 30 against 0.01/2/6.
FRAME_LOAD(Online, 0x1925982, 8, 4, 0x4d26478, Rate, 1, "c5e2591dee0a4003", 0x41f00000)
// a physics-driven bone's impulse: gap x mass x 30 (close it in one step).
FRAME_LOAD(Physics, 0x1c0f15e, 8, 4, 0x4d275e8, Rate, 1, "c5f2590d82841103", 0x41f00000)
// the equipment screen's character preview, a step a frame.
FRAME_LOAD(Menus, 0x19dc1f7, 8, 4, 0x4d26c9c, Step, 1, "c5fa10059daa3403", 0x3d088889)
// the shop's tab animation, a step a frame.
FRAME_LOAD(Menus, 0x1b3456e, 8, 4, 0x4d270e4, Step, 1, "c5fa10056e2b1f03", 0x3d088889)
// ReplayGhostIns::UpdateFade: -0.1 a frame.
FRAME_LOAD(Timers, 0x1d084f5, 8, 4, 0x4d27fd4, Amount, 1, "c5fa580dd7fa0103", 0xbdcccccd)
// ReplayGhostIns::UpdateFade: -0.1 a frame.
FRAME_LOAD(Timers, 0x1d08522, 8, 4, 0x4d27fd4, Amount, 1, "c5fa5805aafa0103", 0xbdcccccd)
// ReplayGhostIns::UpdateFade: +0.1 a frame.
FRAME_LOAD(Timers, 0x1d08518, 8, 4, 0x4d27fe0, Amount, 1, "c5fa580dc0fa0103", 0x3dcccccd)
// ReplayGhostIns::UpdateFade: +0.1 a frame.
FRAME_LOAD(Timers, 0x1d0854c, 8, 4, 0x4d27fe0, Amount, 1, "c5fa580d8cfa0103", 0x3dcccccd)
// YEBIS's frame time (gx_flip_present): half a 30 fps frame, 1/60.
FRAME_LOAD(Timers, 0x25d6ae2, 8, 4, 0x4d2979c, Amount, 1, "c5fa101db22c7502", 0x3c888889)

// FRAME_VALUE(group, address, kind, the game's value, the 60 FPS++ list's value): a value written once for the
// rate (uncapped: for 120, its reference rate; Game: the game's own back). The durations are seconds, not steps: the list converted them as
// if they were frames (Kyo's 1.3 lists and bbhost keep them; bbhost tools/fps_sites.py EXCLUDE).
// a count of 1/30 s frames in sub_11ca140's rate sums.
FRAME_VALUE(Durations, 0x4c88688, Game, 0x3d088889, 0x3c888889)
// a shortest-duration floor, max(t, 1/30) (0x12f06f1).
FRAME_VALUE(Durations, 0x4cadc08, Game, 0x3d088889, 0x3c888889)
// a shortest-duration floor, max(t, 1/30) (0x12f6992).
FRAME_VALUE(Durations, 0x4cadd28, Game, 0x3d088889, 0x3c888889)
// an attack hit's lifetime from a param frame count.
FRAME_VALUE(Durations, 0x4d28418, Game, 0x3d088889, 0x3c888889)
// the menus' auto-repeat interval.
FRAME_VALUE(Durations, 0x4d27720, Game, 0x3d088889, 0x3c888889)
// a network timer drained by the real step.
FRAME_VALUE(Durations, 0x1ca975a, Game, 0x41f00000, 0x42700000)
// a network timer drained by the real step.
FRAME_VALUE(Durations, 0x1ca9892, Game, 0x41f00000, 0x42700000)
// a network timer drained by the real step.
FRAME_VALUE(Durations, 0x1c9f889, Game, 0x41f00000, 0x42700000)
// a summon sign's refresh (network).
FRAME_VALUE(Durations, 0x187305d, Game, 0x41f00000, 0x42700000)
// a summon sign's refresh (network).
FRAME_VALUE(Durations, 0x187458d, Game, 0x41f00000, 0x42700000)
// a position record's period (network).
FRAME_VALUE(Durations, 0x18b254d, Game, 0x41f00000, 0x42700000)
// a position record's period (network).
FRAME_VALUE(Durations, 0x18b382d, Game, 0x41f00000, 0x42700000)
// remote objects' lifetimes (network).
FRAME_VALUE(Durations, 0x187afec, Game, 0x41f00000, 0x42700000)
// remote objects' lifetimes (network).
FRAME_VALUE(Durations, 0x187b0ad, Game, 0x41f00000, 0x42700000)
// a peer's session protection (network).
FRAME_VALUE(Durations, 0x1880cfd, Game, 0x41f00000, 0x42700000)
// a peer's session protection (network).
FRAME_VALUE(Durations, 0x1880e1a, Game, 0x41f00000, 0x42700000)
// the Havok AI's braking limit, m/s^2.
FRAME_VALUE(Durations, 0x21bb004, Game, 0x41f00000, 0x42700000)
// the physics world's expected minimum step (hkpWorldCinfo): a tolerance hint, 1/fps.
FRAME_VALUE(Physics, 0x0f383ca, Step, 0x3d088889, 0x3d088889)
// a shared 1/30 step the list leaves at 1/30 (its other readers step by the frame).
FRAME_VALUE(Physics, 0x4d29170, Step, 0x3d088889, 0x3d088889)
// the debug camera's step (Restore Debug Camera).
FRAME_VALUE(Timers, 0x1972a80, Step, 0x3d088889, 0x3d088889)
// YEBIS's elapsed time, half a frame: eye adaptation and afterimage decay.
FRAME_VALUE(Timers, 0x25df519, Amount, 0x3c888889, 0x3c888889)
// the target bank refresh's step (its load reads the frame's time above).
FRAME_VALUE(Targeting, 0x4d2577c, Step, 0x3d088889, 0x3d088889)
// the ragdoll keyframe controller's drive step (an hkaRagdollRigidBodyController's +0x60): the physics world's
// step, which follows the frame.
FRAME_VALUE(Physics, 0x1c0ca9a, Step, 0x3d088889, 0x3c888889)
// a tenth of a change a frame over the 10-frame interval at 0x192c405 (FRAME_COUNT below): the two together.
FRAME_VALUE(Timers, 0x4cf6420, Amount, 0x3dcccccd, 0x3d4ccccd)
FRAME_VALUE(Timers, 0x4cf6424, Amount, 0x3dcccccd, 0x3d4ccccd)
FRAME_VALUE(Timers, 0x4cf6428, Amount, 0x3dcccccd, 0x3d4ccccd)
FRAME_VALUE(Timers, 0x4cf642c, Amount, 0x3dcccccd, 0x3d4ccccd)

// FRAME_COUNT(group, address, size, the game's value, kind): a count written once for the rate. Frames: a
// number of frames, the game's times rate / 30 (uncapped: for 120). Micros: microseconds, the game's times
// 30 / rate. Digits: microseconds as five ASCII digits, one frame at the rate (uncapped: its limit, at most
// 240). The 60 FPS++ list holds 60's for the first three.
// a countdown of 10 frames (movl into +0xf8).
FRAME_COUNT(Timers, 0x192c405, 4, 10, Frames)
// the default of 5 an int setting is read with (mov edx before sub_24eba00).
FRAME_COUNT(Timers, 0x1929676, 4, 5, Frames)
// a worker's usleep(33000) in a polling loop.
FRAME_COUNT(Timers, 0x25629f1, 4, 33000, Micros)
// the sound thread's update time, MainThreadUpdateTime in the game's text config (sub_2bf67f0, strtol): 33333,
// two frames of sound requests an update at 60, and some attack sounds played half as often as at 30.
FRAME_COUNT(Sound, 0x4b33c51, 5, 33333, Digits)

// FRAME_TARGET(address, the game's value): the frame pacer's target, mov dword [r12+0x18], imm32 (the 60 FPS++
// list's 1/60): 1/rate, uncapped 1/its limit (at most 480). Without all four the game stays at 60.
FRAME_TARGET(0x2434840, 0x3c888889)
FRAME_TARGET(0x243485a, 0x3d088889)
FRAME_TARGET(0x243487e, 0x3d088889)
FRAME_TARGET(0x24348b3, 0x3d088889)

// FRAME_CLOTH_SCALE(address, the game's bytes): the scale the cloth caves read where a slot cannot replace it,
// (60 / rate)^2 in each of its four lanes (uncapped: for 120).
FRAME_CLOTH_SCALE(0x4d3bd00, "444c4c696768744d757465782e637070")

// FRAME_REQUIRE(address, the 60 FPS++ list's bytes): the list's frame-time code, which keeps the frame's step
// in 0x575a908 (the measured frame time clamped to [target, 1/30], eased by 0.15 a frame). The sites above
// read it; without it nothing here is applied.
FRAME_REQUIRE(0x2418e3d, "f30f108364020000f30f5f4318f30f5d0512d59002f30f1145c8f30f100da91a3403f30f5cc1e836000000909090")
FRAME_REQUIRE(0x2418e9e, "f30f590513000000f30f58c1f30f1105561a3403c3cccccccccccc9a99193e")

// FRAME_PLAY_TIME(group, address, the game's bytes): the play time's add (MoveMapStep::InGameUpdate:
// vmulss xmm0, xmm1, [1000.0]; vcvttss2si rcx, xmm0) made mov ecx, [slot]; nop7: a count of ours, the
// frame's milliseconds with the remainder carried. xmm1 (the step) is what the code after reads.
FRAME_PLAY_TIME(PlayTime, 0x1d419d6, "c5f259051267fe02c4e1fa2cc8")
