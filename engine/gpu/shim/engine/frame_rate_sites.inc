// SPDX-License-Identifier: GPL-3.0-or-later
// The frame-rate corrections at 60 FPS and above (frame_rate.cpp): what Kyo's "60 FPS++" (1.0; the base at every
// rate, as our table start/frame_rate_base.inc holds it) leaves at the game's 30 FPS timing or gets wrong, and
// what the rates above 60 need on top of it. Adapted from bbhost (droogie/bbhost 566184a:
// src/engine/frame_rate.cpp, frame_rate_dt_loads.inc and tools/fps_sites.py), which also collects Kyo's 1.3
// lists and yogesh1239's high-FPS findings. Every entry of the list was read for 0.3.0 (docs/frame-rate-survey.md);
// what that added is ours: the steps, the steps objects keep, the pacer's first target, the lens filter's rate
// and the durations after the Havok AI's braking limit. Addresses are the patch lists' (the ELF's VA + 0x400000).
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
// The wind on grass, trees and whatever else sways (sub_2713870, from each model's draw), after droogie's bbhost
// (src/engine/wind_pace.h and frame_rate.cpp, b2818b4 and 5cee912). With the GX device's +0x100 at 1, as the
// game sets it, it moves each model's wind (its gusts' times, the point winds' lives and ramps: xmm4, then
// [rbp-0x170] and xmm10, t += step) by 1/60 s a frame, and weighs the force it uploads (WindForceAndTime.xyz =
// V x step, PointWindForce.x = speed x step: xmm14) by the same 1/60. At 30 that is 1/60 s of wind for each 1/30
// s of the game's. The list points the step at the frame's time: at 60 the wind ran twice the PS4's pace, and its
// force, weighed by each frame's length, bent half as far and shook with it. The step is half the frame's
// now (the load below) and the force is weighed by the game's 1/60 again. The other path (0x2713d9a: a model
// without wind slots) leaves before it moves anything.
// the step's load (vmovss xmm4, [0x4d29e70: 1/60]), which the list points at the frame's time: the game's back,
// for the load below to take.
FRAME_SITE(Grass, 0x2713d8d, "c5fa1025db606102", "c5fa1025736b0403", "c5fa1025db606102")
// the force's weight: vmovdqa xmm14, [rbp-0x170] (the step) made vmovss xmm14, [0x4d29e70] (1/60).
FRAME_SITE(Grass, 0x27149ea, "c5796fb590feffff", "", "c57a10357e546102")
// the wind slots' time took xmm14; it reads the step itself. Before: lea rax, [0x5940df9]; cmp byte [rax], 0;
// vxorps xmm10; mov r8d, [rbp-0x1b8]; jne +5; vmovdqa xmm10, xmm14. After: mov r8d, [rbp-0x1b8]; vxorps xmm10;
// cmp byte [0x5940df9], 0; jne +8; vmovdqa xmm10, [rbp-0x170]. rax is written before it is read on every path.
FRAME_SITE(Grass, 0x2714b26, "488d05ccc22203803800c4412857d2448b8548feffff7505c441796fd6", "", "448b8548feffffc4412857d2803dc0c22203007508c5796f9590feffff")
// The steps: updates the game hands a fixed 1/30 s step in a step-info (an FD4Time on the stack, its vtable
// 0x56efd40, the seconds at +8), which the list makes 1/60 at every rate; the step-info's seconds are read
// from the frame's step instead. Made by tools/re/frame_rate_steps.py, which says how each is rewritten (in
// place, or with the call folded into call [rax+0xd0] where the vtable comes from an earlier one) and checks
// that nothing else in the function can tell.
// a character action family, its +0x260 request run through its own update (vtable +0xd0).
FRAME_SITE(Steps, 0x1ea3ff1, "488d0d38bd84034883c11048894dd8c745e08988083d", "488d0d38bd84034883c11048894dd8c745e08988883c", "8b0d11698b03894de0488d0d3fbd840348894dd86690")
FRAME_SITE(Steps, 0x1ea408e, "488d0d9bbc84034883c11048894dd8c745e08988083d", "488d0d9bbc84034883c11048894dd8c745e08988883c", "8b0d74688b03894de0488d0da2bc840348894dd86690")
FRAME_SITE(Steps, 0x1ea4134, "488d0df5bb84034883c11048894dd8c745e08988083d", "488d0df5bb84034883c11048894dd8c745e08988883c", "8b0dce678b03894de0488d0dfcbb840348894dd86690")
FRAME_SITE(Steps, 0x1ea49b9, "488d0d70b384034883c11048898d78ffffffc745808988083d", "488d0d70b384034883c11048898d78ffffffc745808988883c", "8b0d495f8b03894d80488d0d77b3840348898d78ffffff6690")
FRAME_SITE(Steps, 0x1ea4bc8, "488d0d61b184034883c11048894dc0c745c88988083d", "488d0d61b184034883c11048894dc0c745c88988883c", "8b0d3a5d8b03894dc8488d0d68b1840348894dc06690")
FRAME_SITE(Steps, 0x1ea5236, "488d0df3aa84034883c11048894dc0c745c88988083d", "488d0df3aa84034883c11048894dc0c745c88988883c", "8b0dcc568b03894dc8488d0dfaaa840348894dc06690")
FRAME_SITE(Steps, 0x1ea5464, "488d0dc5a884034883c11048894dc0c745c88988083d", "488d0dc5a884034883c11048894dc0c745c88988883c", "8b0d9e548b03894dc8488d0dcca8840348894dc06690")
FRAME_SITE(Steps, 0x1ea551b, "488d0d0ea884034883c11048894dd8c745e08988083d", "488d0d0ea884034883c11048894dd8c745e08988883c", "8b0de7538b03894de0488d0d15a8840348894dd86690")
FRAME_SITE(Steps, 0x1ea57ae, "488d0d7ba584034883c11048894dd0c745d88988083d", "488d0d7ba584034883c11048894dd0c745d88988883c", "8b0d54518b03894dd8488d0d82a5840348894dd06690")
FRAME_SITE(Steps, 0x1ea5ab3, "488d0d76a284034883c11048894db0c745b88988083d", "488d0d76a284034883c11048894db0c745b88988883c", "8b0d4f4e8b03894db8488d0d7da2840348894db06690")
FRAME_SITE(Steps, 0x1ea6003, "488d0d269d84034883c11048894db0c745b88988083d", "488d0d269d84034883c11048894db0c745b88988883c", "8b0dff488b03894db8488d0d2d9d840348894db06690")
FRAME_SITE(Steps, 0x1ea6033, "488d0df69c84034883c11048894dc0c745c88988083d", "488d0df69c84034883c11048894dc0c745c88988883c", "8b0dcf488b03894dc8488d0dfd9c840348894dc06690")
FRAME_SITE(Steps, 0x1ea6263, "488d0dc69a84034883c11048894db0c745b88988083d", "488d0dc69a84034883c11048894db0c745b88988883c", "8b0d9f468b03894db8488d0dcd9a840348894db06690")
FRAME_SITE(Steps, 0x1ea6293, "488d0d969a84034883c11048894dc0c745c88988083d", "488d0d969a84034883c11048894dc0c745c88988883c", "8b0d6f468b03894dc8488d0d9d9a840348894dc06690")
FRAME_SITE(Steps, 0x1ea631d, "488d0d0c9a84034883c11048894dd8c745e08988083d", "488d0d0c9a84034883c11048894dd8c745e08988883c", "8b0de5458b03894de0488d0d139a840348894dd86690")
// the same through a sub-object's update (+0x108's vtable +0xd0), its +0x368 request.
FRAME_SITE(Steps, 0x1eb53f9, "488d0d30a983034883c11048894dd8c745e08988083d", "488d0d30a983034883c11048894dd8c745e08988883c", "8b0d09558a03894de0488d0d37a9830348894dd86690")
FRAME_SITE(Steps, 0x1eb54ca, "488d0d5fa883034883c11048894dd8c745e08988083d", "488d0d5fa883034883c11048894dd8c745e08988883c", "8b0d38548a03894de0488d0d66a8830348894dd86690")
FRAME_SITE(Steps, 0x1eb9098, "488d0d916c83034883c11048894dc0c745c88988083d", "488d0d916c83034883c11048894dc0c745c88988883c", "8b0d6a188a03894dc8488d0d986c830348894dc06690")
FRAME_SITE(Steps, 0x1eb9105, "488d0d246c83034883c11048894dd0c745d88988083d", "488d0d246c83034883c11048894dd0c745d88988883c", "8b0dfd178a03894dd8488d0d2b6c830348894dd06690")
FRAME_SITE(Steps, 0x1ebb0ff, "488d0d2a4c83034883c11048894dd8c745e08988083d", "488d0d2a4c83034883c11048894dd8c745e08988883c", "8b0d03f88903894de0488d0d314c830348894dd86690")
FRAME_SITE(Steps, 0x1ebf0c1, "488d0d680c83034883c11048894dd8c745e08988083d", "488d0d680c83034883c11048894dd8c745e08988883c", "8b0d41b88903894de0488d0d6f0c830348894dd86690")
FRAME_SITE(Steps, 0x1ec3f57, "4c8d2dd2bd82034983c5104c896d80c745888988083d", "4c8d2dd2bd82034983c5104c896d80c745888988883c", "448b2daa69890344896d884c8d2dd7bd82034c896d80")
FRAME_SITE(Steps, 0x1ec3f8c, "488b80d00000004c896da0c745a88988083d488d75a04c89f7ffd0", "488b80d00000004c896da0c745a88988883c488d75a04c89f7ffd0", "908b0d75698903894da84c896da0488d75a04c89f7ff90d0000000")
FRAME_SITE(Steps, 0x1ec3fca, "488b80d00000004c896da0c745a88988083d488d75a04c89f7ffd0", "488b80d00000004c896da0c745a88988883c488d75a04c89f7ffd0", "908b0d37698903894da84c896da0488d75a04c89f7ff90d0000000")
FRAME_SITE(Steps, 0x1ec3ffb, "488b80d00000004c896d90c745988988083d488d75904c89f7ffd0", "488b80d00000004c896d90c745988988883c488d75904c89f7ffd0", "908b0d06698903894d984c896d90488d75904c89f7ff90d0000000")
FRAME_SITE(Steps, 0x1ec403c, "488d0dedbc82034883c11048894da0c745a88988083d", "488d0dedbc82034883c11048894da0c745a88988883c", "8b0dc6688903894da8488d0df4bc820348894da06690")
FRAME_SITE(Steps, 0x1ec4092, "488d0d97bc82034883c11048894da0c745a88988083d", "488d0d97bc82034883c11048894da0c745a88988883c", "8b0d70688903894da8488d0d9ebc820348894da06690")
FRAME_SITE(Steps, 0x1ec40ce, "488d0d5bbc82034883c11048894db0c745b88988083d", "488d0d5bbc82034883c11048894db0c745b88988883c", "8b0d34688903894db8488d0d62bc820348894db06690")
FRAME_SITE(Steps, 0x1ec4124, "4c8d2505bc82034983c4104c8965c0c745c88988083d", "4c8d2505bc82034983c4104c8965c0c745c88988883c", "448b25dd678903448965c84c8d250abc82034c8965c0")
FRAME_SITE(Steps, 0x1ec4159, "488b80d00000004c8965a0c745a88988083d488d75a04c89f7ffd0", "488b80d00000004c8965a0c745a88988883c488d75a04c89f7ffd0", "908b0da8678903894da84c8965a0488d75a04c89f7ff90d0000000")
FRAME_SITE(Steps, 0x1ec4197, "488b80d00000004c8965a0c745a88988083d488d75a04c89f7ffd0", "488b80d00000004c8965a0c745a88988883c488d75a04c89f7ffd0", "908b0d6a678903894da84c8965a0488d75a04c89f7ff90d0000000")
FRAME_SITE(Steps, 0x1ec41c8, "488b80d00000004c89a570ffffffc78578ffffff8988083d488db570ffffff4c89f7ffd0", "488b80d00000004c89a570ffffffc78578ffffff8988883c488db570ffffff4c89f7ffd0", "908b0d39678903898d78ffffff4c89a570ffffff488db570ffffff4c89f7ff90d0000000")
FRAME_SITE(Steps, 0x1f032e0, "488d0d49ca7e034883c11048894dd0c745d88988083d", "488d0d49ca7e034883c11048894dd0c745d88988883c", "8b0d22768503894dd8488d0d50ca7e0348894dd06690")
FRAME_SITE(Steps, 0x1f03750, "488d0dd9c57e034883c11048894dd0c745d88988083d", "488d0dd9c57e034883c11048894dd0c745d88988883c", "8b0db2718503894dd8488d0de0c57e0348894dd06690")
// functors ticked until done (vtable +0x10; the Chalice dungeon sequences, SprjHolygrail, build some).
FRAME_SITE(Steps, 0x1f6aa20, "488d0d095378034883c11048894dd8c745e08988083d", "488d0d095378034883c11048894dd8c745e08988883c", "8b0de2fe7e03894de0488d0d1053780348894dd86690")
FRAME_SITE(Steps, 0x200d8d0, "488d0d59246e034883c11048894dd8c745e08988083d", "488d0d59246e034883c11048894dd8c745e08988883c", "8b0d32d07403894de0488d0d60246e0348894dd86690")
FRAME_SITE(Steps, 0x200e130, "488d0df91b6e034883c11048894dd8c745e08988083d", "488d0df91b6e034883c11048894dd8c745e08988883c", "8b0dd2c77403894de0488d0d001c6e0348894dd86690")
FRAME_SITE(Steps, 0x200e2c2, "488d0d671a6e034883c11048894dd8c745e08988083d", "488d0d671a6e034883c11048894dd8c745e08988883c", "8b0d40c67403894de0488d0d6e1a6e0348894dd86690")
FRAME_SITE(Steps, 0x200fa40, "488d0de9026e034883c11048894dd0c745d88988083d", "488d0de9026e034883c11048894dd0c745d88988883c", "8b0dc2ae7403894dd8488d0df0026e0348894dd06690")
FRAME_SITE(Steps, 0x20119c0, "488d0d69e36d034883c11048894dd0c745d88988083d", "488d0d69e36d034883c11048894dd0c745d88988883c", "8b0d428f7403894dd8488d0d70e36d0348894dd06690")
FRAME_SITE(Steps, 0x2012640, "488d0de9d66d034883c11048894dd8c745e08988083d", "488d0de9d66d034883c11048894dd8c745e08988883c", "8b0dc2827403894de0488d0df0d66d0348894dd86690")
FRAME_SITE(Steps, 0x20127ba, "488d0d6fd56d034883c11048894dd8c745e08988083d", "488d0d6fd56d034883c11048894dd8c745e08988883c", "8b0d48817403894de0488d0d76d56d0348894dd86690")
FRAME_SITE(Steps, 0x2012920, "488d0d09d46d034883c11048894dd8c745e08988083d", "488d0d09d46d034883c11048894dd8c745e08988883c", "8b0de27f7403894de0488d0d10d46d0348894dd86690")
FRAME_SITE(Steps, 0x2012ab2, "488d0d77d26d034883c11048894dd8c745e08988083d", "488d0d77d26d034883c11048894dd8c745e08988883c", "8b0d507e7403894de0488d0d7ed26d0348894dd86690")
FRAME_SITE(Steps, 0x2012c20, "488d0d09d16d034883c11048894dd8c745e08988083d", "488d0d09d16d034883c11048894dd8c745e08988883c", "8b0de27c7403894de0488d0d10d16d0348894dd86690")
// an event entity's first update.
FRAME_SITE(Steps, 0x16efe4a, "488d0ddffeff034883c11048894dc0c745c88988083d", "488d0ddffeff034883c11048894dc0c745c88988883c", "8b0db8aa0604894dc8488d0de6feff0348894dc06690")
// a list update (sub_1373330).
FRAME_SITE(Steps, 0x13734db, "488d054ec837044883c01048898560ffffffc78568ffffff8988083d", "488d054ec837044883c01048898560ffffffc78568ffffff8988883c", "8b0527743e04898568ffffff488d0552c8370448898560ffffff6690")
// the item list's screen update.
FRAME_SITE(Steps, 0x1f96d82, "488d05a78f75034883c01048898540feffffc78548feffff8988083d", "488d05a78f75034883c01048898540feffffc78548feffff8988883c", "8b05803b7c03898548feffff488d05ab8f750348898540feffff6690")
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
// the wind's step (sub_2713870, the Grass sites above): 1/60 a 30 fps frame, half the frame's at every rate.
FRAME_LOAD(Grass, 0x2713d8d, 8, 4, 0x4d29e70, Amount, 1, "c5fa1025db606102", 0x3c888889)
// the camera lens filter's exposure (vtable 0x56e7bc0, its shader constants): 30 x a / b and its inverse, a
// frame's worth at 30; the list's 60 is 60's.
FRAME_LOAD(Lens, 0x125d6e1, 8, 4, 0x4c9f358, Rate, 1, "c5fa10056f1ca403", 0x41f00000)

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
// shortest-duration floors, max(t, 1/30) (0x12e596a, 0x12e5a8a, 0x12e5baa), as 0x4cadc08's.
FRAME_VALUE(Durations, 0x4cad65c, Game, 0x3d088889, 0x3c888889)
FRAME_VALUE(Durations, 0x4cad660, Game, 0x3d088889, 0x3c888889)
FRAME_VALUE(Durations, 0x4cad664, Game, 0x3d088889, 0x3c888889)
// Havok rigid bodies' maximum linear speed, 30 m/s (an hkUFloat8 beside the maximum angular speed, 4 pi rad/s,
// and the damping, 0.2 and 0.1).
FRAME_VALUE(Durations, 0x13ef51c, Game, 0x41f00000, 0x42700000)
FRAME_VALUE(Durations, 0x13ef6ec, Game, 0x41f00000, 0x42700000)
// an online sequence's timeout (FrpgNetMan): a 30 s countdown by the step it is handed.
FRAME_VALUE(Durations, 0x203acb7, Game, 0x41f00000, 0x42700000)
// the install-progress poll (scePlayGoGetLocus) every 30 s: a static initializer's global.
FRAME_VALUE(Durations, 0x23dede6, Game, 0x41f00000, 0x42700000)
// the title screen's longest wait, 30 s (only its debug display reads it).
FRAME_VALUE(Durations, 0x1ab67ef, Game, 0x41f00000, 0x42700000)
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
// steps an object keeps (written when it is made, read each update): the rate's.
// an object's step at +0x70 (sub_a3fad0).
FRAME_VALUE(Steps, 0xa3fc3f, Step, 0x3d088889, 0x3c888889)
// a timed object's step at +0x80 until its first measured one (sub_12b5340).
FRAME_VALUE(Steps, 0x12b5458, Step, 0x3d088889, 0x3c888889)
// a timeline's step at +0xc8: it advances by it and counts its length in it (sub_1daa5b0).
FRAME_VALUE(Steps, 0x1daa65e, Step, 0x3d088889, 0x3c888889)
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

// FRAME_EASE(instruction, length, displacement offset, constant, the instruction's bytes, the game's weight, the
// 60 FPS++ list's): the follow camera's eases, a weight a frame read from a slot with the frame timer
// (BB_FRAME_TIMER=1), worked out from the frame's step every frame: the game's own a 30 FPS frame (Original, or
// BB_CAMERA_FIX=1), or with the FPS++ retune the list's a 60 FPS frame, so it eases as it does at 60. Without
// the timer the start program converts them once for the rate (patches.cpp).
// the distance ease (vmulss xmm1, xmm1, [0x4d25e68]): 0.1 a frame, the list's 0.025.
FRAME_EASE(0x183af8c, 8, 4, 0x4d25e68, "c5f2590dd4ae4e03", 0x3dcccccd, 0x3ccccccd)
// the chase-rate ease (vmovss xmm2, [0x4d25e94]; value += (target - value) * w): 1/30 a frame, the list's too.
FRAME_EASE(0x183e2b2, 8, 4, 0x4d25e94, "c5fa1015da7b4e03", 0x3d088889, 0x3d088889)

// FRAME_TARGET(address, the store up to its immediate, the game's value): the frame pacer's target, mov dword
// [r12+0x18], imm32 in the pacer (sub_2434770) and mov dword [r13+0x18], imm32 where the pacer is made
// (sub_2434520) (the 60 FPS++ list's 1/60): 1/rate, uncapped 1/its limit (at most 480). Without all of them the
// game stays at 60.
FRAME_TARGET(0x2434608, "41c74518", 0x3d088889)
FRAME_TARGET(0x2434840, "41c7442418", 0x3c888889)
FRAME_TARGET(0x243485a, "41c7442418", 0x3d088889)
FRAME_TARGET(0x243487e, "41c7442418", 0x3d088889)
FRAME_TARGET(0x24348b3, "41c7442418", 0x3d088889)

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
