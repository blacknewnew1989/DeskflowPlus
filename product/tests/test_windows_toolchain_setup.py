from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SETUP = (ROOT / "product/scripts/setup-windows.ps1").read_text(encoding="utf-8")
BUILD = (ROOT / "product/scripts/build-windows.ps1").read_text(encoding="utf-8")
PACKAGE = (ROOT / "product/scripts/package-windows.ps1").read_text(encoding="utf-8")


class WindowsToolchainSetupTests(unittest.TestCase):
    def test_qt_setup_installs_and_requires_declarative_deployment_tools(self) -> None:
        self.assertRegex(
            SETUP,
            r'foreach \(\$archive in @\([^)]*"qtdeclarative"[^)]*\)\)',
        )
        self.assertIn(r'"bin\qmlimportscanner.exe"', SETUP)

    def test_windows_build_uses_stable_msvc_dependency_output(self) -> None:
        self.assertIn('$env:VSLANG = "1033"', BUILD)
        self.assertIn('& chcp.com 65001 | Out-Null', BUILD)
        self.assertIn('$OutputEncoding = $Utf8NoBom', BUILD)

    def test_release_packaging_uses_guarded_clean_build(self) -> None:
        self.assertIn('[switch]$CleanBuild', BUILD)
        self.assertIn('BUILD_CLEAN_PATH_OUTSIDE_REPOSITORY', BUILD)
        self.assertRegex(PACKAGE, r'-PackageVariant \$SigningPlan\.Status\s+`\s*\n\s*-CleanBuild')


if __name__ == "__main__":
    unittest.main()
