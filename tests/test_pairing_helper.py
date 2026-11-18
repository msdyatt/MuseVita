import base64
import json
import shutil
import subprocess
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse


ROOT = Path(__file__).resolve().parents[1]
HELPER = ROOT / "tools/send_musevita_pairing.js"
VITA_PUBLIC_KEY = (
    "BGsX0fLhLEJH-Lzm5WOkQPJ3A32BLeszoPShOUXYmMKWT-NC4v4af5uO5-tKfA-eFivOM1drMV7Oy7ZAaDe_UfU"
)


class PairingHelperTests(unittest.TestCase):
    def test_helper_reads_state_and_sends_one_encrypted_transfer(self):
        if shutil.which("node") is None:
            self.skipTest("Node.js is required")

        sid = "ABCDEFGHJKLMNPQR"
        identity = {"mac": "02:00:00:00:00:01"}
        token = "mgst_" + ("A" * 42) + "A"
        received = {}

        class VitaHandler(BaseHTTPRequestHandler):
            def log_message(self, *_args):
                pass

            def send_body(self, status, body, content_type="application/json"):
                encoded = body.encode("utf-8")
                self.send_response(status)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(encoded)))
                self.end_headers()
                self.wfile.write(encoded)

            def do_GET(self):
                parsed = urlparse(self.path)
                query = parse_qs(parsed.query)
                if parsed.path == "/":
                    self.send_response(302)
                    self.send_header("Location", f"/pair?s={sid}&k={VITA_PUBLIC_KEY}")
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                elif parsed.path == "/pair/config" and query.get("s") == [sid]:
                    self.send_body(200, json.dumps({"sid": sid, "pub": VITA_PUBLIC_KEY}))
                elif parsed.path == "/pair/identity" and query.get("s") == [sid]:
                    self.send_body(200, json.dumps(identity))
                else:
                    self.send_body(404, "Not found", "text/plain")

            def do_POST(self):
                parsed = urlparse(self.path)
                query = parse_qs(parsed.query)
                if parsed.path != "/pair/upload" or query.get("s") != [sid]:
                    self.send_body(404, "Not found", "text/plain")
                    return
                length = int(self.headers.get("Content-Length", "0"))
                received["body"] = json.loads(self.rfile.read(length).decode("utf-8"))
                self.send_body(200, "Pairing bundle received", "text/plain")

        server = ThreadingHTTPServer(("127.0.0.1", 0), VitaHandler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            with tempfile.TemporaryDirectory() as temp:
                state = Path(temp) / "state"
                state.mkdir()
                (state / "identity.json").write_text(json.dumps(identity), encoding="utf-8")
                (state / "pairing.json").write_text(json.dumps({
                    "access_token": "synthetic-access",
                    "refresh_token": "synthetic-refresh",
                    "sdk_token_hint": token[:12],
                }), encoding="utf-8")
                (state / "sdk_token").write_text(token + "\n", encoding="utf-8")
                result = subprocess.run([
                    "node", str(HELPER), "--state-dir", str(state),
                    "--vita-url", f"http://127.0.0.1:{server.server_port}/",
                ], capture_output=True, text=True, check=False)
        finally:
            server.shutdown()
            thread.join(timeout=5)
            server.server_close()

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Pairing sent securely", result.stdout)
        self.assertEqual(received["body"]["sid"], sid)
        for field in ("epk", "iv", "ct"):
            decoded = base64.urlsafe_b64decode(received["body"][field] + "===")
            self.assertGreater(len(decoded), 0)
        serialized = json.dumps(received["body"])
        self.assertNotIn("synthetic-access", serialized)
        self.assertNotIn(token, serialized)


if __name__ == "__main__":
    unittest.main()

