import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class ControlFramingTests(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
