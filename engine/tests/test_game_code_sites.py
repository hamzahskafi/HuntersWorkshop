"""The game code the engine's hooks expect (crash_guards.h, frame_rate.cpp's cloth factor) against the verified
1.09 eboot.elf in out/ (skipped without it), and the byte arrays parsed from the sources."""
import re
import struct
import unittest

from paths import ROOT

HEADER = ROOT / 'gpu/shim/engine/crash_guards.h'
FRAME_RATE = ROOT / 'gpu/shim/engine/frame_rate.cpp'


def arrays(text):
    """constexpr std::uint8_t kName[] = {0x.., ...}; in a source: name -> bytes."""
    out = {}
    for name, body in re.findall(r'constexpr std::uint8_t (k\w+)\[\]\s*=\s*\{([^}]*)\}', text):
        out[name] = bytes(int(v, 16) for v in re.findall(r'0x[0-9a-fA-F]+', body))
    return out


def constants(text):
    """constexpr std::size_t / std::uint64_t kName = 0x..., possibly several on a line."""
    out = {}
    for decl in re.findall(r'constexpr std::(?:size_t|uint64_t) ([^;]*);', text):
        for name, value in re.findall(r'(k\w+)\s*=\s*(0x[0-9a-fA-F]+)', decl):
            out[name] = int(value, 16)
    return out


def read_image(elf, offset, n):
    """n bytes at an image offset (the virtual address the executable gives it)."""
    phoff, = struct.unpack_from('<Q', elf, 0x20)
    phentsize, phnum = struct.unpack_from('<HH', elf, 0x36)
    for i in range(phnum):
        kind, _, file_offset, vaddr, _, filesz, _, _ = struct.unpack_from('<IIQQQQQQ', elf, phoff + i * phentsize)
        if kind == 1 and vaddr <= offset and offset + n <= vaddr + filesz:
            start = file_offset + offset - vaddr
            return elf[start:start + n]
    return None


class GameCodeSites(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        header = HEADER.read_text(encoding='utf-8')
        frame_rate = FRAME_RATE.read_text(encoding='utf-8')
        cls.bytes = arrays(header) | arrays(frame_rate)
        cls.at = constants(header) | constants(frame_rate)
        # (image offset, bytes): crash_guards.h's are image offsets, frame_rate.cpp's patch addresses.
        cls.sites = {
            'hit push': (cls.at['kHitPush'], cls.bytes['kHitPushCode']),
            'query create': (cls.at['kQueryCreate'], cls.bytes['kQueryCreateEntry']),
            'query begin': (cls.at['kQueryBegin'], cls.bytes['kQueryBeginCode']),
            'query end': (cls.at['kQueryEnd'], cls.bytes['kQueryEndCode']),
            'cloth factor': (cls.at['kClothFactor'] - 0x400000, cls.bytes['kClothFactorPrologue']),
        }

    def test_the_sources_parse(self):
        # A hook overwrites 14 bytes at least; begin and end are replaced whole.
        for name, (_, code) in self.sites.items():
            self.assertGreaterEqual(len(code), 14, name)
        # The hit push: the cave's jump takes its grow's three instructions; the lea's target is the allocator, the
        # call is Havok's grow (0x819100 in the patch lists), and the cave's return is the push's epilogue.
        push, at = self.bytes['kHitPushCode'], self.at['kHitPush']
        grow, call, ret = self.at['kHitGrow'] - at, self.at['kHitGrowCall'] - at, self.at['kHitReturn'] - at
        self.assertEqual(push[grow:grow + 4], bytes.fromhex('498d7620'))
        self.assertEqual(push[grow + 4:grow + 7], bytes.fromhex('488d3d'))
        lea, = struct.unpack_from('<i', push, grow + 7)
        self.assertEqual(at + grow + 11 + lea, self.at['kHitAllocator'])
        self.assertEqual(push[grow + 11:call], bytes.fromhex('ba60000000'))
        rel, = struct.unpack_from('<i', push, call + 1)
        self.assertEqual((push[call], at + call + 5 + rel), (0xe8, 0x819100 - 0x400000))
        self.assertEqual(push[ret:], bytes.fromhex('5b415e5dc3'))
        # Begin and end whole: each ends mov al, 1; pop rbp; ret.
        for name in ('kQueryBeginCode', 'kQueryEndCode'):
            self.assertEqual(self.bytes[name][-4:], bytes.fromhex('b0015dc3'), name)
        # Their calls land on the device's begin and end.
        for code, start, target in ((self.bytes['kQueryBeginCode'], self.at['kQueryBegin'], self.at['kDeviceBegin']),
                                    (self.bytes['kQueryEndCode'], self.at['kQueryEnd'], self.at['kDeviceEnd'])):
            call = code.index(b'\xe8')
            rel, = struct.unpack_from('<i', code, call + 1)
            self.assertEqual(start + call + 5 + rel, target)

    def test_the_executable_holds_them(self):
        elf_path = ROOT / 'out/eboot.elf'
        if not elf_path.is_file():
            self.skipTest('no prepared eboot.elf')
        elf = elf_path.read_bytes()
        from engine_assets import EBOOT_SHA, sha
        if sha(elf) not in EBOOT_SHA:
            self.skipTest('out/eboot.elf is not the verified 1.09 executable')
        for name, (offset, code) in self.sites.items():
            self.assertEqual(read_image(elf, offset, len(code)), code, name)
        # The cloth's one substep is the 60 FPS++ list's write over the game's substep count.
        self.assertEqual(read_image(elf, self.at['kClothSubsteps'] - 0x400000, 11),
                         bytes.fromhex('8b581885db7504418b5d20'))


if __name__ == '__main__':
    unittest.main()
