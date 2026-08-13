# SPDX-FileCopyrightText: 2026 RelayDesk Contributors
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]


class MacosPackagingContractTests(unittest.TestCase):
    def test_macos_entrypoint_scripts_are_executable(self) -> None:
        for relative_path in (
            "product/scripts/setup-macos.sh",
            "product/scripts/build-macos.sh",
            "product/scripts/package-macos.sh",
            "deploy/mac/sanitize_bundle_rpaths.sh",
        ):
            script = ROOT / relative_path
            self.assertNotEqual(
                script.stat().st_mode & 0o111,
                0,
                f"{relative_path} must be executable from a Git checkout",
            )

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
            "/private/tmp/relaydesk-package.XXXXXX",
            'cpack --config "$BUILD_DIR/CPackConfig.cmake" -B "$PACKAGE_TEMP"',
            '--build-dir "$PACKAGE_TEMP"',
        ):
            self.assertIn(required, script)

        for forbidden in ("AC_PASSWORD", "APPLE_ID_PASSWORD", "--password", "-maxdepth"):
            self.assertNotIn(forbidden, script)

    def test_bundle_and_dmg_use_central_brand_and_signature_variant(self) -> None:
        deploy = (ROOT / "deploy/mac/deploy.cmake").read_text(encoding="utf-8")
        dmg_layout = (ROOT / "deploy/mac/generate_ds_store.applescript").read_text(
            encoding="utf-8"
        )
        gui = (ROOT / "src/apps/deskflow-gui/CMakeLists.txt").read_text(encoding="utf-8")
        plist = (ROOT / "src/apps/res/deskflow.plist.in").read_text(encoding="utf-8")

        self.assertIn("RELAYDESK_MACOS_PACKAGE_VARIANT", deploy)
        self.assertIn("RELAYDESK_MACOS_SIGNING_IDENTITY", deploy)
        self.assertIn("RELAYDESK_MACOS_CUSTOM_DMG_LAYOUT", deploy)
        self.assertIn("Use Finder automation to apply the custom RelayDesk DMG layout", deploy)
        self.assertIn("if(RELAYDESK_MACOS_CUSTOM_DMG_LAYOUT)", deploy)
        self.assertLess(
            deploy.index("if(RELAYDESK_MACOS_CUSTOM_DMG_LAYOUT)"),
            deploy.index("CPACK_DMG_DS_STORE_SETUP_SCRIPT"),
        )
        self.assertIn("with timeout of 30 seconds", dmg_layout)
        self.assertIn("custom Finder layout skipped", dmg_layout)
        self.assertIn("${RELAYDESK_MACOS_ICON_SOURCE}", gui)
        self.assertIn("@BUNDLE_LOCAL_NETWORK_USAGE_DESCRIPTION@", plist)
        self.assertIn('-executable=\\${relaydesk_core}', deploy)
        self.assertIn('-libpath=${RELAYDESK_QT_LIBRARY_PATH}', deploy)
        self.assertIn('"${RELAYDESK_QTPATHS}" --query QT_INSTALL_LIBS', deploy)
        self.assertIn("OUTPUT_STRIP_TRAILING_WHITESPACE", deploy)
        self.assertEqual(deploy.count('COMMAND \\"${DEPLOYQT}\\"'), 2)
        self.assertIn('\\"-no-codesign\\"', deploy)
        self.assertIn("discovering RelayDesk plugins", deploy)
        self.assertIn("sanitize_bundle_rpaths.sh", deploy)

    def test_package_readme_documents_retained_user_data(self) -> None:
        readme = (ROOT / "deploy/mac/README-macOS.txt.in").read_text(encoding="utf-8")

        self.assertIn("Removing @CMAKE_PROJECT_PROPER_NAME@.app does not remove", readme)
        self.assertIn("~/Library/RelayDesk", readme)
        self.assertIn("Downloads/RelayDesk", readme)

    def test_rpath_sanitizer_removes_external_paths_before_final_signing(self) -> None:
        script = (ROOT / "deploy/mac/sanitize_bundle_rpaths.sh").read_text(
            encoding="utf-8"
        )

        self.assertIn('install_name_tool -delete_rpath "$rpath"', script)
        self.assertIn('codesign --force --deep --sign -', script)
        self.assertLess(
            script.index('install_name_tool -delete_rpath "$rpath"'),
            script.index('codesign --force --deep --sign -'),
        )

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

    def test_local_build_resolves_and_pins_the_active_macos_sdk(self) -> None:
        script = (ROOT / "product/scripts/build-macos.sh").read_text(encoding="utf-8")

        self.assertIn("xcrun --sdk macosx --show-sdk-path", script)
        self.assertIn('"-DCMAKE_OSX_SYSROOT=$MACOS_SDK"', script)
        self.assertIn('[[ -n "$MACOS_SDK" && -d "$MACOS_SDK" ]]', script)

    def test_local_build_compiles_catalogs_without_rewriting_sources(self) -> None:
        script = (ROOT / "product/scripts/build-macos.sh").read_text(encoding="utf-8")
        translations = (ROOT / "translations/CMakeLists.txt").read_text(encoding="utf-8")

        self.assertIn("-DRELAYDESK_UPDATE_TRANSLATION_SOURCES=OFF", script)
        self.assertIn("if(RELAYDESK_UPDATE_TRANSLATION_SOURCES)", translations)
        self.assertIn("qt_create_translation(", translations)
        self.assertIn("qt_add_translation(TRS", translations)
        self.assertIn("qt_add_translation(RELAYDESK_TRS", translations)


if __name__ == "__main__":
    unittest.main()
