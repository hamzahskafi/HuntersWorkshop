# SPDX-License-Identifier: GPL-2.0-or-later
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import os
# The tests cover the connection itself: online play is paused by default (online.ONLINE_PAUSED).
os.environ['BB_ONLINE_PAUSED'] = '0'
from online import configure_online, game_server, http_base, ONLINE_ENV, THD_ENV, HUNTERS_DREAM


class OnlineProfileTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.data = Path(self.temp.name)
        self.settings = dict(online_enabled='1', online_connected='1',
            online_host='server.example', online_port='31413',
            online_webapi='http://server.example:31415', online_username='Hunter',
            online_upnp='0')
        self.env = {'BB_SHADNET_PASSWORD': 'synthetic-test-password', 'UNRELATED': 'keep'}

    def test_offline_clears_inherited_online_credentials_and_override(self):
        for settings in ({}, {'online_enabled':'0'}, {'online_enabled':'1','online_connected':'0'}):
            env = dict.fromkeys(ONLINE_ENV, 'inherited')
            env['UNRELATED'] = 'keep'
            self.assertIsNone(configure_online(settings, self.data, env))
            self.assertEqual(env, {'BB_ONLINE':'0','UNRELATED':'keep'})
            self.assertFalse((self.data/'network').exists())

    def test_a_shadnet_profile_starts_offline(self):
        # 0.4.4 beta 28: the shadNet path went; its settings (a server, an account) start offline and
        # leave no network cache behind.
        env = dict(self.env, **dict.fromkeys(THD_ENV, 'inherited'))
        self.assertIsNone(configure_online(self.settings, self.data, env))
        self.assertEqual(env, {'BB_ONLINE': '0', 'UNRELATED': 'keep'})
        self.assertFalse((self.data / 'network').exists())

    def test_game_server_bare_name_and_old_shadnet_default(self):
        self.assertEqual(game_server('thehuntersdream.com'), 'https://thehuntersdream.com')
        self.assertEqual(game_server('http://srv.shadps4.net:31315'), HUNTERS_DREAM)
        self.assertEqual(game_server('http://game.example:8080'), 'http://game.example:8080')

    def test_url_does_not_drop_port(self):
        self.assertEqual(http_base('https://server.example:34443/api/'),'https://server.example:34443/api')
        for url in ('http://server.example?x=1','http://server.example#x','http://server.example\n'):
            with self.assertRaises(ValueError): http_base(url)


class OnlinePausedTests(unittest.TestCase):
    def test_paused_starts_offline(self):
        import online
        env = {'BB_SHADNET_SERVER': 'x:1'}
        paused = online.ONLINE_PAUSED
        online.ONLINE_PAUSED = True
        try:
            self.assertIsNone(configure_online({'online_enabled': '1'}, '.', env))
        finally:
            online.ONLINE_PAUSED = paused
        self.assertEqual(env, {'BB_ONLINE': '0'})


if __name__ == '__main__': unittest.main()
