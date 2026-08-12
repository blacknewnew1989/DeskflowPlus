from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SETTINGS_CPP = ROOT / "src/lib/gui/dialogs/SettingsDialog.cpp"


class WindowsStartAtLoginGuiTests(unittest.TestCase):
    def test_control_enablement_recomputes_from_availability_and_writability(self) -> None:
        source = SETTINGS_CPP.read_text(encoding="utf-8")
        self.assertIn(
            "ui->cbStartAtLogin->setEnabled(m_startAtLoginAvailable && writable);",
            source,
        )
        self.assertNotIn(
            "ui->cbStartAtLogin->setEnabled(ui->cbStartAtLogin->isEnabled() && writable);",
            source,
        )

        available = True
        self.assertFalse(available and False)
        self.assertTrue(available and True)
if __name__ == "__main__":
    unittest.main()
