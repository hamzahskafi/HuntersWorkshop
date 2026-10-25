"""Our frame-rate table (start/frame_rate_base.inc, the Windows start program's in place of the patch file's 60
FPS++ list): every entry is the list's bytes at the list's address, in its order, every line of the list is in it
but the three it leaves out, and each entry's game bytes are as long as ours (tools/re/frame_rate_base.py makes
it; with a 1.09 eboot.elf its --check also compares the game's bytes)."""
import re
import struct
import unittest
import xml.etree.ElementTree as ET

from paths import ROOT

LEFT_OUT = (0x26FB39F, 0x4CAD7FC, 0x2483EC1)


def list_lines():
    meta = next(m for m in ET.parse(ROOT / 'patches/Bloodborne.xml').getroot().iter('Metadata')
                if m.get('Name') == '60 FPS++')
    for line in meta.iter('Line'):
        kind, value = line.get('Type').lower(), line.get('Value')
        yield int(line.get('Address'), 16), (bytes.fromhex(value) if kind == 'bytes' else
                                             struct.pack('<I', int(value, 0)))


class FrameRateBaseTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        text = (ROOT / 'start/frame_rate_base.inc').read_text()
        cls.entries = [(int(a, 16), bytes.fromhex(was), bytes.fromhex(now)) for a, was, now in
                       re.findall(r'^FRAME_BASE\((0x[0-9a-f]+), "([0-9a-f]+)", "([0-9a-f]+)"\)$', text, re.M)]

    def test_the_lists_lines_in_its_order(self):
        expected = [(a, d) for a, d in list_lines() if a not in LEFT_OUT]
        self.assertEqual([(a, now) for a, _, now in self.entries], expected)
        self.assertEqual(len(self.entries), 244)

    def test_game_bytes_as_long_as_ours(self):
        for a, was, now in self.entries:
            self.assertEqual(len(was), len(now), hex(a))
        self.assertFalse([a for a, _, _ in self.entries if a in LEFT_OUT])


if __name__ == '__main__':
    unittest.main()
