#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Makes start/frame_rate_base.inc, the frame-rate base the Windows start program writes with the frame-rate
corrections on: Kyo's "60 FPS++" list for 1.09 (patches/Bloodborne.xml) as our own table, each entry with the
game's bytes from a 1.09 eboot.elf, so it no longer depends on the patch file. Three of the list's entries are
left out, as the start program has always dropped them: the effects' rate (2, scripts/patches.py
EFFECT_RATE_SITES) and the timed wait's bare ret (TIMED_WAIT).

Usage: frame_rate_base.py eboot.elf [--xml patches/Bloodborne.xml] [--check]
--check compares the file with what it would write; the exit code is 1 when they differ.
"""
import argparse
import struct
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'start/frame_rate_base.inc'
BASE = 0x400000
LEFT_OUT = ((0x26FB39F, 0x26FB3A3, 'effects'), (0x4CAD7FC, 0x4CAD800, 'effects'), (0x2483EC1, 0x2483EC6, 'timed wait'))
FRAME_TIME = 0x575a908
SIXTIETH, THIRTIETH = 0x3c888889, 0x3d088889

# Where each entry belongs, for the table's comments (bbhost src/engine/frame_rate.cpp says what each is).
CAVE = (0x2418e30, 0x2418ec0)
PACER = (0x2434770, 0x2435700)
CAMERA = (0x183a000, 0x1841000)
CAMERA_EASES = (0x4D25E68, 0x4D25E94)
NOTES = {
    0xb6e67a: "the cloth's substep count forced to 1: the part files were made at 30 FPS with 2 substeps, a rate of 60",
    0xb6fbd4: "the cloth's damping exponent read from another 1.0",
    0xfbc40f: "the motion blur's sample check",
    0x1cbc4fb: "a character's status effects: the first step read from the frame's step (the second: patches.cpp)",
    0x47df584: "a constant, 0.25",
    FRAME_TIME: "the frame's step, 1/60 until the cave first writes it",
}


def lines(xml, name='60 FPS++'):
    meta = next((m for m in ET.parse(xml).getroot().iter('Metadata') if m.get('Name') == name), None)
    if meta is None:
        raise SystemExit(f'no "{name}" in {xml}')
    for line in meta.iter('Line'):
        kind, value = line.get('Type').lower(), line.get('Value')
        data = (bytes.fromhex(value) if kind == 'bytes' else
                struct.pack({'bytes16': '<H', 'bytes32': '<I', 'bytes64': '<Q'}[kind], int(value, 0)))
        yield int(line.get('Address'), 16), data


class Eboot:
    def __init__(self, path):
        self.file = Path(path).read_bytes()
        phoff, = struct.unpack_from('<Q', self.file, 0x20)
        phnum, = struct.unpack_from('<H', self.file, 0x38)
        self.loads = [h for h in (struct.unpack_from('<IIQQQQQQ', self.file, phoff + 56 * i) for i in range(phnum))
                      if h[0] == 1]

    def read(self, address, n):
        a = address - BASE
        for _, _, off, va, _, fs, ms, _ in self.loads:
            if va <= a and a + n <= va + ms:
                s = a - va
                data = self.file[off + s:off + min(fs, s + n)] if s < fs else b''
                return data + bytes(n - len(data))
        return None


RANGES = (
    (0x21bc181, 0x21bc1a2, "a fixed step multiplied by the frame's step where 0x21bc181 had one"),
    (0x21bc308, 0x21bc310, 'the same: a jump back into it'),
    (0x3abc000, 0x3abd000, 'table bytes for 60'),
    (0x575a920, 0x575a938, "the frame modes' names for a debug display: absolute pointers, which the loader rebases"),
    (0x2c01180, 0x2c012cc, "the sound loop's profiling clock reads skipped"),
    (0x2c012cc, 0x2c012de, "the sound thread's sleep capped at 16 ms, a cave in that loop (frame_rate.cpp puts the game's "
                           "call back and sets the thread's update time)"),
    (0x2c0137d, 0x2c01382, 'the call into that cap'),
)
COUNTS = {0x192c405: 'a countdown of 10 frames', 0x1929676: 'the default of 5 an int setting is read with',
          0x25629f0: "a worker's usleep(33000) in a polling loop"}


def category(eboot, at, was, now):
    if CAVE[0] <= at < CAVE[1]:
        return ("the frame's step: the measured frame time kept in [target, 1/30] for the game's task step, and eased "
                "by 0.15 a frame into 0x575a908 for what reads it (frame_rate.cpp's timer replaces both)")
    if PACER[0] <= at < PACER[1]:
        return ("the frame pacer (sub_2434770): mode 2, one vblank a frame, its target 1/60 (frame_rate.cpp sets "
                "the rate's), no catch-up allowance")
    if CAMERA[0] <= at < CAMERA[1] or at in CAMERA_EASES:
        return "Kyo's retune of the follow camera (FPS++ retune; Original leaves it out, patches.cpp)"
    if at in NOTES:
        return NOTES[at]
    for lo, hi, note in RANGES:
        if lo <= at < hi:
            return note
    if at in COUNTS:
        return f'{COUNTS[at]}, for 60 (frame_rate.cpp sets the rate\'s)'
    if len(now) == 4:
        old, value = struct.unpack('<I', was)[0], struct.unpack('<I', now)[0]
        if old == THIRTIETH and value == SIXTIETH:
            return "a 1/30 s step made 1/60 (above 60 too, as Kyo's 90 and 120 lists hold them)"
        if (old, value) == (0x41f00000, 0x42700000):
            return "30 a second made 60"
    if len(now) % 4 == 0 and all(0 < abs(v) < 10 for v in struct.unpack(f'<{len(now) // 4}f', now)) and \
            all(0 < abs(v) < 10 for v in struct.unpack(f'<{len(was) // 4}f', was)):
        return 'a value a frame, for a 60 FPS frame'
    if len(now) in (3, 4):
        # A rip displacement: the instruction ends after it, the rest of it the game's.
        disp = struct.unpack('<i', now + eboot.read(at + len(now), 4 - len(now)))[0]
        if at + 4 + disp == FRAME_TIME:
            return "a load of a 1/30 step pointed at the frame's step (0x575a908)"
        return "a load pointed at another constant: Kyo's value for 60"
    return 'code and table bytes for 60'


def build(eboot, xml):
    entries, left = [], []
    for at, now in lines(xml):
        if any(lo <= at < hi for lo, hi, _ in LEFT_OUT):
            left.append(at)
            continue
        was = eboot.read(at, len(now))
        if was is None:
            raise SystemExit(f'{at:#x}: not in the eboot')
        entries.append((at, was, now))
    out = ['// SPDX-License-Identifier: GPL-3.0-or-later',
           "// The frame-rate base (start/patches.cpp, the frame-rate corrections on): Kyo's \"60 FPS++\" list for 1.09 as",
           '// our own table, made by tools/re/frame_rate_base.py from patches/Bloodborne.xml and the 1.09 eboot; credit to',
           "// Kyo for the findings, bbhost (droogie/bbhost, src/engine/frame_rate.cpp) for what each one is. FRAME_BASE(address,",
           "// the game's bytes, ours), in the list's order; the start program checks every entry's game bytes and leaves",
           '// the whole table out on any difference. Left out: the effects\' rate (0x26fb39f, 0x4cad7fc: the data\'s 30) and',
           "// the timed wait's bare ret (0x2483ec1). The engine's frame_rate.cpp corrects and extends it (frame_rate_sites.inc).",
           '']
    last = None
    for at, was, now in entries:
        c = category(eboot, at, was, now)
        if c != last:
            out.append(f'// {c}.')
            last = c
        out.append(f'FRAME_BASE({at:#x}, "{was.hex()}", "{now.hex()}")')
    out.append(f'// {len(entries)} entries, {sum(len(n) for _, _, n in entries)} bytes')
    return '\n'.join(out) + '\n', left


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('eboot')
    p.add_argument('--xml', default=str(ROOT / 'patches/Bloodborne.xml'))
    p.add_argument('--check', action='store_true')
    a = p.parse_args()
    text, left = build(Eboot(a.eboot), a.xml)
    if a.check:
        same = OUT.is_file() and OUT.read_text() == text
        print('OK' if same else f'{OUT} differs from what the list and the eboot make')
        return 0 if same else 1
    OUT.write_text(text)
    print(f'{OUT}: {text.count(chr(10) + "FRAME_BASE(")} entries; left out {", ".join(hex(a) for a in left)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
