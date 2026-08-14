# SPDX-FileCopyrightText: 2026 RelayDesk Contributors
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]


class MacosPackagingContractTests(unittest.TestCase):
    def test_build_script_resolves_an_explicit_macos_sdk(self) -> None:
        script = (ROOT / "product/scripts/build-macos.sh").read_text(encoding="utf-8")

        self.assertIn("xcrun --sdk macosx --show-sdk-path", script)
        self.assertIn('"-DCMAKE_OSX_SYSROOT=$MACOS_SDKROOT"', script)
        self.assertIn("RELAYDESK_MACOS_SDKROOT", script)

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
            'generate-macos-brand-assets.py" --check',
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
        self.assertIn("${RELAYDESK_MACOS_ICON_SOURCE}", deploy)
        self.assertIn("${RELAYDESK_MACOS_DMG_BACKGROUND_SOURCE}", deploy)
        self.assertNotIn('set(CPACK_PACKAGE_ICON "${MY_DIR}/dmg-volume.icns")', deploy)

    def test_relaydesk_brand_assets_are_current_and_consumed_by_qt(self) -> None:
        result = subprocess.run(
            [sys.executable, str(ROOT / "product/scripts/generate-macos-brand-assets.py"), "--check"],
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr or result.stdout)

        brand = (ROOT / "product/branding/RelayDeskBrand.cmake").read_text(encoding="utf-8")
        qrc = (ROOT / "src/apps/res/relaydesk-brand.qrc.in").read_text(encoding="utf-8")
        main_window = (ROOT / "src/lib/gui/MainWindow.cpp").read_text(encoding="utf-8")
        about = (ROOT / "src/lib/gui/dialogs/AboutDialog.cpp").read_text(encoding="utf-8")

        self.assertIn('set(RELAYDESK_MACOS_ICON_FILE "RelayDesk.icns")', brand)
        self.assertIn("@RELAYDESK_BRAND_MARK_SOURCE@", qrc)
        self.assertIn("@RELAYDESK_MACOS_MENU_BAR_TEMPLATE_SOURCE@", qrc)
        self.assertEqual(qrc.count("@CMAKE_PROJECT_REV_FQDN@.svg"), 2)
        self.assertEqual(qrc.count("@RELAYDESK_MACOS_MENU_BAR_ICON_NAME@.svg"), 2)
        self.assertIn("icon.setIsMask(true)", main_window)
        self.assertIn("QIcon::fromTheme(kRevFqdnName)", about)
        tray_function = main_window.split("void MainWindow::setTrayIcon()", 1)[1].split(
            "void MainWindow::refreshBackgroundLifecycleSettings()", 1
        )[0]
        mac_tray_branch = tray_function.split("if (deskflow::platform::isMac()) {", 1)[1].split("}", 1)[0]
        self.assertIn('themeIcon.append(QStringLiteral("-symbolic"))', mac_tray_branch)
        self.assertIn("icon.setIsMask(true)", mac_tray_branch)
        self.assertNotIn("SymbolicTrayIcon", mac_tray_branch)

    def test_configured_bundle_and_dmg_share_relaydesk_artwork(self) -> None:
        with tempfile.TemporaryDirectory(prefix="relaydesk-brand-cmake-") as temp_name:
            script = Path(temp_name) / "check.cmake"
            script.write_text(
                f'''set(CMAKE_SOURCE_DIR "{ROOT.as_posix()}")
set(CMAKE_CURRENT_SOURCE_DIR "{ROOT.as_posix()}/src/apps/deskflow-gui")
set(CMAKE_CURRENT_BINARY_DIR "{Path(temp_name).as_posix()}")
set(CMAKE_PROJECT_REV_FQDN "local.relaydesk.desktop")
include("{ROOT.as_posix()}/product/branding/RelayDeskBrand.cmake")
configure_file(
  "{ROOT.as_posix()}/src/apps/res/relaydesk-brand.qrc.in"
  "{Path(temp_name).as_posix()}/relaydesk-brand.qrc"
  @ONLY
)
''',
                encoding="utf-8",
            )
            result = subprocess.run(["cmake", "-P", str(script)], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr or result.stdout)
            qrc = (Path(temp_name) / "relaydesk-brand.qrc").read_text(encoding="utf-8")

        self.assertIn("local.relaydesk.desktop.svg", qrc)
        self.assertIn("local.relaydesk.desktop-symbolic.svg", qrc)
        self.assertIn("product/assets/branding/relaydesk-mark.svg", qrc)
        self.assertIn("product/assets/branding/generated/relaydesk-menu-bar-template.svg", qrc)

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


if __name__ == "__main__":
    unittest.main()
