"""The frame-rate corrections' table (gpu/shim/engine/frame_rate_sites.inc) against the 60 FPS++ list, through
tools/re/frame_rate_check.py (no game executable: its own checks of the game's bytes need one)."""
import copy
import importlib.util
import unittest

from paths import ROOT

spec = importlib.util.spec_from_file_location('frame_rate_check', ROOT / 'tools/re/frame_rate_check.py')
frame_rate_check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(frame_rate_check)


class FrameRateSitesTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.entries = frame_rate_check.parse(frame_rate_check.INC.read_text())
        cls.listed = frame_rate_check.list_bytes(ROOT / 'patches/Bloodborne.xml')

    def test_table_holds_against_the_list(self):
        self.assertEqual(frame_rate_check.check(self.entries, self.listed), [])

    def test_every_entry_is_parsed(self):
        counts = {k: len(v) for k, v in self.entries.items()}
        self.assertEqual(counts, {'sites': 7, 'loads': 23, 'values': 22, 'require': 2, 'play': 1})
        text = frame_rate_check.INC.read_text()
        self.assertEqual(sum(text.count(f'\n{m}(') for m in ('FRAME_SITE', 'FRAME_LOAD', 'FRAME_VALUE',
                                                               'FRAME_REQUIRE', 'FRAME_PLAY_TIME')), 55)

    def test_a_wrong_entry_fails(self):
        wrong = copy.deepcopy(self.entries)
        wrong['loads'][0]['constant'] += 4  # its displacement no longer reaches it
        self.assertTrue(any('reads' in f for f in frame_rate_check.check(wrong, self.listed)))
        wrong = copy.deepcopy(self.entries)
        wrong['values'][0]['listed'] ^= 1  # not what the list leaves
        self.assertTrue(any('the list leaves' in f for f in frame_rate_check.check(wrong, self.listed)))
        wrong = copy.deepcopy(self.entries)
        wrong['values'].append(dict(wrong['values'][0]))  # the same bytes twice
        self.assertTrue(any('overlap' in f for f in frame_rate_check.check(wrong, self.listed)))

    def test_the_list_frame_time_code_is_required(self):
        listed = dict(self.listed)
        del listed[self.entries['require'][0]['at']]
        self.assertTrue(any(f.startswith('required') for f in frame_rate_check.check(self.entries, listed)))


if __name__ == '__main__':
    unittest.main()
