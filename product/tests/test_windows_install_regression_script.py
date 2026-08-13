from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = (
    ROOT / "product/scripts/test-windows-install-regression.ps1"
).read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github/workflows/relaydesk-build.yml").read_text(
    encoding="utf-8"
)


class WindowsInstallRegressionScriptTests(unittest.TestCase):
    def test_requires_explicit_system_install_opt_in(self) -> None:
        self.assertIn("[switch]$AllowSystemInstall", SCRIPT)
        self.assertIn("TEST005_SYSTEM_INSTALL_OPT_IN_REQUIRED", SCRIPT)
        self.assertLess(
            SCRIPT.index("TEST005_SYSTEM_INSTALL_OPT_IN_REQUIRED"),
            SCRIPT.index("Resolve-RequiredFile -Path $MsiPath"),
        )

    def test_uses_only_an_explicit_unique_temp_root(self) -> None:
        self.assertIn('"relaydesk-test005-" + [guid]::NewGuid()', SCRIPT)
        self.assertIn("TEST005_UNSAFE_TEST_ROOT", SCRIPT)
        self.assertNotIn("Remove-Item", SCRIPT)
        self.assertIn("$ExistingRegistrations = @(Get-ProductRegistrationsByName", SCRIPT)
        self.assertIn("$ExistingFirewallRules = @(Get-RelayDeskFirewallRules", SCRIPT)

    def test_exercises_real_install_repair_and_uninstall(self) -> None:
        self.assertIn('"/i", (Quote-ProcessArgument $MsiPath)', SCRIPT)
        self.assertIn('"REINSTALL=ALL", "REINSTALLMODE=vomus"', SCRIPT)
        self.assertIn('"/x", (Format-MsiGuid', SCRIPT)
        self.assertNotIn('"/a", $MsiPath', SCRIPT)
        self.assertIn("Assert-ProductInstalled", SCRIPT)
        self.assertIn("Assert-SystemResidueRemoved", SCRIPT)

    def test_checks_service_firewall_and_real_user_data_boundary(self) -> None:
        self.assertIn("Get-Service -Name $ExpectedServiceName", SCRIPT)
        self.assertIn("Get-NetFirewallRule -DisplayName", SCRIPT)
        self.assertIn("Get-NetFirewallApplicationFilter", SCRIPT)
        self.assertIn("Join-Path $env:APPDATA $ExpectedProductName", SCRIPT)
        self.assertIn("TEST005_USER_DATA_SENTINEL_REMOVED", SCRIPT)
        self.assertIn("RelayDesk.conf, trust marker, and history marker", SCRIPT)
        self.assertIn("$UserDataFilesCreatedByHarness", SCRIPT)
        self.assertIn("foreach ($UserDataPath in $UserDataFilesCreatedByHarness)", SCRIPT)
        self.assertIn("$UserConfigPreExisted", SCRIPT)
        self.assertIn("$UserConfigBackup", SCRIPT)
        self.assertIn("backup-append-restore", SCRIPT)
        self.assertIn("[IO.File]::Copy($UserConfigBackup, $UserConfigPath, $true)", SCRIPT)
        self.assertIn("Test-ByteSubsequence", SCRIPT)
        self.assertIn("TEST005_PREEXISTING_CONFIG_CONTENT_REMOVED", SCRIPT)
        self.assertIn("preexistingConfigPreserved", SCRIPT)
        self.assertIn("unrelatedUserDataHashPreserved", SCRIPT)

    def test_exercises_same_upgrade_code_lower_version_major_upgrade(self) -> None:
        self.assertIn("New-SyntheticPreviousMsi", SCRIPT)
        self.assertIn("TEST005_PREVIOUS_PRODUCT_CODE_NOT_UNIQUE", SCRIPT)
        self.assertIn("TEST005_PREVIOUS_VERSION_NOT_LOWER", SCRIPT)
        self.assertIn("TEST005_PREVIOUS_PRODUCT_REMAINS_AFTER_UPGRADE", SCRIPT)
        self.assertIn('AllowSameVersionUpgrades="yes"', SCRIPT)
        self.assertIn("-PreviousMsiPath or -GeneratePreviousPackage", SCRIPT)
        self.assertIn("$View.Execute() | Out-Null", SCRIPT)
        self.assertIn("$View.Close() | Out-Null", SCRIPT)
        self.assertIn("$Database.Commit() | Out-Null", SCRIPT)
        self.assertIn("$Summary.Persist() | Out-Null", SCRIPT)

    def test_launches_installed_and_portable_executables_for_dependencies(self) -> None:
        self.assertIn("$PortableCore.FullName", SCRIPT)
        self.assertIn("$PortableGui.FullName", SCRIPT)
        self.assertIn('Join-Path $Installed.InstallRoot "deskflow-core.exe"', SCRIPT)
        self.assertIn('Arguments @("--version")', SCRIPT)

    def test_workflow_enables_system_test_only_for_windows_matrix(self) -> None:
        self.assertIn("id: windows_install_regression", WORKFLOW)
        self.assertIn("if: runner.os == 'Windows'", WORKFLOW)
        self.assertIn("-AllowSystemInstall", WORKFLOW)
        self.assertIn("-GeneratePreviousPackage", WORKFLOW)
        self.assertIn("steps.windows_install_regression.outcome", WORKFLOW)


if __name__ == "__main__":
    unittest.main()
