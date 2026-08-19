from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SETUP = (ROOT / "product/scripts/setup-windows.ps1").read_text(encoding="utf-8")


class WindowsToolchainSetupTests(unittest.TestCase):
    def test_qt_setup_installs_and_requires_declarative_deployment_tools(self) -> None:
        self.assertRegex(
            SETUP,
            r'foreach \(\$archive in @\([^)]*"qtdeclarative"[^)]*\)\)',
        )
        self.assertIn(r'"bin\qmlimportscanner.exe"', SETUP)


if __name__ == "__main__":
    unittest.main()
