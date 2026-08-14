# SPDX-FileCopyrightText: 2026 RelayDesk Contributors
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[1] / "verify-macos-translation-bundle.py"
SPEC = importlib.util.spec_from_file_location("verify_macos_translation_bundle", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


class MacosTranslationBundleContractTests(unittest.TestCase):
    @staticmethod
    def _write_language_manifest(root: Path, languages: tuple[str, ...]) -> None:
        manifest = root / MODULE.LANGUAGE_MANIFEST
        manifest.parent.mkdir(parents=True)
        manifest.write_text(
            "set(RELAYDESK_SUPPORTED_LANGUAGES\n  "
            + "\n  ".join(languages)
            + "\n)\n",
            encoding="utf-8",
        )

    @staticmethod
    def _write_bundle(root: Path, languages: tuple[str, ...]) -> Path:
        app = root / "RelayDesk.app"
        translations = app / "Contents" / "Resources" / "translations"
        translations.mkdir(parents=True)
        for language in languages:
            (translations / f"relaydesk_{language}.qm").write_bytes(
                MODULE.QM_MAGIC + f"fixture-{language}".encode("ascii")
            )
        return app

    def test_consumes_shared_manifest_without_a_platform_language_list(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            languages = ("en", "fr", "zh_CN")
            self._write_language_manifest(root, languages)
            app = self._write_bundle(root, languages)

            result = MODULE.verify_bundle(root, app)

            self.assertEqual(result["supportedLanguages"], list(languages))
            self.assertEqual(
                [item["name"] for item in result["catalogs"]],
                [f"relaydesk_{language}.qm" for language in languages],
            )
            self.assertTrue(all(item["qtQmHeader"] == "PASS" for item in result["catalogs"]))

    def test_rejects_missing_catalog_from_shared_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            languages = ("en", "fr")
            self._write_language_manifest(root, languages)
            app = self._write_bundle(root, languages[:-1])

            with self.assertRaisesRegex(MODULE.TranslationBundleError, "CATALOG_MISSING"):
                MODULE.verify_bundle(root, app)

    def test_rejects_platform_only_catalog(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            languages = ("en",)
            self._write_language_manifest(root, languages)
            app = self._write_bundle(root, languages + ("fr",))

            with self.assertRaisesRegex(MODULE.TranslationBundleError, "CATALOG_UNEXPECTED"):
                MODULE.verify_bundle(root, app)

    def test_rejects_file_that_is_not_a_qt_qm(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            languages = ("en",)
            self._write_language_manifest(root, languages)
            app = self._write_bundle(root, languages)
            catalog = app / "Contents" / "Resources" / "translations" / "relaydesk_en.qm"
            catalog.write_bytes(b"not-a-qm")

            with self.assertRaisesRegex(MODULE.TranslationBundleError, "CATALOG_QM_INVALID"):
                MODULE.verify_bundle(root, app)

    def test_rejects_duplicate_language_in_shared_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self._write_language_manifest(root, ("en", "en"))

            with self.assertRaisesRegex(MODULE.TranslationBundleError, "LANGUAGE_LIST_DUPLICATE"):
                MODULE.supported_languages(root / MODULE.LANGUAGE_MANIFEST)


if __name__ == "__main__":
    unittest.main()
