import os
import select
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


class ControlFramingTests(unittest.TestCase):
    def test_noise_xx_interoperates_with_official_python_sdk(self):
        compiler = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
        sdk_src = Path("/tmp/muse-gadget-sdk/linux/src")
        if compiler is None or not (sdk_src / "musegadget/noise/noise_xx.py").exists():
            self.skipTest("official Muse SDK checkout is not available")

        prefixes = []
        configured = os.environ.get("MBEDTLS_PREFIX")
        if configured:
            prefixes.append(Path(configured))
        prefixes.extend([Path("/opt/homebrew/opt/mbedtls@3"), Path("/opt/homebrew/opt/mbedtls"),
                         Path("/usr/local/opt/mbedtls@3"), Path("/usr/local/opt/mbedtls")])
        prefix = next((item for item in prefixes if (item / "include/mbedtls/gcm.h").exists()), None)
        if prefix is None:
            self.skipTest("native mbedTLS headers are not available")
        if str(sdk_src) not in sys.path:
            sys.path.insert(0, str(sdk_src))
        sdk_deps = Path("/tmp/muse-gadget-deps")
        if sdk_deps.exists() and str(sdk_deps) not in sys.path:
            sys.path.insert(0, str(sdk_deps))
        try:
            from musegadget.noise.noise_xx import NoiseXXResponder
        except ImportError:
            self.skipTest("official Muse SDK Python dependencies are not available")

        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as temp_dir:
            binary = Path(temp_dir) / "test_noise_interop"
            subprocess.run(
                [compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
                 "-I", str(root / "protocol"), "-I", str(prefix / "include"),
                 str(root / "protocol/noise_xx.c"), str(root / "tests/test_noise_interop.c"),
                 "-L", str(prefix / "lib"), "-lmbedcrypto", "-o", str(binary)],
                check=True,
            )
            environment = os.environ.copy()
            environment["PYTHONPATH"] = str(sdk_src) + os.pathsep + environment.get("PYTHONPATH", "")
            process = subprocess.Popen([str(binary)], stdin=subprocess.PIPE,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       text=True, env=environment)

            def read_line():
                ready, _, _ = select.select([process.stdout], [], [], 10)
                self.assertTrue(ready, "Noise interop helper did not respond")
                value = process.stdout.readline().strip()
                self.assertTrue(value, "Noise interop helper exited without output")
                return value

            try:
                first = read_line()
                self.assertTrue(first.startswith("msg1 "))
                responder = NoiseXXResponder(payload=b"server handshake payload")
                responder.initialize()
                message2 = responder.read_message1_and_write_message2(bytes.fromhex(first[5:]))
                process.stdin.write("msg2 " + message2.hex() + "\n")
                process.stdin.flush()
                third = read_line()
                self.assertTrue(third.startswith("msg3 "))
                responder.read_message3(bytes.fromhex(third[5:]))
                server_send, server_receive = responder.split()

                process.stdin.write("encrypt " + b"client payload".hex() + "\n")
                process.stdin.flush()
                client_ciphertext = bytes.fromhex(read_line()[len("client_ct "):])
                self.assertEqual(server_receive.decrypt_with_ad(b"", client_ciphertext), b"client payload")

                server_ciphertext = server_send.encrypt_with_ad(b"", b"server payload")
                process.stdin.write("server " + server_ciphertext.hex() + "\n")
                process.stdin.flush()
                self.assertEqual(bytes.fromhex(read_line()[len("server_pt "):]), b"server payload")
            finally:
                if process.stdin:
                    process.stdin.close()
                process.wait(timeout=10)
                if process.returncode != 0:
                    stderr = process.stderr.read() if process.stderr else ""
                    self.fail(f"Noise interop helper failed: {process.returncode}: {stderr}")
                if process.stdout:
                    process.stdout.close()
                if process.stderr:
                    process.stderr.close()

    def test_c_wire_framing_harness(self):
        compiler = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
        if compiler is None:
            self.skipTest("no host C compiler available")

        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as temp_dir:
            binary = Path(temp_dir) / "test_control_framing"
            subprocess.run(
                [
                    compiler,
                    "-std=c99",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(root / "protocol"),
                    str(root / "protocol" / "control_framing.c"),
                    str(root / "tests" / "test_control_framing.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)

    def test_c_wire_builders_emit_parseable_json(self):
        compiler = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
        if compiler is None:
            self.skipTest("no host C compiler available")

        import json

        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as temp_dir:
            binary = Path(temp_dir) / "test_link_wire"
            subprocess.run(
                [
                    compiler,
                    "-std=c99",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(root / "protocol"),
                    str(root / "protocol" / "link_wire.c"),
                    str(root / "tests" / "test_link_wire.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            register, chat = result.stdout.splitlines()
            self.assertEqual(json.loads(register)["method"], "link.register")
            self.assertEqual(json.loads(chat)["device_id"], "homelink-000001")

    def test_json_extractor_handles_api_and_chat_fields(self):
        compiler = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
        if compiler is None:
            self.skipTest("no host C compiler available")
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as temp_dir:
            binary = Path(temp_dir) / "test_json_extract"
            subprocess.run(
                [compiler, "-std=c99", "-Wall", "-Wextra", "-Werror", "-I",
                 str(root / "protocol"), str(root / "protocol" / "json_extract.c"),
                 str(root / "tests" / "test_json_extract.c"), "-o", str(binary)],
                check=True,
            )
            subprocess.run([str(binary)], check=True)

    def test_chat_state_strips_controls_without_corrupting_utf8(self):
        compiler = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
        if compiler is None:
            self.skipTest("no host C compiler available")
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as temp_dir:
            binary = Path(temp_dir) / "test_chat_state"
            subprocess.run(
                [compiler, "-std=c99", "-Wall", "-Wextra", "-Werror", "-I",
                 str(root / "vita/src"), str(root / "vita/src/chat_state.c"),
                 str(root / "tests/test_chat_state.c"), "-o", str(binary)],
                check=True,
            )
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
