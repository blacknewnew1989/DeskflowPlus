# SPDX-FileCopyrightText: 2026 RelayDesk Contributors
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

from __future__ import annotations

import hashlib
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[1] / "test-macos-install-regression.py"
ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("test_macos_install_regression_script", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class MacosInstallRegressionTests(unittest.TestCase):
    @staticmethod
    def _write_artifacts(root: Path, commit: str = "a" * 40) -> Path:
        artifact_dir = root / "artifact"
        artifact_dir.mkdir()
        files = {
            f"RelayDesk-macos-arm64-adhoc-{commit[:8]}.app.zip": b"app zip",
            f"relaydesk-{commit}-macos-arm64-adhoc.dmg": b"dmg",
            f"relaydesk-{commit}-Source.tar.xz": b"source",
        }
        entries = []
        checksums = []
        for name, content in files.items():
            path = artifact_dir / name
            path.write_bytes(content)
            digest = hashlib.sha256(content).hexdigest()
            entries.append({"name": name, "size": len(content), "sha256": digest})
            checksums.append(f"{digest}  {name}")
        manifest = {
            "platform": "macos-arm64",
            "commit": commit,
            "signed": False,
            "notarized": False,
            "packageVariant": "adhoc",
            "files": entries,
        }
        (artifact_dir / "artifact-manifest.json").write_text(
            json.dumps(manifest), encoding="utf-8"
        )
        (artifact_dir / "SHA256SUMS.txt").write_text(
            "\n".join(checksums) + "\n", encoding="ascii"
        )
        return artifact_dir

    def test_validates_exact_adhoc_app_zip_and_dmg_checksums(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            commit = "a" * 40
            artifacts = self._write_artifacts(Path(directory), commit)

            result = MODULE.validate_artifacts(artifacts, commit)

            self.assertEqual(result.package_variant, "adhoc")
            self.assertTrue(result.app_zip.name.endswith(".app.zip"))
            self.assertTrue(result.dmg.name.endswith(".dmg"))

    def test_rejects_tampered_artifact(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            commit = "b" * 40
            artifacts = self._write_artifacts(Path(directory), commit)
            next(artifacts.glob("*.dmg")).write_bytes(b"tampered")

            with self.assertRaisesRegex(MODULE.RegressionError, "SIZE_MISMATCH"):
                MODULE.validate_artifacts(artifacts, commit)

    def test_rejects_non_adhoc_manifest_boundary(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            commit = "c" * 40
            artifacts = self._write_artifacts(Path(directory), commit)
            manifest_path = artifacts / "artifact-manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["packageVariant"] = "signed"
            manifest["signed"] = True
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

            with self.assertRaisesRegex(MODULE.RegressionError, "PACKAGE_VARIANT"):
                MODULE.validate_artifacts(artifacts, commit)

    def test_test_root_must_be_unique_and_beneath_runner_temp(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            runner_temp = base / "runner-temp"
            runner_temp.mkdir()
            valid = runner_temp / f"{MODULE.TEST_ROOT_PREFIX}abc123"
            valid.mkdir()
            outside = base / f"{MODULE.TEST_ROOT_PREFIX}outside"
            outside.mkdir()

            self.assertEqual(MODULE.validate_test_root(valid, runner_temp), valid.resolve())
            with self.assertRaisesRegex(MODULE.RegressionError, "UNSAFE_TEST_ROOT"):
                MODULE.validate_test_root(outside, runner_temp)

    def test_script_keeps_required_real_macos_lifecycle_commands(self) -> None:
        script = SCRIPT.read_text(encoding="utf-8")
        for required in (
            '"/usr/bin/hdiutil", "verify"',
            '"/usr/bin/hdiutil",\n                    "attach"',
            '"/usr/bin/hdiutil", "detach"',
            'input_text="Y\\n"',
            '"dmgLicenseAcceptedForAutomation"] = "PASS"',
            '"/usr/bin/codesign", "--verify", "--deep", "--strict", "--verbose=4"',
            '"/usr/bin/ditto", "-x", "-k"',
            '"--version"',
            '"accessibility": {"status": "NOT_RUN"',
            '"inputMonitoring": {"status": "NOT_RUN"',
            '"localNetwork": {"status": "NOT_RUN"',
        ):
            self.assertIn(required, script)
        self.assertNotIn('Path("/Applications")', script)

    def test_canonical_workflow_runs_real_regression_on_macos_14(self) -> None:
        workflow = (ROOT / ".github/workflows/relaydesk-build.yml").read_text(
            encoding="utf-8"
        )
        template = (
            ROOT / "product/templates/github/workflows/relaydesk-build.yml"
        ).read_text(encoding="utf-8")

        self.assertEqual(workflow, template)
        for required in (
            "macos-install-regression:",
            "runs-on: macos-14",
            "needs: package",
            "actions/download-artifact@v5",
            "test-macos-install-regression.py",
            "steps:\n      - uses: actions/checkout@v5",
            "if-no-files-found: error",
        ):
            self.assertIn(required, workflow)
        regression_job = workflow.split("  macos-install-regression:", 1)[1].split(
            "  publish-tag-artifacts:", 1
        )[0]
        self.assertNotIn("continue-on-error", regression_job)


if __name__ == "__main__":
    unittest.main()
