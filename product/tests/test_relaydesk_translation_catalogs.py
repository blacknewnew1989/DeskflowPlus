# SPDX-FileCopyrightText: 2026 RelayDesk Contributors
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

from __future__ import annotations

import re
import unittest
import xml.etree.ElementTree as ET
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TRANSLATIONS = ROOT / "translations"
PLURAL_FORMS = {
    "en": 2,
    "es": 2,
    "it": 2,
    "ja": 1,
    "ko": 1,
    "ru": 3,
    "zh_CN": 1,
}
PLACEHOLDER = re.compile(r"%(?:[1-9]|n)")
ENGLISH_WORD = re.compile(r"[A-Za-z]+")
HTML_TAG = re.compile(r"<[^>]+>")


def messages(path: Path) -> dict[str, ET.Element]:
    root = ET.parse(path).getroot()
    context = next(
        candidate
        for candidate in root.findall("context")
        if candidate.findtext("name") == "RelayDesk"
    )
    result: dict[str, ET.Element] = {}
    for message in context.findall("message"):
        source = message.findtext("source")
        if source is None:
            continue
        if source in result:
            raise AssertionError(f"duplicate semantic key in {path.name}: {source}")
        result[source] = message
    return result


def translation_forms(message: ET.Element) -> list[str]:
    translation = message.find("translation")
    if translation is None or translation.get("type") == "unfinished":
        return []
    numerus = translation.findall("numerusform")
    if numerus:
        return [(form.text or "").strip() for form in numerus]
    return [(translation.text or "").strip()]


class RelayDeskTranslationCatalogTests(unittest.TestCase):
    def test_every_selectable_language_has_a_complete_semantic_catalog(self) -> None:
        english = messages(TRANSLATIONS / "relaydesk_en.ts")
        self.assertGreater(len(english), 150)

        for language, plural_count in PLURAL_FORMS.items():
            with self.subTest(language=language):
                catalog_path = TRANSLATIONS / f"relaydesk_{language}.ts"
                self.assertTrue(catalog_path.is_file(), catalog_path)
                catalog = messages(catalog_path)
                self.assertEqual(set(catalog), set(english))

                for key, english_message in english.items():
                    forms = translation_forms(catalog[key])
                    self.assertTrue(forms, f"{catalog_path.name}: empty {key}")
                    self.assertNotIn("", forms, f"{catalog_path.name}: empty {key}")

                    is_plural = english_message.get("numerus") == "yes"
                    self.assertEqual(
                        len(forms),
                        plural_count if is_plural else 1,
                        f"{catalog_path.name}: plural forms for {key}",
                    )

                    expected_placeholders = Counter(
                        PLACEHOLDER.findall(translation_forms(english_message)[0])
                    )
                    for form in forms:
                        self.assertEqual(
                            Counter(PLACEHOLDER.findall(form)),
                            expected_placeholders,
                            f"{catalog_path.name}: placeholders for {key}",
                        )

    def test_inherited_ui_catalogs_have_no_english_fallback_gaps(self) -> None:
        for language, plural_count in PLURAL_FORMS.items():
            catalog_path = TRANSLATIONS / f"deskflow_{language}.ts"
            with self.subTest(language=language):
                self.assertTrue(catalog_path.is_file(), catalog_path)
                semantic_catalog = messages(
                    TRANSLATIONS / f"relaydesk_{language}.ts"
                )
                root = ET.parse(catalog_path).getroot()
                checked = 0
                for context in root.findall("context"):
                    context_name = context.findtext("name") or "<unnamed>"
                    for message in context.findall("message"):
                        # The English Deskflow catalog is intentionally
                        # plural-only. RelayDesk's complete English semantic
                        # catalog is checked separately above.
                        if language == "en" and context_name == "RelayDesk":
                            continue

                        translation = message.find("translation")
                        if translation is None or translation.get("type") in {"obsolete", "vanished"}:
                            continue

                        source = message.findtext("source") or ""
                        forms = translation.findall("numerusform")
                        values = [(form.text or "").strip() for form in forms] if forms else [
                            (translation.text or "").strip()
                        ]
                        label = f"{catalog_path.name}:{context_name}:{source}"
                        self.assertNotEqual(translation.get("type"), "unfinished", label)
                        self.assertTrue(all(values), label)
                        if message.get("numerus") == "yes":
                            self.assertEqual(len(values), plural_count, label)
                        if language != "en" and not forms:
                            english_words = ENGLISH_WORD.findall(
                                HTML_TAG.sub(" ", source)
                            )
                            if len(english_words) >= 4:
                                self.assertNotEqual(values[0], source, label)

                        # lupdate also discovers RelayDesk's semantic keys in
                        # the inherited application catalog.  Those keys do
                        # not contain their runtime placeholders, so compare
                        # the duplicate entries with the canonical semantic
                        # catalog instead of comparing them with the key text.
                        if context_name == "RelayDesk":
                            self.assertIn(source, semantic_catalog, label)
                            self.assertEqual(
                                values,
                                translation_forms(semantic_catalog[source]),
                                label,
                            )
                            checked += 1
                            continue

                        expected = Counter(PLACEHOLDER.findall(source))
                        for value in values:
                            actual = Counter(PLACEHOLDER.findall(value))
                            form_expected = expected
                            if language == "en" and message.get("numerus") == "yes" and "%n" not in actual:
                                form_expected = expected.copy()
                                del form_expected["%n"]
                            self.assertEqual(actual, form_expected, label)
                        checked += 1

                self.assertGreater(
                    checked,
                    1 if language == "en" else 250,
                    catalog_path.name,
                )

    def test_cmake_packages_exactly_the_complete_catalog_set(self) -> None:
        cmake = (TRANSLATIONS / "CMakeLists.txt").read_text(encoding="utf-8")
        for language in PLURAL_FORMS:
            self.assertIn(
                f"${{RELAYDESK_TRANSLATION_CATALOG}}_{language}.ts",
                cmake,
            )


if __name__ == "__main__":
    unittest.main()
