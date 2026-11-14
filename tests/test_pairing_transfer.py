import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class PairingTransferTests(unittest.TestCase):
    def test_qr_payload_is_local_and_does_not_include_muse_credentials(self):
        source = (ROOT / "vita/src/pairing_transfer.c").read_text()
        qr_start = source.index('"http://%s:%d/pair?s=%s&k=%s"')
        qr_block = source[qr_start:source.index("transfer->thread", qr_start)]
        self.assertIn("session_id", qr_block)
        self.assertIn("public_key_b64", qr_block)
        self.assertNotIn("access_token", qr_block)
        self.assertNotIn("refresh_token", qr_block)
        self.assertNotIn("sdk_token", qr_block)

    def test_phone_page_uses_pinned_standard_web_crypto(self):
        page = (ROOT / "vita/assets/pairing.html").read_text()
        self.assertIn("ECDH", page)
        self.assertIn("HKDF", page)
        self.assertIn("AES-GCM", page)
        self.assertIn("cfg.pub!==pinned", page)
        self.assertIn("/pair/upload", page)
        self.assertNotIn("access_token", page)
        self.assertNotIn("refresh_token", page)


if __name__ == "__main__":
    unittest.main()
