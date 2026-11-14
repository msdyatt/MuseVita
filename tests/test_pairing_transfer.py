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
        self.assertIn("/pair/identity", page)
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

    def test_token_refresh_uses_official_node_id(self):
        source = (ROOT / "vita/src/muse_session.c").read_text()
        start = source.index("static int refresh_credentials")
        end = source.index("static int websocket_send", start)
        refresh = source[start:end]
        self.assertIn('"{\\"device_id\\":\\"%s\\",\\"sdk_token\\":\\"%s\\"}"', refresh)
        self.assertIn('session->node_id, session->sdk_token', refresh)
        self.assertIn('"{\\"device_id\\":\\"%s\\"}"', refresh)
        self.assertNotIn('session->device_id, session->sdk_token', refresh)

    def test_token_refresh_is_age_based_and_persists_timestamp(self):
        source = (ROOT / "vita/src/muse_session.c").read_text()
        self.assertIn('"access_token_saved_at"', source)
        self.assertIn("TOKEN_REFRESH_AGE_SECONDS", source)
        self.assertIn("current_time - session->access_token_saved_at", source)
        self.assertIn("session->access_token_saved_at = now_seconds();", source)

    def test_native_import_rechecks_sdk_token_hint(self):
        source = (ROOT / "vita/src/muse_session.c").read_text()
        self.assertIn("sdk_token_matches_pairing_hint", source)
        import_start = source.index("int muse_vita_session_import_pairing")
        import_end = source.index("static int fetch_vm", import_start)
        import_block = source[import_start:import_end]
        self.assertIn("sdk_token_matches_pairing_hint(pairing_json, pairing_size, sdk_token)",
                      import_block)

    def test_vm_lookup_accepts_official_vm_name_fallback(self):
        source = (ROOT / "vita/src/muse_session.c").read_text()
        self.assertIn('"vm_name"', source)
        self.assertIn("strncpy(vm_id, vm_name", source)
        self.assertIn("muse_json_find_array_object_range", source)
        self.assertIn('"api_url_v2"', source)
        self.assertIn("DEFAULT_API_ROOT", source)

    def test_chat_events_are_deferred_until_ack_and_bound_to_parent(self):
        source = (ROOT / "vita/src/muse_session.c").read_text()
        self.assertIn("awaiting_chat_ack", source)
        self.assertIn("deferred_chat_buffer", source)
        self.assertIn('"reply_to_message_id"', source)
        self.assertIn("last_rendered_user_id", source)

    def test_chat_send_reports_failure_without_discarding_the_draft(self):
        source = (ROOT / "vita/src/main.c").read_text()
        self.assertIn('"Message sent."', source)
        self.assertIn('"Message not sent — check the connection."', source)
        self.assertIn("muse_vita_chat_clear_draft(&chat);", source)

    def test_websocket_transport_reassembles_fragments_and_handles_partial_writes(self):
        source = (ROOT / "vita/src/muse_session.c").read_text()
        self.assertIn("CURLWS_CONT", source)
        self.assertIn("session->ws_message_active", source)
        self.assertIn("sent > size - offset", source)
        self.assertIn("flags = CURLWS_CONT", source)
        self.assertIn("Control frames may appear between fragments", source)

    def test_request_timeout_cancels_the_link_stream(self):
        source = (ROOT / "vita/src/muse_session.c").read_text()
        wire = (ROOT / "protocol/link_wire.h").read_text()
        self.assertIn("muse_link_encode_reset", wire)
        self.assertIn('"request timeout"', source)
        self.assertIn('"request abandoned"', source)
        self.assertIn("cancel_request_stream", source)

    def test_noise_send_encrypts_serialized_frame(self):
        source = (ROOT / "vita/src/muse_session.c").read_text()
        start = source.index("static int send_noise_payload")
        end = source.index("static int send_link_payload", start)
        function = source[start:end]
        self.assertIn("total_chunks", function)
        self.assertIn("MUSE_LINK_MAX_NOISE_CHUNKS", function)
        frame_position = function.index("muse_link_encode_noise_frame")
        encrypt_position = function.index("muse_noise_xx_encrypt")
        self.assertLess(frame_position, encrypt_position)
        self.assertIn("payload == NULL ? NULL : payload + offset", function)

    def test_phone_transfer_waits_for_listener_and_handles_partial_tcp_writes(self):
        source = (ROOT / "vita/src/pairing_transfer.c").read_text()
        self.assertIn("volatile bool listener_bound", (ROOT / "vita/src/pairing_transfer.h").read_text())
        self.assertIn("static int send_bytes", source)
        self.assertIn("return send_bytes(socket, transfer_file_buffer, total);", source)
        self.assertIn("if (transfer->listener_bound)", source)
        self.assertIn("sceKernelDelayThread(10000)", source)

    def test_phone_transfer_uses_standard_http_status_lines(self):
        source = (ROOT / "vita/src/pairing_transfer.c").read_text()
        self.assertIn('"HTTP/1.1 %d %s\\r\\nContent-Type: %s\\r\\n"', source)
        self.assertIn('"HTTP/1.1 200 OK\\r\\nContent-Type: %s\\r\\n"', source)
        self.assertIn('case 404: return "Not Found";', source)

    def test_phone_transfer_has_a_short_manual_fallback_url(self):
        source = (ROOT / "vita/src/pairing_transfer.c").read_text()
        self.assertIn('strcmp(path, "/") == 0', source)
        self.assertIn('"HTTP/1.1 302 Found\\r\\nLocation: %s\\r\\n"', source)
        self.assertIn('"/pair?s=%s&k=%s"', source)
        self.assertIn('muse_vita_pairing_transfer_manual_url', source)

    def test_phone_onboarding_is_short_and_action_oriented(self):
        page = (ROOT / "vita/assets/pairing.html").read_text()
        self.assertIn("Pair your Vita", page)
        self.assertIn("Pick one bundle", page)
        self.assertIn("musevita-pairing.json", page)
        self.assertIn("Send securely", page)
        self.assertIn("Need help?", page)
        self.assertNotIn("official identity.json", page)

    def test_vita_onboarding_uses_short_actions(self):
        source = (ROOT / "vita/src/main.c").read_text()
        self.assertIn('"Pair your Vita"', source)
        self.assertIn('"Scan. Pick one bundle. Press SELECT."', source)
        self.assertIn('"Scan QR"', source)
        self.assertIn('"Pick one bundle"', source)
        self.assertIn('"Press SELECT"', source)

    def test_identity_download_is_local_and_credential_free(self):
        source = (ROOT / "vita/src/pairing_transfer.c").read_text()
        route = source[source.index('"/pair/identity"'):source.index('"/pair/config"')]
        self.assertIn('IDENTITY_PATH', route)
        self.assertNotIn("PAIRING_PATH", route)
        self.assertNotIn("SDK_TOKEN_PATH", route)

    def test_pairing_listener_only_starts_on_setup_page(self):
        source = (ROOT / "vita/src/main.c").read_text()
        self.assertIn("page == PAGE_SETUP && network == MUSEVITA_NETWORK_ONLINE", source)

    def test_pairing_thread_receives_pointer_argument_safely(self):
        source = (ROOT / "vita/src/pairing_transfer.c").read_text()
        thread_start = source.index("static int transfer_thread")
        start_call = source.index("sceKernelStartThread", thread_start)
        thread_block = source[thread_start:start_call]
        self.assertIn("if (arg == NULL || args != sizeof(transfer)) return -1;", thread_block)
        self.assertIn("transfer = *(MuseVitaPairingTransfer **)arg;", thread_block)
        self.assertIn(
            "sceKernelStartThread(transfer->thread, sizeof(transfer), &transfer)",
            source,
        )
        self.assertNotIn("sceKernelStartThread(transfer->thread, 0, NULL)", source)


if __name__ == "__main__":
    unittest.main()
