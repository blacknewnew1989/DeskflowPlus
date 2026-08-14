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

        for forbidden in ("AC_PASSWORD", "APPLE_ID_PASSWORD", "--password", "-maxdepth"):
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

    def test_existing_actions_workflow_collects_deployed_adhoc_app(self) -> None:
        workflow = (ROOT / ".github/workflows/relaydesk-build.yml").read_text(encoding="utf-8")

        self.assertIn("Stage deployed macOS app bundle", workflow)
        self.assertIn("package_variant: adhoc", workflow)
        self.assertIn("--app-bundle", workflow)
        self.assertNotIn("required checks", workflow)

    def test_all_bundle_resources_are_installed_before_macdeployqt_signing(self) -> None:
        root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        deploy = (ROOT / "deploy/mac/deploy.cmake").read_text(encoding="utf-8")

        self.assertLess(
            root_cmake.index("FILES ${PROJECT_SOURCE_DIR}/LICENSE"),
            root_cmake.index("add_subdirectory(deploy)"),
        )
        self.assertLess(
            deploy.index('FILES "${CMAKE_CURRENT_BINARY_DIR}/README-macOS.txt"'),
            deploy.index('install(CODE "'),
        )

    def test_actions_stage_verifies_the_final_app_after_install(self) -> None:
        workflow = (ROOT / ".github/workflows/relaydesk-build.yml").read_text(encoding="utf-8")
        stage = workflow.split("- name: Stage deployed macOS app bundle", 1)[1].split(
            "- name: Run tests and keep diagnostics", 1
        )[0]

        self.assertLess(stage.index("cmake --install"), stage.index("codesign --verify"))
        self.assertIn("--deep --strict --verbose=4", stage)

    def test_real_app_and_dmg_bundles_enforce_the_shared_translation_manifest(self) -> None:
        package = (ROOT / "product/scripts/package-macos.sh").read_text(encoding="utf-8")
        lifecycle = (ROOT / "product/scripts/test-macos-install-regression.py").read_text(
            encoding="utf-8"
        )
        verifier = (
            ROOT / "product/scripts/verify-macos-translation-bundle.py"
        ).read_text(encoding="utf-8")
        workflow = (ROOT / ".github/workflows/relaydesk-build.yml").read_text(
            encoding="utf-8"
        )

        self.assertIn("verify-macos-translation-bundle.py", package)
        self.assertIn("verify-macos-translation-bundle.py", workflow)
        self.assertGreaterEqual(lifecycle.count("verify_translation_bundle("), 3)
        self.assertIn('Path("translations/RelayDeskLanguages.cmake")', verifier)
        self.assertIn('LANGUAGE_VARIABLE = "RELAYDESK_SUPPORTED_LANGUAGES"', verifier)
        self.assertIn('bundleResourcePath": "Contents/Resources/translations"', verifier)


if __name__ == "__main__":
    unittest.main()
