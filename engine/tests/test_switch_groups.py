"""Switches that stand for several (0.4.4 beta 28): bb-start (start/main.cpp ExpandSwitchGroups) and run.sh
hold the same table, and run.sh expands it as bb-start does. The same for the diagnostics list, BB_DIAG
(ExpandDiagnostics, run.sh's diag parts)."""
import os
import re
import subprocess
import unittest
from pathlib import Path

ENGINE = Path(__file__).resolve().parents[1]


def block(text, marker='switch-groups'):
    start = text.index(f'{marker}-begin')
    return text[start:text.index(f'{marker}-end', start)]


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


def bb_start_diag_parts():
    parts = {}
    for name, fallback, sets in re.findall(r'\{"(\w+)", "(\w*)", \{(.*?)\}\},',
                                           block((ENGINE / 'start/main.cpp').read_text(), 'diag-parts')):
        parts[name] = (fallback, tuple(re.findall(r'\{"(\w+)", "(\w*)"\}', sets)))
    return parts


def run_sh_diag_parts():
    parts = {}
    for line in re.findall(r'^\s*"(.*)"$', block((ENGINE / 'run.sh').read_text(), 'diag-parts'), re.M):
        name, fallback, *sets = line.split()
        parts[name] = ('' if fallback == '-' else fallback, tuple(tuple(s.split('=')) for s in sets))
    return parts


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


class DiagnosticsTests(unittest.TestCase):
    def test_bb_start_and_run_sh_hold_the_same_parts(self):
        start = bb_start_diag_parts()
        self.assertIn('heap', start)
        self.assertEqual(start, run_sh_diag_parts())

    def expand(self, **env):
        text = (ENGINE / 'run.sh').read_text()
        start = text.index('\n', text.index('# diag-parts-begin')) + 1
        snippet = text[start:text.index('\nfi\n', start) + 4]  # the table and the code that expands it
        names = sorted({v for _, sets in bb_start_diag_parts().values() for v, _ in sets})
        clean = {k: v for k, v in os.environ.items() if not k.startswith('BB_')}
        out = subprocess.run(['bash', '-c', snippet + ''.join(f'echo "{n}=${{{n}-unset}}"\n' for n in names)],
                             env={**clean, **env}, capture_output=True, text=True, check=True).stdout
        return dict(line.split('=', 1) for line in out.splitlines() if line.startswith('BB_'))

    def test_parts_set_their_switches(self):
        values = self.expand(BB_DIAG='frame, heap,hitch=25,threads=gpu+audio')
        self.assertEqual(values['BB_FRAME_STATS'], '1')
        self.assertEqual([values[n] for n in ('BB_WRITE_LOG', 'BB_FREE_CHECK', 'BB_HEAP_SITES')], ['1', '1', '1'])
        self.assertEqual(values['BB_HITCH_REPORT_MS'], '25')
        self.assertEqual(values['BB_SAMPLE_THREADS'], 'gpu,audio')
        self.assertEqual(values['BB_GPU_PROFILE'], 'unset')

    def test_a_bare_hitch_takes_its_default_and_a_switch_set_on_its_own_wins(self):
        values = self.expand(BB_DIAG='hitch,gpu,nonsense', BB_GPU_PROFILE='0')
        self.assertEqual(values['BB_HITCH_REPORT_MS'], '40')
        self.assertEqual(values['BB_GPU_PROFILE'], '0')
        self.assertEqual(self.expand()['BB_FRAME_STATS'], 'unset')


if __name__ == '__main__':
    unittest.main()
