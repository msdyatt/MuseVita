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
        self.assertIn('src="/pairing-crypto.js"', page)
        self.assertIn("ECDH", page)
        self.assertIn("HKDF", page)
        self.assertIn("AES-GCM", page)
        self.assertIn("cfg.pub!==pinned", page)
        self.assertIn("'='.repeat((4-raw.length%4)%4)", page)
        self.assertNotIn("pinned.replaceAll('-','+').replaceAll('_','/')+'=='", page)
        self.assertIn("/pair/upload", page)
        self.assertIn("MuseVitaCrypto.seal", page)
        self.assertIn("globalThis.crypto&&globalThis.crypto.subtle", page)
        self.assertNotIn("access_token", page)
        self.assertNotIn("refresh_token", page)

    def test_crypto_fallback_is_packaged_with_the_vpk(self):
        cmake = (ROOT / "vita/CMakeLists.txt").read_text()
        self.assertIn("assets/pairing-crypto.js", cmake)
        source = (ROOT / "vita/src/pairing_transfer.c").read_text()
        self.assertIn('"/pairing-crypto.js"', source)
        self.assertIn('"text/javascript; charset=utf-8"', source)

    def test_history_and_expired_auth_follow_sdk_bounds(self):
        source = (ROOT / "vita/src/muse_session.c").read_text()
        self.assertIn('"/chat/history?limit=1"', source)
        self.assertIn("return status == 401 ? -2 : -1;", source)
        self.assertIn("forget_pairing(session);", source)

    def test_vm_lookup_accepts_official_vm_name_fallback(self):
        source = (ROOT / "vita/src/muse_session.c").read_text()
        self.assertIn('"vm_name"', source)
        self.assertIn("strncpy(vm_id, vm_name", source)

    def test_chat_events_are_deferred_until_ack_and_bound_to_parent(self):
        source = (ROOT / "vita/src/muse_session.c").read_text()
        self.assertIn("awaiting_chat_ack", source)
        self.assertIn("deferred_chat_buffer", source)
        self.assertIn('"reply_to_message_id"', source)
        self.assertIn("last_rendered_user_id", source)

    def test_websocket_transport_reassembles_fragments_and_handles_partial_writes(self):
        source = (ROOT / "vita/src/muse_session.c").read_text()
        self.assertIn("CURLWS_CONT", source)
        self.assertIn("session->ws_message_active", source)
        self.assertIn("sent > size - offset", source)
        self.assertIn("Control frames may appear between fragments", source)

    def test_pairing_listener_only_starts_on_setup_page(self):
        source = (ROOT / "vita/src/main.c").read_text()
        self.assertIn("page == PAGE_SETUP && network == MUSEVITA_NETWORK_ONLINE", source)


if __name__ == "__main__":
    unittest.main()
