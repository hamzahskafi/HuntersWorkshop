"""0.4.4 beta 27c: the shipped settings (engine/config.default.ini, which a new install's config.ini is copied
from by bb-start and the launcher) and the Windows launcher's defaults (Core/IniFile.cs Defaults) agree.
tests/test_settings.cpp checks the shipped file against the engine's own defaults (bbport_settings.h).
(The shipped file said fsr411 and Quality after 0.4.4 beta 27 changed the defaults: the tester's catch.)"""
import re
import unittest

from paths import ROOT

SHIPPED = ROOT / 'config.default.ini'
LAUNCHER = ROOT.parent / 'launcher' / 'windows' / 'BloodborneLauncher' / 'Core' / 'IniFile.cs'


def shipped():
    values = {}
    for line in SHIPPED.read_text(encoding='utf-8').splitlines():
        key, sep, value = line.partition('=')
        if sep and not line.startswith('#'):
            values[key.strip()] = value.strip()
    return values


def launcher():
    text = LAUNCHER.read_text(encoding='utf-8-sig')
    block = text[text.index('Defaults ='):]
    block = block[:block.index('};')]
    return dict(re.findall(r'\("([^"]+)",\s*"([^"]*)"\)', block))


class ShippedDefaultsTests(unittest.TestCase):
    def test_the_launcher_and_the_shipped_file_agree(self):
        ours, theirs = shipped(), launcher()
        self.assertTrue(theirs, 'no defaults read from IniFile.cs')
        for key in sorted(set(ours) & set(theirs)):
            with self.subTest(key=key):
                self.assertEqual(ours[key], theirs[key])

    def test_the_upscaling_defaults_are_written_out(self):
        # No upscaler (the game's own anti-aliasing, 0.4.4 beta 28), and for when one is chosen its quality,
        # AMD FSR's version and DLSS's model: Auto (beta 27c).
        expected = {'upscaler': 'off', 'preset': 'auto', 'fsr_version': 'auto', 'dlss_preset': 'auto'}
        for source, values in (('config.default.ini', shipped()), ('IniFile.cs', launcher())):
            for key, value in expected.items():
                with self.subTest(source=source, key=key):
                    self.assertEqual(values.get(key), value)


if __name__ == '__main__':
    unittest.main()
