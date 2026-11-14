#!/usr/bin/env python3
"""Muse Vita QR setup *demonstrator*.

The service creates short-lived, NON-AUTHENTICATED setup sessions.
It intentionally never receives, stores, or relays Muse credentials.
Do not expose this development server directly to the internet.
"""
from __future__ import annotations

from dataclasses import dataclass
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from io import BytesIO
from pathlib import Path
from urllib.parse import parse_qs, quote, urlsplit
import json
import os
import secrets
import threading
import time

ROOT = Path(__file__).resolve().parent
SESSION_TTL_SECONDS = 600


@dataclass
class SetupSession:
    code: str
    created_at: float
    expires_at: float
    state: str = "waiting_for_protocol_integration"


class SessionStore:
    """Ephemeral session registry; stores no API keys or authorization material."""

    def __init__(self, clock=time.time, ttl: int = SESSION_TTL_SECONDS):
        self._sessions: dict[str, SetupSession] = {}
        self._clock = clock
        self._ttl = ttl
        self._lock = threading.Lock()

    def create(self) -> SetupSession:
        with self._lock:
            self._purge_expired()
            code = secrets.token_urlsafe(12)
            while code in self._sessions:
                code = secrets.token_urlsafe(12)
            now = self._clock()
            session = SetupSession(code=code, created_at=now, expires_at=now + self._ttl)
            self._sessions[code] = session
            return session

    def get(self, code: str) -> SetupSession | None:
        with self._lock:
            self._purge_expired()
            return self._sessions.get(code)

    def _purge_expired(self) -> None:
        now = self._clock()
        for key in list(self._sessions):
            if self._sessions[key].expires_at <= now:
                del self._sessions[key]


SESSIONS = SessionStore()


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT), **kwargs)

    def end_headers(self):
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Security-Policy", "default-src 'self'; img-src 'self' data:; style-src 'self'; script-src 'self'; connect-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'")
        super().end_headers()

    def send_json(self, obj: dict, status: int = 200):
        data = json.dumps(obj, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self):
        if self.path != "/api/sessions":
            return self.send_json({"error": "not_found"}, 404)
        # No credential-bearing data is accepted in this demonstrator.
        length = int(self.headers.get("Content-Length", "0"))
        if length != 0:
            return self.send_json({"error": "body_not_accepted"}, 400)
        s = SESSIONS.create()
        return self.send_json({
            "code": s.code,
            "expires_at": int(s.expires_at),
            "state": s.state,
            "qr_url": "/api/qr.svg?code=" + quote(s.code),
            "pair_path": "/pair?code=" + quote(s.code)
        }, 201)

    def do_GET(self):
        p = urlsplit(self.path)
        if p.path in ("/", "/pair"):
            self.path = "/index.html"
            return super().do_GET()
        if p.path == "/api/health":
            return self.send_json({"ok": True, "prototype": True})
        if p.path.startswith("/api/sessions/"):
            code = p.path.removeprefix("/api/sessions/")
            session = SESSIONS.get(code)
            if not session:
                return self.send_json({"error": "missing_or_expired"}, 404)
            return self.send_json({"code": session.code, "state": session.state, "expires_at": int(session.expires_at)})
        if p.path == "/api/qr.svg":
            code = parse_qs(p.query).get("code", [""])[0]
            if not SESSIONS.get(code):
                return self.send_json({"error": "missing_or_expired"}, 404)
            base = os.environ.get("MUSE_VITA_PUBLIC_URL", "http://localhost:8777").rstrip("/")
            parts = urlsplit(base)
            if parts.scheme not in ("http", "https") or not parts.netloc or parts.username or parts.password:
                return self.send_json({"error": "invalid_server_configuration"}, 500)
            url = base + "/pair?code=" + quote(code)
            try:
                import qrcode
                import qrcode.image.svg
            except ImportError:
                return self.send_json({"error": "install_qrcode_dependency"}, 500)
            img = qrcode.make(url, image_factory=qrcode.image.svg.SvgPathImage, box_size=9, border=3)
            buf = BytesIO()
            img.save(buf)
            data = buf.getvalue()
            self.send_response(200)
            self.send_header("Content-Type", "image/svg+xml")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        if p.path.startswith("/api/"):
            return self.send_json({"error": "not_found"}, 404)
        # Don't expose arbitrary hidden or nonstatic files.
        if p.path not in ("/index.html", "/app.js", "/styles.css"):
            return self.send_error(404)
        return super().do_GET()


def main():
    port = int(os.environ.get("PORT", "8777"))
    host = os.environ.get("HOST", "127.0.0.1")
    print(f"Muse Vita onboarding prototype: http://{host}:{port}")
    print("Development/demo only. Does NOT pair with Muse or collect credentials.")
    ThreadingHTTPServer((host, port), Handler).serve_forever()


if __name__ == "__main__":
    main()
