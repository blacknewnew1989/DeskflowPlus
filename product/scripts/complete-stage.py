#!/usr/bin/env python3
"""Commit stage metadata, push integration branch/tag, and monitor package jobs."""

from __future__ import annotations

import argparse
import datetime as dt
import re
import shutil
import subprocess
import sys
from pathlib import Path

INTEGRATION_BRANCH = "product/relaydesk-v1"


def run(args: list[str], cwd: Path, *, capture: bool = False, check: bool = True) -> str:
    print("+", " ".join(args))
    result = subprocess.run(
        args,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.PIPE if capture else None,
        check=False,
    )
    if check and result.returncode != 0:
        raise RuntimeError((result.stderr or result.stdout or "command failed").strip())
    return (result.stdout or "").strip()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", required=True)
    parser.add_argument("--summary", required=True)
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    parser.add_argument("--tests", default="See build/test logs for this stage")
    parser.add_argument("--skip-actions-monitor", action="store_true")
    args = parser.parse_args()

    repo = Path(run(["git", "rev-parse", "--show-toplevel"], args.repo, capture=True)).resolve()
    branch = run(["git", "branch", "--show-current"], repo, capture=True)
    if branch != INTEGRATION_BRANCH:
        raise RuntimeError(
            f"stage completion must run on {INTEGRATION_BRANCH}; current branch is {branch or 'detached'}"
        )

    now = dt.datetime.now().astimezone()
    safe_stage = re.sub(r"[^a-zA-Z0-9._-]+", "-", args.stage).strip("-").lower()
    tag = f"relaydesk-{safe_stage}-{now.strftime('%Y%m%d-%H%M%S')}"
    report = repo / "product/working/stages" / f"{tag}.md"
    report.parent.mkdir(parents=True, exist_ok=True)
    head = run(["git", "rev-parse", "HEAD"], repo, capture=True)
    report.write_text(
        "\n".join(
            [
                f"# Stage Report: {args.stage}",
                "",
                f"- Time: {now.isoformat(timespec='seconds')}",
                f"- Branch: `{branch}`",
                f"- Starting HEAD: `{head}`",
                f"- Summary: {args.summary}",
                f"- Tests: {args.tests}",
                "- User action required: none until final acceptance",
                "",
                "## Actions and artifacts",
                "",
                "The automatic build result is recorded in `product/working/actions/<run-id>.json`, including artifact SHA-256 values.",
                "",
            ]
        ),
        encoding="utf-8",
    )

    report_path = str(report.relative_to(repo))
    run(["git", "add", "-f", "--", report_path], repo)
    optional_paths = [
        item
        for item in ("product/PROJECT_STATE.md", "product/TASK_BOARD.md")
        if (repo / item).exists()
    ]
    if optional_paths:
        run(["git", "add", "--", *optional_paths], repo)
    staged = subprocess.run(["git", "diff", "--cached", "--quiet"], cwd=repo, check=False).returncode != 0
    if staged:
        run(["git", "commit", "-m", f"chore(stage): {args.stage} {args.summary}"], repo)

    run(["git", "push", "-u", "origin", INTEGRATION_BRANCH], repo)
    run(["git", "tag", "-a", tag, "-m", f"RelayDesk {args.stage}: {args.summary}"], repo)
    run(["git", "push", "origin", tag], repo)

    monitored = False
    runner = repo / "product/scripts/run-github-actions.py"
    if not args.skip_actions_monitor and runner.is_file() and shutil.which("gh"):
        result = subprocess.run(
            [sys.executable, str(runner), "--repo", str(repo), "--ref", tag, "--no-trigger"],
            cwd=repo,
            check=False,
        )
        monitored = result.returncode == 0

    print(f"STAGE_TAG={tag}")
    print(f"ACTIONS_MONITORED={str(monitored).lower()}")
    if not monitored:
        print("ACTIONS_MONITOR_REQUIRED_BY_A0=true")
    print("USER_ACTION_REQUIRED=none")
    if runner.is_file() and shutil.which("gh") and not args.skip_actions_monitor and not monitored:
        return 3
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # noqa: BLE001
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(2)
