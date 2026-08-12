from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


SCRIPT = Path(__file__).resolve().parents[1] / "collect-ci-artifacts.py"
SPEC = importlib.util.spec_from_file_location("collect_ci_artifacts", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


class CollectCiArtifactsTests(unittest.TestCase):
    def test_package_candidates_only_returns_final_build_root_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            expected = build / "relaydesk-win-x64-portable.7z"
            expected.write_bytes(b"package")
            staging = build / "_CPack_Packages" / "win64" / "RelayDesk.app"
            staging.mkdir(parents=True)
            (staging.parent / "relaydesk-win-x64-portable.7z").write_bytes(b"duplicate")

            self.assertEqual(MODULE.package_candidates(build), [expected])

    def test_app_candidates_excludes_cpack_and_vcpkg_staging(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            expected = build / "bin" / "RelayDesk.app"
            expected.mkdir(parents=True)
            (build / "_CPack_Packages" / "RelayDesk.app").mkdir(parents=True)
            (build / "vcpkg_installed" / "RelayDesk.app").mkdir(parents=True)

            self.assertEqual(MODULE.app_candidates(build), [expected])

    def test_signed_flag_is_recorded_in_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build = root / "build"
            out = root / "out"
            build.mkdir()
            (build / "relaydesk-1.26.0-win-x64-signed-portable.7z").write_bytes(b"signed-content")
            arguments = [
                str(SCRIPT),
                "--build-dir", str(build),
                "--out-dir", str(out),
                "--platform", "windows-x64",
                "--commit", "abc123",
                "--signed",
            ]
            with patch.object(sys, "argv", arguments):
                self.assertEqual(MODULE.main(), 0)

            manifest = json.loads((out / "artifact-manifest.json").read_text(encoding="utf-8"))
            self.assertTrue(manifest["signed"])


if __name__ == "__main__":
    unittest.main()
