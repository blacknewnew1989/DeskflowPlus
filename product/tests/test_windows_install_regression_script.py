from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = (
    ROOT / "product/scripts/test-windows-install-regression.ps1"
).read_text(encoding="utf-8")


class WindowsInstallRegressionScriptTests(unittest.TestCase):
    def test_uses_only_an_explicit_unique_temp_root(self) -> None:
        self.assertIn('"relaydesk-test005-" + [guid]::NewGuid()', SCRIPT)
        self.assertIn("TEST005_UNSAFE_TEST_ROOT", SCRIPT)
        self.assertNotIn("Remove-Item", SCRIPT)

    def test_uses_administrative_install_not_machine_install(self) -> None:
        self.assertIn('"/a", $MsiPath', SCRIPT)
        self.assertNotIn('"/i", $MsiPath', SCRIPT)
        self.assertNotIn('"/x",', SCRIPT)
        self.assertIn("automatic service and firewall rules", SCRIPT)

    def test_checks_identity_unsigned_and_user_data_boundary(self) -> None:
        self.assertIn("RELAYDESK_WINDOWS_WIX_UPGRADE_GUID", SCRIPT)
        self.assertIn("Get-AuthenticodeSignature", SCRIPT)
        self.assertIn("TEST005_UPGRADE_CODE_MISMATCH", SCRIPT)
        self.assertIn("TEST005_USER_DATA_SENTINEL_REMOVED", SCRIPT)

    def test_launches_both_portable_executables_for_dependencies(self) -> None:
        self.assertIn("$PortableCore.FullName", SCRIPT)
        self.assertIn("$PortableGui.FullName", SCRIPT)
        self.assertIn('Arguments @("--version")', SCRIPT)


if __name__ == "__main__":
    unittest.main()
