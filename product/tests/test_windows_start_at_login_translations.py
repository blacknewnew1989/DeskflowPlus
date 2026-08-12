from __future__ import annotations

import re
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ZH_CN = ROOT / "translations/deskflow_zh_CN.ts"


def settings_dialog_translations(path: Path) -> dict[str, str]:
    root = ET.parse(path).getroot()
    for context in root.findall("context"):
        if context.findtext("name") != "SettingsDialog":
            continue
        return {
            message.findtext("source", default=""): message.findtext("translation", default="")
            for message in context.findall("message")
        }
    raise AssertionError("SettingsDialog translation context is missing")


class WindowsStartAtLoginTranslationTests(unittest.TestCase):
    def test_chinese_catalog_covers_start_at_login_strings(self) -> None:
        translations = settings_dialog_translations(ZH_CN)
        expected_sources = {
            "Start with system",
            "Start-at-login status could not be read (code %1, native %2). %3",
            "The application path will be updated when preferences are saved.",
            "Start at login",
            "The start-at-login setting could not be updated (code %1, native %2).\n%3",
        }
        for source in expected_sources:
            self.assertIn(source, translations)
            translation = translations[source]
            self.assertTrue(translation.strip())
            self.assertEqual(
                set(re.findall(r"%\d+", source)),
                set(re.findall(r"%\d+", translation)),
            )


if __name__ == "__main__":
    unittest.main()
