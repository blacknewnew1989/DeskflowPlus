import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TRANSLATIONS_CMAKE = (ROOT / "translations/CMakeLists.txt").read_text(encoding="utf-8")
GUI_TESTS_CMAKE = (ROOT / "src/unittests/gui/CMakeLists.txt").read_text(encoding="utf-8")


class TranslationBuildContractTests(unittest.TestCase):
    def test_normal_build_compiles_catalogs_without_updating_sources(self) -> None:
        self.assertNotIn("qt_create_translation(", TRANSLATIONS_CMAKE)
        self.assertGreaterEqual(TRANSLATIONS_CMAKE.count("qt_add_translation("), 2)

    def test_layout_test_receives_chinese_app_and_product_catalogs(self) -> None:
        self.assertIn(
            "add_dependencies(MainWindowLayoutTests app_translations)",
            GUI_TESTS_CMAKE,
        )
        self.assertRegex(
            GUI_TESTS_CMAKE,
            r"add_custom_target\(\s*MainWindowLayoutTestTranslations",
        )
        self.assertIn(
            "add_dependencies(MainWindowLayoutTests MainWindowLayoutTestTranslations)",
            GUI_TESTS_CMAKE,
        )
        self.assertIn("${CMAKE_PROJECT_NAME}_zh_CN.qm", GUI_TESTS_CMAKE)
        self.assertIn("${RELAYDESK_TRANSLATION_CATALOG}_zh_CN.qm", GUI_TESTS_CMAKE)
        self.assertIn(
            "$<TARGET_FILE_DIR:MainWindowLayoutTests>/translations",
            GUI_TESTS_CMAKE,
        )


if __name__ == "__main__":
    unittest.main()
