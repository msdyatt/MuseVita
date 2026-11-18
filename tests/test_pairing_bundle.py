import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from prepare_musevita_pairing import BUNDLE_FORMAT, PairingBundleError, stage_bundle, stage_bundle_file


class PairingBundleTests(unittest.TestCase):
    def _make_state(self, root: Path) -> Path:
        state = root / "state"
        state.mkdir()
        token = "mgst_" + ("A" * 42) + "A"
        (state / "identity.json").write_text(json.dumps({"mac": "02:00:00:00:00:01"}))
        (state / "pairing.json").write_text(json.dumps({
            "access_token": "synthetic-access",
            "refresh_token": "synthetic-refresh",
            "sdk_token_hint": token[:12],
        }))
        (state / "sdk_token").write_text(token + "\n")
        return state

    def test_stages_only_validated_official_files_without_printing_values(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            state = self._make_state(root)
            output = root / "vita-bundle"
            stage_bundle(state, output)
            self.assertEqual(sorted(path.name for path in output.iterdir()),
                             ["identity.json", "pairing.json", "sdk_token"])
            self.assertEqual((output / "sdk_token").read_text(), (state / "sdk_token").read_text())
            self.assertEqual(os.stat(output).st_mode & 0o777, 0o700)
            self.assertEqual(os.stat(output / "sdk_token").st_mode & 0o777, 0o600)

    def test_rejects_mismatched_sdk_token_hint(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            state = self._make_state(root)
            pairing = json.loads((state / "pairing.json").read_text())
            pairing["sdk_token_hint"] = "mgst_wrong"
            (state / "pairing.json").write_text(json.dumps(pairing))
            with self.assertRaises(PairingBundleError):
                stage_bundle(state, root / "vita-bundle")

    def test_creates_one_file_phone_bundle_without_printing_credentials(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            state = self._make_state(root)
            output = root / "musevita-pairing.json"
            stage_bundle_file(state, output)
            bundle = json.loads(output.read_text())
            self.assertEqual(bundle["format"], BUNDLE_FORMAT)
            self.assertIn('"access_token"', bundle["pairing"])
            self.assertEqual(bundle["sdk_token"], (state / "sdk_token").read_text().strip())
            self.assertEqual(os.stat(output).st_mode & 0o777, 0o600)


if __name__ == "__main__":
    unittest.main()
