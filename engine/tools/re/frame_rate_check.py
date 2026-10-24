#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks the frame-rate corrections (gpu/shim/engine/frame_rate_sites.inc) against the 60 FPS++ list in
patches/Bloodborne.xml and, given one, a 1.09 eboot.elf.

Usage: frame_rate_check.py [eboot.elf] [--xml patches/Bloodborne.xml]

Without the eboot: the entries' shapes, that none overlaps another, that the list's bytes recorded for each
entry are the list's, that the list's frame-time code is as required and leaves every per-frame load alone,
and that the slots fit past 1.09's image. With it, also: the game's bytes and values at every entry, and that
each load's displacement reaches its constant. Prints each failure; the exit code is their number.
"""
import argparse
import re
import struct
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
INC = ROOT / 'gpu/shim/engine/frame_rate_sites.inc'
BASE = 0x400000
IMAGE_SIZE = 0x56d30f4  # 1.09's image: the end of its data segment (frame_rate.cpp's slots follow it)
GROUPS = ('Clips', 'Physics', 'Grass', 'Targeting', 'Jump', 'Sprint', 'Slope', 'Online', 'Hitbox', 'Timers', 'Menus',
          'Durations', 'PlayTime')
KINDS = ('Step', 'Amount', 'Decay', 'Keep', 'Rate', 'Game')
GAME_BYTES = "the game's bytes"


def parse(text):
    """The .inc's entries, as dicts."""
    out = {'sites': [], 'loads': [], 'values': [], 'require': [], 'play': []}
    for m in re.finditer(r'^FRAME_SITE\((\w+), (0x[0-9a-f]+), "([0-9a-f]*)", "([0-9a-f]*)", "([0-9a-f]*)"\)$', text, re.M):
        out['sites'].append(dict(group=m[1], at=int(m[2], 16), was=bytes.fromhex(m[3]), listed=bytes.fromhex(m[4]),
                                 now=bytes.fromhex(m[5])))
    for m in re.finditer(r'^FRAME_LOAD\((\w+), (0x[0-9a-f]+), (\d+), (\d+), (0x[0-9a-f]+), (\w+), (\d), "([0-9a-f]+)", '
                         r'([0-9a-fx, ]+)\)$', text, re.M):
        out['loads'].append(dict(group=m[1], insn=int(m[2], 16), length=int(m[3]), disp=int(m[4]), constant=int(m[5], 16),
                                 kind=m[6], lanes=int(m[7]), bytes=bytes.fromhex(m[8]),
                                 value=[int(v, 16) for v in m[9].split(',')]))
    for m in re.finditer(r'^FRAME_VALUE\((\w+), (0x[0-9a-f]+), (\w+), (0x[0-9a-f]+), (0x[0-9a-f]+)\)$', text, re.M):
        out['values'].append(dict(group=m[1], at=int(m[2], 16), kind=m[3], game=int(m[4], 16), listed=int(m[5], 16)))
    for m in re.finditer(r'^FRAME_REQUIRE\((0x[0-9a-f]+), "([0-9a-f]+)"\)$', text, re.M):
        out['require'].append(dict(at=int(m[1], 16), bytes=bytes.fromhex(m[2])))
    for m in re.finditer(r'^FRAME_PLAY_TIME\((\w+), (0x[0-9a-f]+), "([0-9a-f]+)"\)$', text, re.M):
        out['play'].append(dict(group=m[1], at=int(m[2], 16), was=bytes.fromhex(m[3])))
    return out


def list_bytes(xml, name='60 FPS++'):
    """address -> byte for every byte the list writes."""
    meta = next((m for m in ET.parse(xml).getroot().iter('Metadata') if m.get('Name') == name), None)
    if meta is None:
        raise SystemExit(f'no "{name}" in {xml}')
    out = {}
    for line in meta.iter('Line'):
        kind, value = line.get('Type').lower(), line.get('Value')
        data = (bytes.fromhex(value) if kind == 'bytes' else
                struct.pack({'bytes16': '<H', 'bytes32': '<I', 'bytes64': '<Q'}[kind], int(value, 0)))
        for i, b in enumerate(data):
            out[int(line.get('Address'), 16) + i] = b
    return out


class Eboot:
    def __init__(self, path):
        self.file = Path(path).read_bytes()
        phoff, = struct.unpack_from('<Q', self.file, 0x20)
        phnum, = struct.unpack_from('<H', self.file, 0x38)
        self.loads = [h for h in (struct.unpack_from('<IIQQQQQQ', self.file, phoff + 56 * i) for i in range(phnum))
                      if h[0] == 1]
        self.size = max(va + ms for _, _, _, va, _, _, ms, _ in self.loads)

    def read(self, address, n):
        a = address - BASE
        for _, _, off, va, _, fs, ms, _ in self.loads:
            if va <= a and a + n <= va + ms:
                s = a - va
                data = self.file[off + s:off + min(fs, s + n)] if s < fs else b''
                return data + bytes(n - len(data))
        return None


def check(entries, listed, eboot=None):
    failures = []
    fail = failures.append

    def list_over(address, n, game=None):
        """The list's bytes over [address, address + n), b'' when it leaves them; None when it changes only
        some and the game's bytes are not known."""
        touched = [address + i in listed for i in range(n)]
        if not any(touched):
            return b''
        if all(touched):
            return bytes(listed[address + i] for i in range(n))
        return None if game is None else bytes(listed.get(address + i, game[i]) for i in range(n))

    spans = []
    for s in entries['sites']:
        if s['group'] not in GROUPS:
            fail(f'site {s["at"]:#x}: no group {s["group"]}')
        if not s['was'] or len(s['was']) != len(s['now']) or (s['listed'] and len(s['listed']) != len(s['was'])):
            fail(f'site {s["at"]:#x}: lengths differ')
        game = eboot.read(s['at'], len(s['was'])) if eboot else None
        if eboot and game != s['was']:
            fail(f'site {s["at"]:#x}: the game has {game.hex() if game else "nothing"} there')
        over = list_over(s['at'], len(s['was']), game)
        if over is not None and over != s['listed']:
            fail(f'site {s["at"]:#x}: the list leaves {over.hex() or GAME_BYTES}, recorded {s["listed"].hex() or "none"}')
        spans.append((s['at'], s['at'] + len(s['was']), f'site {s["at"]:#x}'))
    for v in entries['values']:
        if v['group'] not in GROUPS or v['kind'] not in KINDS:
            fail(f'value {v["at"]:#x}: group or kind')
        if eboot and eboot.read(v['at'], 4) != struct.pack('<I', v['game']):
            fail(f"value {v['at']:#x}: the game's value is not {v['game']:#010x}")
        over = list_over(v['at'], 4, struct.pack('<I', v['game']))
        if (over or struct.pack('<I', v['game'])) != struct.pack('<I', v['listed']):
            fail(f'value {v["at"]:#x}: the list leaves {over.hex() if over else GAME_BYTES}, recorded {v["listed"]:#010x}')
        spans.append((v['at'], v['at'] + 4, f'value {v["at"]:#x}'))
    for p in entries['play']:
        if eboot and eboot.read(p['at'], len(p['was'])) != p['was']:
            fail(f"play time {p['at']:#x}: the game's bytes differ")
        if list_over(p['at'], len(p['was'])):
            fail(f'play time {p["at"]:#x}: the list writes there')
        spans.append((p['at'], p['at'] + len(p['was']), f'play time {p["at"]:#x}'))
    for l in entries['loads']:
        if l['group'] not in GROUPS or l['kind'] not in KINDS or len(l['value']) != l['lanes'] or l['lanes'] not in (1, 2, 4):
            fail(f'load {l["insn"]:#x}: group, kind or lanes')
        if len(l['bytes']) != l['length'] or l['disp'] + 4 > l['length']:
            fail(f'load {l["insn"]:#x}: length')
        target = l['insn'] + l['length'] + struct.unpack_from('<i', l['bytes'], l['disp'])[0]
        if target != l['constant']:
            fail(f'load {l["insn"]:#x}: reads {target:#x}, not {l["constant"]:#x}')
        if list_over(l['insn'], l['length']):
            fail(f'load {l["insn"]:#x}: the list writes into the instruction')
        if eboot:
            if eboot.read(l['insn'], l['length']) != l['bytes']:
                fail(f"load {l['insn']:#x}: the game's instruction differs")
            if eboot.read(l['constant'], 4 * l['lanes']) != struct.pack(f'<{l["lanes"]}I', *l['value']):
                fail(f"load {l['insn']:#x}: the game's value at {l['constant']:#x} differs")
        spans.append((l['insn'] + l['disp'], l['insn'] + l['disp'] + 4, f'load {l["insn"]:#x}'))
    for r in entries['require']:
        over = list_over(r['at'], len(r['bytes']))
        if over != r['bytes']:
            fail(f'required {r["at"]:#x}: the list leaves {over.hex() if over else GAME_BYTES}')
    spans.sort()
    for (a0, a1, n0), (b0, b1, n1) in zip(spans, spans[1:]):
        if b0 < a1:
            fail(f'{n0} and {n1} overlap')
    size = eboot.size if eboot else IMAGE_SIZE
    slots = len(entries['loads']) + len(entries['play'])
    first, end = (size + 15) & ~15, (size + 4095) & ~4095
    if first + 16 * slots > end:
        fail(f'{slots} slots of 16 bytes do not fit in the {end - size} bytes past the image')
    return failures


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('eboot', nargs='?', help='a 1.09 eboot.elf (decrypted)')
    p.add_argument('--xml', default=str(ROOT / 'patches/Bloodborne.xml'))
    a = p.parse_args()
    entries = parse(INC.read_text())
    eboot = Eboot(a.eboot) if a.eboot else None
    failures = check(entries, list_bytes(a.xml), eboot)
    for f in failures:
        print('FAIL', f)
    counts = ', '.join(f'{len(v)} {k}' for k, v in entries.items())
    print(f'{"FAILED" if failures else "OK"}: {counts}' + ('' if eboot else " (no eboot: the game's bytes not checked)"))
    return len(failures)


if __name__ == '__main__':
    sys.exit(main())
