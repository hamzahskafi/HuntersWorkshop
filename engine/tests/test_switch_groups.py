"""Switches that stand for several (0.4.4 beta 28): bb-start (start/main.cpp ExpandSwitchGroups) and run.sh
hold the same table, and run.sh expands it as bb-start does."""
import os
import re
import subprocess
import unittest
from pathlib import Path

ENGINE = Path(__file__).resolve().parents[1]


def block(text):
    start = text.index('switch-groups-begin')
    return text[start:text.index('switch-groups-end', start)]


def bb_start_groups():
    groups = {}
    for name, when, force, sets in re.findall(r'\{"(\w+)", "(\w+)", (true|false),\s*\{(.*?)\}\},',
                                                block((ENGINE / 'start/main.cpp').read_text()), re.S):
        pairs = tuple(re.findall(r'\{"(\w+)", "(\w+)"\}', sets))
        groups[name] = (when, force == 'true', pairs)
    return groups


def run_sh_groups():
    groups = {}
    for line in re.findall(r'^\s*"(.*)"$', block((ENGINE / 'run.sh').read_text()), re.M):
        trigger, mode, *sets = line.split()
        name, when = trigger.split('=')
        groups[name] = (when, mode == 'force', tuple(tuple(s.split('=')) for s in sets))
    return groups


class SwitchGroupTests(unittest.TestCase):
    def test_bb_start_and_run_sh_hold_the_same_table(self):
        start = bb_start_groups()
        self.assertEqual(set(start), {'BB_SAFE_MODE', 'BB_FAST_DRAWS', 'BB_COMPILE_SKIP', 'BB_VRAM_BUDGET',
                                      'BB_SYNC'})
        self.assertEqual(start, run_sh_groups())

    def expand(self, **env):
        text = (ENGINE / 'run.sh').read_text()
        start = text.index('\n', text.index('# switch-groups-begin')) + 1
        snippet = text[start:text.index('\ndone\n', start) + 6]  # the table and the loop that expands it
        names = sorted({part for _, _, sets in bb_start_groups().values() for part, _ in sets})
        clean = {k: v for k, v in os.environ.items() if not k.startswith('BB_')}
        out = subprocess.run(['bash', '-c', snippet + ''.join(f'echo "{n}=${{{n}-unset}}"\n' for n in names)],
                             env={**clean, **env}, capture_output=True, text=True, check=True).stdout
        return dict(line.split('=', 1) for line in out.splitlines() if '=' in line and not line.startswith('Engine'))

    def test_safe_mode_sets_its_values_whatever_was_set(self):
        values = self.expand(BB_SAFE_MODE='1', BB_HOT_PAGES='2', BB_SUBMIT_END_TASKS='1')
        self.assertEqual(values['BB_HOT_PAGES'], '0')
        self.assertEqual(values['BB_MODS_ENABLED'], '0')
        self.assertEqual(values['BB_INDIRECT_CLAMP'], '1')
        # Safe mode turns fast draws off, whose parts follow (a part set on its own stays).
        self.assertEqual(values['BB_FAST_DRAWS'], '0')
        self.assertEqual(values['BB_MEMO_REVALIDATE'], '0')
        self.assertEqual(values['BB_SUBMIT_END_TASKS'], '1')

    def test_a_group_off_turns_its_parts_off(self):
        values = self.expand(BB_COMPILE_SKIP='0')
        self.assertEqual([values[n] for n in ('BB_SKIP_UNCOMPILED', 'BB_LATE_PIPELINE_BIND', 'BB_PIPELINE_TAKE')],
                         ['0', '0', '0'])
        self.assertEqual(values['BB_SUBMIT_END_TASKS'], 'unset')
        self.assertEqual(self.expand()['BB_SKIP_UNCOMPILED'], 'unset')


if __name__ == '__main__':
    unittest.main()
