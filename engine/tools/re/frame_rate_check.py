#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks the frame-rate corrections (gpu/shim/engine/frame_rate_sites.inc) against the 60 FPS++ list in
patches/Bloodborne.xml and, given one, a 1.09 eboot.elf.

Usage: frame_rate_check.py [eboot.elf] [--xml patches/Bloodborne.xml]

Without the eboot: the entries' shapes, that none overlaps another, that the list's bytes recorded for each
entry are the list's (the frame pacer's targets 1/60, the counts 60's), that the list's frame-time code is as
required and leaves every per-frame load alone, that each load a site writes is in that site's bytes, and that
the slots fit past the end of 1.09's data segment, in its last page. With it, also: the game's bytes and values
at every entry, and that each load's displacement reaches its constant. Prints each failure; the exit code is
their number.
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
DATA_END = 0x56d30f4  # 1.09: the end of its data segment (frame_rate.cpp's kDataEnd; the slots follow it)
GROUPS = ('Clips', 'Physics', 'Grass', 'Targeting', 'Jump', 'Sprint', 'Slope', 'Online', 'Hitbox', 'Timers', 'Menus',
          'Durations', 'PlayTime', 'Sound', 'Cloth', 'Steps', 'Lens')
KINDS = ('Step', 'Amount', 'Decay', 'Keep', 'Rate', 'Game', 'Cloth')
COUNTS = ('Frames', 'Micros', 'Digits')
SIXTIETH = 0x3c888889
GAME_BYTES = "the game's bytes"


def parse(text):
    """The .inc's entries, as dicts."""
    out = {'sites': [], 'loads': [], 'values': [], 'counts': [], 'targets': [], 'cloth_scales': [], 'eases': [],
           'require': [], 'play': []}
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
    for m in re.finditer(r'^FRAME_COUNT\((\w+), (0x[0-9a-f]+), (\d), (\d+), (\w+)\)$', text, re.M):
        out['counts'].append(dict(group=m[1], at=int(m[2], 16), size=int(m[3]), game=int(m[4]), kind=m[5]))
    for m in re.finditer(r'^FRAME_TARGET\((0x[0-9a-f]+), "([0-9a-f]+)", (0x[0-9a-f]+)\)$', text, re.M):
        out['targets'].append(dict(at=int(m[1], 16), store=bytes.fromhex(m[2]), game=int(m[3], 16)))
    for m in re.finditer(r'^FRAME_CLOTH_SCALE\((0x[0-9a-f]+), "([0-9a-f]+)"\)$', text, re.M):
        out['cloth_scales'].append(dict(at=int(m[1], 16), was=bytes.fromhex(m[2])))
    for m in re.finditer(r'^FRAME_EASE\((0x[0-9a-f]+), (\d+), (\d+), (0x[0-9a-f]+), "([0-9a-f]+)", (0x[0-9a-f]+), '
                         r'(0x[0-9a-f]+)\)$', text, re.M):
        out['eases'].append(dict(insn=int(m[1], 16), length=int(m[2]), disp=int(m[3]), constant=int(m[4], 16),
                                 bytes=bytes.fromhex(m[5]), game=int(m[6], 16), listed=int(m[7], 16)))
    for m in re.finditer(r'^FRAME_REQUIRE\((0x[0-9a-f]+), "([0-9a-f]+)"\)$', text, re.M):
        out['require'].append(dict(at=int(m[1], 16), bytes=bytes.fromhex(m[2])))
    for m in re.finditer(r'^FRAME_PLAY_TIME\((\w+), (0x[0-9a-f]+), "([0-9a-f]+)"\)$', text, re.M):
        out['play'].append(dict(group=m[1], at=int(m[2], 16), was=bytes.fromhex(m[3])))
    return out


def count_bytes(c, rate):
    """A count's bytes at `rate`, as frame_rate.cpp writes it (30: the game's own)."""
    n = c['game']
    if c['kind'] == 'Frames':
        n = round(c['game'] * rate / 30)
    elif c['kind'] == 'Micros':
        n = round(c['game'] * 30 / rate)
    elif rate != 30:
        n = int(1e6 // rate)
    if c['kind'] == 'Digits':
        return f'{n:0{c["size"]}d}'.encode()
    return n.to_bytes(c['size'], 'little')


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
        # An instruction one of its group's sites writes (a cave's): in that site's bytes, not the game's.
        site = next((s for s in entries['sites'] if s['group'] == l['group'] and
                     s['at'] <= l['insn'] < s['at'] + len(s['now'])), None)
        if site and site['now'][l['insn'] - site['at']:l['insn'] - site['at'] + l['length']] != l['bytes']:
            fail(f'load {l["insn"]:#x}: not what its site {site["at"]:#x} writes')
        if eboot:
            if not site and eboot.read(l['insn'], l['length']) != l['bytes']:
                fail(f"load {l['insn']:#x}: the game's instruction differs")
            if eboot.read(l['constant'], 4 * l['lanes']) != struct.pack(f'<{l["lanes"]}I', *l['value']):
                fail(f"load {l['insn']:#x}: the game's value at {l['constant']:#x} differs")
        if not site:
            spans.append((l['insn'] + l['disp'], l['insn'] + l['disp'] + 4, f'load {l["insn"]:#x}'))
    for c in entries['counts']:
        if c['group'] not in GROUPS or c['kind'] not in COUNTS:
            fail(f'count {c["at"]:#x}: group or kind')
        game = count_bytes(c, 30)
        if eboot and eboot.read(c['at'], c['size']) != game:
            fail(f"count {c['at']:#x}: the game's value is not {c['game']}")
        over = list_over(c['at'], c['size'], game)
        expect = game if c['kind'] == 'Digits' else count_bytes(c, 60)
        if (over or game) != expect:
            fail(f'count {c["at"]:#x}: the list leaves {over.hex() if over else GAME_BYTES}, not {expect.hex()}')
        spans.append((c['at'], c['at'] + c['size'], f'count {c["at"]:#x}'))
    for t in entries['targets']:
        n = len(t['store']) + 4
        if t['store'][:2] != b'\x41\xc7' or t['store'][-1:] != b'\x18':
            fail(f'target {t["at"]:#x}: not mov dword [r12/r13+0x18], imm32')
        game = t['store'] + struct.pack('<I', t['game'])
        if eboot and eboot.read(t['at'], n) != game:
            fail(f"target {t['at']:#x}: the game's bytes differ")
        if list_over(t['at'], n, game) != t['store'] + struct.pack('<I', SIXTIETH):
            fail(f'target {t["at"]:#x}: the list does not set 1/60 there')
        spans.append((t['at'], t['at'] + n, f'target {t["at"]:#x}'))
    for c in entries['cloth_scales']:
        if len(c['was']) != 16:
            fail(f'cloth scale {c["at"]:#x}: not four lanes')
        if eboot and eboot.read(c['at'], len(c['was'])) != c['was']:
            fail(f"cloth scale {c['at']:#x}: the game's bytes differ")
        if list_over(c['at'], len(c['was'])):
            fail(f'cloth scale {c["at"]:#x}: the list writes there')
        if any(l['constant'] != c['at'] for l in entries['loads'] if l['group'] == 'Cloth'):
            fail(f'cloth scale {c["at"]:#x}: a cloth load reads elsewhere')
        spans.append((c['at'], c['at'] + len(c['was']), f'cloth scale {c["at"]:#x}'))
    for e in entries['eases']:
        if len(e['bytes']) != e['length'] or e['disp'] + 4 > e['length']:
            fail(f'ease {e["insn"]:#x}: length')
        target = e['insn'] + e['length'] + struct.unpack_from('<i', e['bytes'], e['disp'])[0]
        if target != e['constant']:
            fail(f'ease {e["insn"]:#x}: reads {target:#x}, not {e["constant"]:#x}')
        if list_over(e['insn'], e['length']):
            fail(f'ease {e["insn"]:#x}: the list writes into the instruction')
        game = struct.pack('<I', e['game'])
        if (list_over(e['constant'], 4, game) or game) != struct.pack('<I', e['listed']):
            fail(f'ease {e["insn"]:#x}: the list does not leave {e["listed"]:#010x} at {e["constant"]:#x}')
        if eboot:
            if eboot.read(e['insn'], e['length']) != e['bytes']:
                fail(f"ease {e['insn']:#x}: the game's instruction differs")
            if eboot.read(e['constant'], 4) != game:
                fail(f"ease {e['insn']:#x}: the game's value at {e['constant']:#x} differs")
        spans.append((e['insn'] + e['disp'], e['insn'] + e['disp'] + 4, f'ease {e["insn"]:#x}'))
    for r in entries['require']:
        over = list_over(r['at'], len(r['bytes']))
        if over != r['bytes']:
            fail(f'required {r["at"]:#x}: the list leaves {over.hex() if over else GAME_BYTES}')
    spans.sort()
    for (a0, a1, n0), (b0, b1, n1) in zip(spans, spans[1:]):
        if b0 < a1:
            fail(f'{n0} and {n1} overlap')
    cpp = re.search(r'kDataEnd = (0x[0-9a-f]+)', (ROOT / 'gpu/shim/engine/frame_rate.cpp').read_text())
    if not cpp or int(cpp[1], 16) != DATA_END:
        fail(f'frame_rate.cpp kDataEnd is not {DATA_END:#x}')
    if eboot and eboot.size != DATA_END:
        fail(f"the eboot's data segment ends at {eboot.size:#x}, not {DATA_END:#x}")
    size = DATA_END
    slots = len(entries['loads']) + len(entries['play']) + 1 + len(entries['eases'])  # the timer's step too
    first, end = (size + 15) & ~15, (size + 4095) & ~4095
    if first + 16 * slots > end:
        fail(f'{slots} slots of 16 bytes do not fit in the {end - size} bytes past the game\'s data')
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
