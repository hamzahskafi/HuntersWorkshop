#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Makes the "steps" group of gpu/shim/engine/frame_rate_sites.inc: the game's updates handed a fixed 1/30 s step in
a step-info (an FD4Time: its vtable 0x56efd40, the seconds at +8) built on the stack before the call, which Kyo's
"60 FPS++" list makes 1/60 (above 60 too) and these make read the frame's step (0x575a908).

Two shapes, both rewritten in place with the same bytes before and after the rewrite as far as the rest of the
function can tell (no branch lands inside one, no flag it set is read):
  lea R, [0x56efd30]; add R, 0x10; mov [rbp-A], R; mov dword [rbp-B], 1/30
    -> mov R32, [0x575a908]; mov [rbp-B], R32; lea R, [0x56efd40]; mov [rbp-A], R; nop
  (the vtable kept in r12 or r13 from an earlier one)
  mov rax, [rax+0xd0]; mov [rbp-A], R; mov dword [rbp-B], 1/30; lea rsi, [rbp-A]; mov rdi, r14; call rax
    -> nop; mov ecx, [0x575a908]; mov [rbp-B], ecx; mov [rbp-A], R; lea rsi, [rbp-A]; mov rdi, r14;
       call [rax+0xd0]   (the call ends where it did: the same return address)

Usage: frame_rate_steps.py eboot.elf [--check]
Prints the entries; --check compares them with the .inc's (the exit code is 1 when they differ). Needs capstone.
"""
import argparse
import re
import struct
import sys
from pathlib import Path

import capstone
from capstone import x86

ROOT = Path(__file__).resolve().parents[2]
INC = ROOT / 'gpu/shim/engine/frame_rate_sites.inc'
BASE_INC = ROOT / 'start/frame_rate_base.inc'
BASE = 0x400000
FD4TIME = 0x56efd30  # the step-info class; its vtable is 16 bytes in
FRAME_TIME = 0x575a908
THIRTIETH = 0x3d088889

# The sites (the address of the step's immediate, as the list has it), by what calls them.
STEPS = (
    ('a character action family, its +0x260 request run through its own update (vtable +0xd0)', (
        0x1ea4003, 0x1ea40a0, 0x1ea4146, 0x1ea49ce, 0x1ea4bda, 0x1ea5248, 0x1ea5476, 0x1ea552d, 0x1ea57c0,
        0x1ea5ac5, 0x1ea6015, 0x1ea6045, 0x1ea6275, 0x1ea62a5, 0x1ea632f)),
    ("the same through a sub-object's update (+0x108's vtable +0xd0), its +0x368 request", (
        0x1eb540b, 0x1eb54dc, 0x1eb90aa, 0x1eb9117, 0x1ebb111, 0x1ebf0d3, 0x1ec3f69, 0x1ec3f9a, 0x1ec3fd8,
        0x1ec4009, 0x1ec404e, 0x1ec40a4, 0x1ec40e0, 0x1ec4136, 0x1ec4167, 0x1ec41a5, 0x1ec41dc, 0x1f032f2,
        0x1f03762)),
    ('functors ticked until done (vtable +0x10; the Chalice dungeon sequences, SprjHolygrail, build some)', (
        0x1f6aa32, 0x200d8e2, 0x200e142, 0x200e2d4, 0x200fa52, 0x20119d2, 0x2012652, 0x20127cc, 0x2012932,
        0x2012ac4, 0x2012c32)),
    ("an event entity's first update", (0x16efe5c,)),
    ('a list update (sub_1373330)', (0x13734f3,)),
    ("the item list's screen update", (0x1f96d9a,)),
)


class Eboot:
    def __init__(self, path):
        self.file = Path(path).read_bytes()
        f = self.file
        phoff, = struct.unpack_from('<Q', f, 0x20)
        phnum, = struct.unpack_from('<H', f, 0x38)
        self.ph = [struct.unpack_from('<IIQQQQQQ', f, phoff + 56 * i) for i in range(phnum)]
        self.loads = [h for h in self.ph if h[0] == 1]
        self.funcs = self._funcs()
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        self.md.detail = True

    def read(self, address, n):
        a = address - BASE
        for _, _, off, va, _, fs, ms, _ in self.loads:
            if va <= a and a + n <= va + ms:
                s = a - va
                data = self.file[off + s:off + min(fs, s + n)] if s < fs else b''
                return data + bytes(n - len(data))
        return None

    def _funcs(self):
        """(start, end) of every function with an unwind entry (.eh_frame_hdr's table), patch-list addresses."""
        f = self.file
        hdr = next(h for h in self.ph if h[0] == 0x6474e550)
        va, off = hdr[3], hdr[2]
        if f[off] != 1 or f[off + 1] & 0x0f not in (3, 0xb) or f[off + 2] & 0x0f not in (3, 0xb) or f[off + 3] != 0x3b:
            raise SystemExit('an .eh_frame_hdr this does not read')
        count, = struct.unpack_from('<I', f, off + 8)
        out = []
        for i in range(count):
            loc, fde = struct.unpack_from('<ii', f, off + 12 + 8 * i)
            fde_va = va + fde
            seg = next(h for h in self.ph if h[0] in (1, 0x6474e550) and h[3] <= fde_va < h[3] + max(h[5], h[6]))
            begin, length = struct.unpack_from('<iI', f, fde_va - seg[3] + seg[2] + 8)
            if fde_va + 8 + begin == va + loc:
                out.append((va + loc + BASE, va + loc + length + BASE))
        return sorted(out)

    def function(self, address):
        return next(((s, e) for s, e in self.funcs if s <= address < e), None)

    def insns(self, start, end):
        return list(self.md.disasm(self.read(start, end - start), start))


def rip_target(ins):
    return next((ins.address + ins.size + op.mem.disp for op in ins.operands
                 if op.type == x86.X86_OP_MEM and op.mem.base == x86.X86_REG_RIP), None)


def rbp_store(ins, n):
    """`mov [rbp - d], ...` of n bytes: -d, else None."""
    if ins.mnemonic != 'mov' or len(ins.operands) != 2:
        return None
    dst = ins.operands[0]
    if dst.type != x86.X86_OP_MEM or dst.mem.base != x86.X86_REG_RBP or dst.mem.index or dst.size != n:
        return None
    return dst.mem.disp


REGS = {'rax': 0, 'rcx': 1, 'rdx': 2, 'rbx': 3, 'rsi': 6, 'rdi': 7, 'r8': 8, 'r9': 9, 'r10': 10, 'r11': 11,
        'r12': 12, 'r13': 13, 'r14': 14, 'r15': 15}


def rbp_operand(reg, disp):
    """ModRM (and displacement) for [rbp + disp] with `reg` in the reg field."""
    if -128 <= disp < 128:
        return bytes([0x45 | (reg & 7) << 3]) + struct.pack('<b', disp)
    return bytes([0x85 | (reg & 7) << 3]) + struct.pack('<i', disp)


def rip_load32(reg, at, target):
    """mov reg32, [rip + target] at `at`."""
    code = (b'\x44' if reg >= 8 else b'') + bytes([0x8b, 0x05 | (reg & 7) << 3])
    return code + struct.pack('<i', target - (at + len(code) + 4))


def store32(reg, disp):
    """mov [rbp + disp], reg32."""
    return (b'\x44' if reg >= 8 else b'') + b'\x89' + rbp_operand(reg, disp)


def rip_lea(reg, at, target):
    code = bytes([0x4c if reg >= 8 else 0x48, 0x8d, 0x05 | (reg & 7) << 3])
    return code + struct.pack('<i', target - (at + len(code) + 4))


NOPS = {0: b'', 1: b'\x90', 2: b'\x66\x90', 3: b'\x0f\x1f\x00'}


class Unfit(Exception):
    pass


def branch_targets(eboot, fn):
    out = set()
    for ins in eboot.insns(*fn):
        if ins.group(capstone.CS_GRP_JUMP) or ins.group(capstone.CS_GRP_CALL):
            op = ins.operands[0] if ins.operands else None
            if op is not None and op.type == x86.X86_OP_IMM:
                out.add(op.imm)
    return out


def flags_read_after(insns, i):
    """Whether an instruction from insns[i] on, up to the next call or flag write, reads a flag."""
    for ins in insns[i:]:
        if ins.group(capstone.CS_GRP_CALL) or ins.mnemonic in ('ret', 'jmp'):
            return False
        m = ins.mnemonic
        if (m.startswith('j') and m != 'jmp') or m.startswith(('set', 'cmov', 'adc', 'sbb')):
            return True
        if m in ('add', 'sub', 'cmp', 'test', 'and', 'or', 'xor', 'inc', 'dec', 'neg', 'shl', 'shr', 'sar',
                 'vucomiss', 'vcomiss', 'ucomiss', 'comiss', 'bt'):
            return False
    return False


def rewrite(eboot, at):
    """(start, the game's bytes, ours, the shape) for the site whose immediate is at `at`."""
    fn = eboot.function(at)
    if not fn:
        raise Unfit('not in a function')
    insns = eboot.insns(*fn)
    i = next((k for k, x in enumerate(insns) if x.address <= at < x.address + x.size), None)
    if i is None:
        raise Unfit('no instruction there')
    imm = insns[i]
    b = rbp_store(imm, 4)
    if b is None or imm.operands[1].type != x86.X86_OP_IMM or imm.operands[1].imm & 0xffffffff != THIRTIETH:
        raise Unfit(f'not mov dword [rbp-B], 1/30: {imm.mnemonic} {imm.op_str}')
    lea, add, store = insns[i - 3:i]
    a = rbp_store(store, 8)
    if a != b - 8:
        raise Unfit("the step-info's vtable is not stored right before")
    reg = store.operands[1].reg
    name = store.reg_name(reg)
    if lea.mnemonic == 'lea' and rip_target(lea) == FD4TIME and add.mnemonic == 'add' and \
            add.op_str == f'{name}, 0x10' and lea.op_str.startswith(f'{name},'):
        r = REGS[name]
        start, end = lea.address, imm.address + imm.size
        new = rip_load32(r, start, FRAME_TIME)
        new += store32(r, b)
        new += rip_lea(r, start + len(new), FD4TIME + 0x10)
        new += store.bytes
        shape = 'in place'
        after = i + 1
    else:
        # The vtable from an earlier one: mov rax, [rax+0xd0]; the stores; lea rsi, [rbp-A]; mov rdi, r14; call rax.
        load, (lea_rsi, mov_rdi, call) = insns[i - 2], insns[i + 1:i + 4]
        if name not in ('r12', 'r13') or load.mnemonic != 'mov' or load.op_str != 'rax, qword ptr [rax + 0xd0]' or \
                lea_rsi.mnemonic != 'lea' or lea_rsi.op_str != f'rsi, [rbp - {-a:#x}]' or \
                mov_rdi.mnemonic != 'mov' or not mov_rdi.op_str.startswith('rdi, ') or \
                call.mnemonic != 'call' or call.op_str != 'rax':
            raise Unfit('neither shape')
        start, end = load.address, call.address + call.size
        tail = store.bytes + lea_rsi.bytes + mov_rdi.bytes + b'\xff\x90\xd0\x00\x00\x00'  # call [rax+0xd0]
        body_len = 6 + len(store32(1, b)) + len(tail)
        pad = (end - start) - body_len
        if pad not in NOPS:
            raise Unfit(f'{pad} bytes to fill')
        new = NOPS[pad]
        new += rip_load32(1, start + len(new), FRAME_TIME)  # ecx: the call's arguments are rdi and rsi
        new += store32(1, b)
        new += tail
        shape = 'call folded'
        after = i + 4
    pad = (end - start) - len(new)
    if pad not in NOPS:
        raise Unfit(f'{pad} bytes to fill')
    if shape == 'in place':
        new += NOPS[pad]
    if len(new) != end - start:
        raise Unfit('lengths differ')
    inside = [t for t in branch_targets(eboot, fn) if start < t < end]
    if inside:
        raise Unfit(f'a branch lands inside: {", ".join(hex(t) for t in inside)}')
    if shape == 'in place' and flags_read_after(insns, after):
        raise Unfit("a flag the add set is read after")
    # What ours decodes to: the same loads and stores, reading the frame's step.
    ours = list(eboot.md.disasm(new, start))
    if sum(x.size for x in ours) != len(new):
        raise Unfit('ours does not decode')
    if not any(rip_target(x) == FRAME_TIME for x in ours):
        raise Unfit("ours does not read the frame's step")
    if shape == 'call folded' and (ours[-1].mnemonic != 'call' or ours[-1].address + ours[-1].size != end):
        raise Unfit('the call does not end where it did')
    return start, eboot.read(start, end - start), new, shape


def base_bytes():
    out = {}
    for at, now in re.findall(r'^FRAME_BASE\((0x[0-9a-f]+), "[0-9a-f]*", "([0-9a-f]*)"\)', BASE_INC.read_text(), re.M):
        for i, byte in enumerate(bytes.fromhex(now)):
            out[int(at, 16) + i] = byte
    return out


def entries(eboot):
    listed = base_bytes()
    lines = []
    for note, sites in STEPS:
        lines.append(f'// {note}.')
        for at in sites:
            try:
                start, was, new, shape = rewrite(eboot, at)
            except Unfit as e:
                raise Unfit(f'{at:#x}: {e}') from None
            over = bytes(listed.get(start + k, was[k]) for k in range(len(was)))
            lines.append(f'FRAME_SITE(Steps, {start:#x}, "{was.hex()}", "{over.hex() if over != was else ""}", '
                         f'"{new.hex()}")')
    return lines


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('eboot', help='a 1.09 eboot.elf (decrypted)')
    p.add_argument('--check', action='store_true', help="compare with the .inc's entries")
    a = p.parse_args()
    eboot = Eboot(a.eboot)
    try:
        lines = entries(eboot)
    except Unfit as e:
        print(f'a site does not fit: {e}')
        return 2
    if a.check:
        have = [l for l in INC.read_text().splitlines() if l.startswith('FRAME_SITE(Steps, ')]
        want = [l for l in lines if l.startswith('FRAME_SITE(')]
        print('OK' if have == want else f'{INC.name}: the steps differ from what the eboot makes')
        return 0 if have == want else 1
    print('\n'.join(lines))
    return 0


if __name__ == '__main__':
    sys.exit(main())
