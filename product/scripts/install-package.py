#!/usr/bin/env python3
# Install this development package into a Deskflow source repository.

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path
from typing import NoReturn

PACKAGE_ITEMS = [
    "AGENTS.md",
    "README.md",
    "START_HERE.md",
    "CODEX_START_PROMPT.txt",
    "PROJECT_STATE.md",
    "TASK_BOARD.md",
    "docs",
    "prompts",
    "spec",
    "starter",
    "scripts",
    "config",
    "templates",
    "references",
    "working",
    "assets",
    "reuse",
    "LICENSES",
    "THIRD_PARTY_NOTICES.md",
    "PACKAGE_INFO.json",
    "VALIDATION_REPORT.md",
]


def fail(message: str) -> NoReturn:
    print(f"ERROR: {message}", file=sys.stderr)
    raise SystemExit(2)


def copy_item(source: Path, destination: Path) -> None:
    # Re-running from an already installed ``product/`` directory must be idempotent.
    if source.resolve() == destination.resolve():
        return
    if source.is_dir():
        shutil.copytree(source, destination, dirs_exist_ok=True)
    else:
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--package-root", type=Path, required=True)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--force-agents", action="store_true")
    args = parser.parse_args()

    package_root = args.package_root.resolve()
    repo = args.repo.resolve()

    if not (package_root / "AGENTS.md").is_file():
        fail(f"not a RelayDesk package root: {package_root}")
    if not (repo / ".git").exists():
        fail(f"target is not a Git worktree: {repo}")
    if not (repo / "CMakeLists.txt").is_file() or not (repo / "src").is_dir():
        fail(f"target does not look like a Deskflow source tree: {repo}")

    target_agents = repo / "AGENTS.md"
    source_agents = package_root / "AGENTS.md"
    if target_agents.exists():
        same = target_agents.read_bytes() == source_agents.read_bytes()
        if not same and not args.force_agents:
            fail(
                f"{target_agents} already exists and differs; "
                "A0 must merge it automatically or rerun with --force-agents"
            )

    shutil.copy2(source_agents, target_agents)

    product = repo / "product"
    product.mkdir(parents=True, exist_ok=True)

    for name in PACKAGE_ITEMS:
        source = package_root / name
        if source.exists():
            copy_item(source, product / name)

    print(f"Installed AGENTS.md into: {target_agents}")
    print(f"Installed development materials into: {product}")
    print("Development materials installed. A0 is responsible for committing and pushing the result.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
