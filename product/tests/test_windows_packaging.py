from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BRAND = (ROOT / "product/branding/RelayDeskBrand.cmake").read_text(encoding="utf-8")
DEPLOY = (ROOT / "deploy/windows/deploy.cmake").read_text(encoding="utf-8")
PORTABLE = (ROOT / "deploy/windows/pre-cpack.cmake.in").read_text(encoding="utf-8")
WIX = (ROOT / "deploy/windows/wix-patch.xml.in").read_text(encoding="utf-8")
WIX_CUSTOM = (ROOT / "deploy/windows/wix-custom.cpp").read_text(encoding="utf-8")
ROOT_CPACK = (ROOT / "deploy/CMakeLists.txt").read_text(encoding="utf-8")


class WindowsPackagingTests(unittest.TestCase):
    def test_product_identity_and_upgrade_code_are_centralized(self) -> None:
        self.assertRegex(BRAND, r'set\(RELAYDESK_WINDOWS_WIX_UPGRADE_GUID "[0-9A-F-]{36}"\)')
        self.assertIn('set(RELAYDESK_WINDOWS_SERVICE_NAME "RelayDesk")', BRAND)
        self.assertIn('${RELAYDESK_WINDOWS_WIX_UPGRADE_GUID}', DEPLOY)
        self.assertIn('@RELAYDESK_WINDOWS_SERVICE_NAME@', WIX)

    def test_internal_windows_packages_are_explicitly_unsigned(self) -> None:
        self.assertIn('set(RELAYDESK_PACKAGE_VARIANT "unsigned")', DEPLOY)
        self.assertIn('string(APPEND CPACK_PACKAGE_FILE_NAME "-${RELAYDESK_PACKAGE_VARIANT}")', ROOT_CPACK)

    def test_portable_marker_matches_product_settings_contract(self) -> None:
        self.assertIn('settings/@CMAKE_PROJECT_PROPER_NAME@.conf', PORTABLE)
        self.assertNotIn('settings/Deskflow.conf', PORTABLE)
        self.assertIn('file(REMOVE ${CPACK_TEMPORARY_INSTALL_DIRECTORY}/deskflow-daemon.exe)', PORTABLE)

    def test_installer_has_private_profile_rules_and_product_strings(self) -> None:
        firewall_rules = re.findall(r'<firewall:FirewallException[^>]+/>', WIX)
        self.assertEqual(len(firewall_rules), 2)
        for rule in firewall_rules:
            self.assertIn('Profile="private"', rule)
            self.assertIn('@CMAKE_PROJECT_PROPER_NAME@', rule)
        self.assertNotIn('Name="Deskflow', WIX)
        self.assertNotIn('Value="Run Deskflow', WIX)

    def test_wix_custom_action_has_stable_cross_toolchain_name(self) -> None:
        self.assertIn('PREFIX ""', DEPLOY)
        self.assertIn('message __VA_OPT__(, ) __VA_ARGS__', WIX_CUSTOM)

    def test_installer_documents_preserved_external_data(self) -> None:
        self.assertIn('retained when this package is upgraded or uninstalled', DEPLOY)
        readme = (ROOT / "deploy/windows/README-Windows.txt.in").read_text(encoding="utf-8")
        normalized = " ".join(readme.split())
        self.assertIn('does not delete that user data', normalized)
        self.assertIn('portable folder is deleted', normalized)


if __name__ == "__main__":
    unittest.main()
