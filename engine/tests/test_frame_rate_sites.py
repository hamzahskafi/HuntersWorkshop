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
        self.assertEqual(counts, {'sites': 20, 'loads': 29, 'values': 27, 'counts': 4, 'targets': 4, 'cloth_scales': 1,
                                  'eases': 2, 'require': 2, 'play': 1})
        text = frame_rate_check.INC.read_text()
        self.assertEqual(sum(text.count(f'\n{m}(') for m in ('FRAME_SITE', 'FRAME_LOAD', 'FRAME_VALUE', 'FRAME_COUNT',
                                                               'FRAME_TARGET', 'FRAME_CLOTH_SCALE', 'FRAME_EASE',
                                                               'FRAME_REQUIRE', 'FRAME_PLAY_TIME')), 90)

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

    def test_counts_and_targets_follow_the_rate(self):
        by_at = {c['at']: c for c in self.entries['counts']}
        for fps, frames, micros, digits in ((60, 20, 16500, b'16666'), (90, 30, 11000, b'11111'),
                                            (120, 40, 8250, b'08333'), (144, 48, 6875, b'06944')):
            with self.subTest(fps=fps):
                self.assertEqual(frame_rate_check.count_bytes(by_at[0x192c405], fps), frames.to_bytes(4, 'little'))
                self.assertEqual(frame_rate_check.count_bytes(by_at[0x25629f1], fps), micros.to_bytes(4, 'little'))
                self.assertEqual(frame_rate_check.count_bytes(by_at[0x4b33c51], fps), digits)
        self.assertEqual(frame_rate_check.count_bytes(by_at[0x4b33c51], 240), b'04166')  # uncapped's most
        wrong = copy.deepcopy(self.entries)
        wrong['counts'][0]['game'] += 1  # 60's is no longer what the list holds
        self.assertTrue(any(f.startswith('count') for f in frame_rate_check.check(wrong, self.listed)))
        wrong = copy.deepcopy(self.entries)
        wrong['targets'][0]['at'] += 1
        self.assertTrue(any(f.startswith('target') for f in frame_rate_check.check(wrong, self.listed)))

    def test_a_cave_load_is_in_its_site(self):
        wrong = copy.deepcopy(self.entries)
        load = next(l for l in wrong['loads'] if l['group'] == 'Cloth')
        load['bytes'] = load['bytes'][:-1] + bytes([load['bytes'][-1] ^ 1])
        self.assertTrue(any('not what its site' in f for f in frame_rate_check.check(wrong, self.listed)))

    def test_the_list_frame_time_code_is_required(self):
        listed = dict(self.listed)
        del listed[self.entries['require'][0]['at']]
        self.assertTrue(any(f.startswith('required') for f in frame_rate_check.check(self.entries, listed)))


if __name__ == '__main__':
    unittest.main()
