# SPDX-FileCopyrightText: 2026 RelayDesk Contributors
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]


class MacosPackagingContractTests(unittest.TestCase):
    def test_package_script_has_optional_signing_and_notarization_verification(self) -> None:
        script = (ROOT / "product/scripts/package-macos.sh").read_text(encoding="utf-8")

        for required in (
            "MACOS_SIGNING_STATUS=adhoc",
            "codesign --verify --deep --strict",
            "notarytool submit",
            "--keychain-profile",
            "stapler staple",
            "stapler validate",
            "MACOS_NOTARIZATION_STATUS=not-requested",
            "--app-bundle",
            "--plan-only",
        ):
            self.assertIn(required, script)

        for forbidden in ("AC_PASSWORD", "APPLE_ID_PASSWORD", "--password"):
            self.assertNotIn(forbidden, script)

    def test_bundle_and_dmg_use_central_brand_and_signature_variant(self) -> None:
        deploy = (ROOT / "deploy/mac/deploy.cmake").read_text(encoding="utf-8")
        gui = (ROOT / "src/apps/deskflow-gui/CMakeLists.txt").read_text(encoding="utf-8")
        plist = (ROOT / "src/apps/res/deskflow.plist.in").read_text(encoding="utf-8")

        self.assertIn("RELAYDESK_MACOS_PACKAGE_VARIANT", deploy)
        self.assertIn("RELAYDESK_MACOS_SIGNING_IDENTITY", deploy)
        self.assertIn("${RELAYDESK_MACOS_ICON_SOURCE}", gui)
        self.assertIn("@BUNDLE_LOCAL_NETWORK_USAGE_DESCRIPTION@", plist)

    def test_package_readme_documents_retained_user_data(self) -> None:
        readme = (ROOT / "deploy/mac/README-macOS.txt.in").read_text(encoding="utf-8")

        self.assertIn("Removing @CMAKE_PROJECT_PROPER_NAME@.app does not remove", readme)
        self.assertIn("~/Library/RelayDesk", readme)
        self.assertIn("Downloads/RelayDesk", readme)


if __name__ == "__main__":
    unittest.main()
