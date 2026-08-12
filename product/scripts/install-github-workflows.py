#!/usr/bin/env python3
"""Install the single non-gating RelayDesk GitHub Actions workflow."""

from __future__ import annotations

import argparse
import shutil
import subprocess
from pathlib import Path


def git_root(start: Path) -> Path:
    result = subprocess.run(
        ["git", "-C", str(start), "rev-parse", "--show-toplevel"],
        check=True,
        capture_output=True,
        text=True,
    )
    return Path(result.stdout.strip()).resolve()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    parser.add_argument("--package-root", type=Path)
    args = parser.parse_args()

    repo = git_root(args.repo.resolve())
    package_root = (
        args.package_root.resolve()
        if args.package_root
        else Path(__file__).resolve().parent.parent
    )
    source = package_root / "templates/github/workflows/relaydesk-build.yml"
    target = repo / ".github/workflows/relaydesk-build.yml"

    if not source.is_file():
        raise SystemExit(f"workflow template not found: {source}")

    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
    print(f"Installed workflow: {target}")
    print("No branch protection, environment approval, required review, or required check was created.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
