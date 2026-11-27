from paths import ROOT
import os
import subprocess
import unittest

EXE = ROOT / 'out/sync-test'


@unittest.skipUnless(EXE.exists(), 'run bash build.sh --test first')
class SyncTests(unittest.TestCase):
    """0.4.6 beta 18: the runtime's semaphores and read/write locks under load (tests/test_sync.c), the
    new ones and, with BB_RUNTIME_SYNC=0, the ones before beta 18."""

    def run_case(self, mode, legacy=False):
        env = dict(os.environ)
        if legacy:
            env['BB_RUNTIME_SYNC'] = '0'
        return subprocess.run([str(EXE.resolve()), mode], capture_output=True, text=True, timeout=300, env=env)

    def test_fork_join_barrier(self):
        for legacy in (False, True):
            r = self.run_case('--barrier', legacy)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertIn('PASS: sync', r.stdout)

    def test_semaphore_tokens_cancel_delete_and_stale_ids(self):
        for legacy in (False, True):
            r = self.run_case('--stress', legacy)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_rwlock_readers_writers_first_use_and_destroy(self):
        for legacy in (False, True):
            r = self.run_case('--rwlock', legacy)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
