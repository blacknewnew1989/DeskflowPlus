from __future__ import annotations

import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[1] / "run-github-actions.py"
SPEC = importlib.util.spec_from_file_location("run_github_actions", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class OriginRepositoryTests(unittest.TestCase):
    def test_automatic_report_commit_message_is_simplified_chinese(self) -> None:
        source = SCRIPT.read_text(encoding="utf-8")
        self.assertIn('f"记录(Actions): 保存运行 {run_id} [skip ci]"', source)
        self.assertNotIn("chore(actions): record run", source)

    def test_resolves_https_origin_and_ignores_upstream(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            subprocess.run(["git", "init", str(repo)], check=True, capture_output=True)
            subprocess.run(
                [
                    "git",
                    "-C",
                    str(repo),
                    "remote",
                    "add",
                    "origin",
                    "https://github.com/example-owner/relaydesk.git",
                ],
                check=True,
            )
            subprocess.run(
                [
                    "git",
                    "-C",
                    str(repo),
                    "remote",
                    "add",
                    "upstream",
                    "https://github.com/deskflow/deskflow.git",
                ],
                check=True,
            )

            self.assertEqual(
                MODULE.origin_repository(repo), "example-owner/relaydesk"
            )

    def test_gh_repository_command_always_targets_origin_repository(self) -> None:
        self.assertEqual(
            MODULE.gh_repository_command(
                "example-owner/relaydesk", "run", "list", "--limit", "1"
            ),
            [
                "gh",
                "run",
                "list",
                "--limit",
                "1",
                "-R",
                "example-owner/relaydesk",
            ],
        )


if __name__ == "__main__":
    unittest.main()
