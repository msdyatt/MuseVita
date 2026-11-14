import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "web"))
from server import SessionStore


class SessionTests(unittest.TestCase):
    def test_session_is_created_and_expires(self):
        now = [1000]
        store = SessionStore(clock=lambda: now[0], ttl=600)
        session = store.create()
        self.assertEqual(session.state, "waiting_for_protocol_integration")
        self.assertGreaterEqual(len(session.code), 16)
        self.assertIs(store.get(session.code), session)
        now[0] += 600
        self.assertIsNone(store.get(session.code))

    def test_codes_are_independent_and_do_not_contain_credentials(self):
        store = SessionStore()
        a = store.create()
        b = store.create()
        self.assertNotEqual(a.code, b.code)
        self.assertFalse(hasattr(a, "access_token"))
        self.assertFalse(hasattr(a, "refresh_token"))
        self.assertIsNone(store.get("fake"))


if __name__ == "__main__":
    unittest.main()
