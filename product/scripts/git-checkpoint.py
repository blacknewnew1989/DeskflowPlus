#!/usr/bin/env python3
"""Create traceable feature/task/phase Git checkpoints without force-pushing."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path


def run(repo: Path, *args: str, check: bool = True, capture: bool = False) -> str:
    result = subprocess.run(
        ["git", "-C", str(repo), *args],
        check=False,
        text=True,
        capture_output=capture,
    )
    if check and result.returncode != 0:
        raise SystemExit((result.stderr or result.stdout or "git command failed").strip())
    return (result.stdout or "").strip()


def root(start: Path) -> Path:
    return Path(run(start, "rev-parse", "--show-toplevel", capture=True)).resolve()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    parser.add_argument("--task", required=True)
    parser.add_argument("--type", default="feat")
    parser.add_argument("--area", required=True)
    parser.add_argument("--summary", required=True)
    parser.add_argument("--paths", nargs="*")
    parser.add_argument("--push-task", action="store_true")
    parser.add_argument("--integration-branch", default="product/relaydesk-v1")
    parser.add_argument("--phase")
    args = parser.parse_args()

    repo = root(args.repo.resolve())
    branch = run(repo, "branch", "--show-current", capture=True)
    if not branch:
        raise SystemExit("refusing to commit on detached HEAD")

    if args.paths:
        run(repo, "add", "--", *args.paths)
    else:
        run(repo, "add", "-A")

    staged = subprocess.run(
        ["git", "-C", str(repo), "diff", "--cached", "--quiet"],
        check=False,
    ).returncode != 0
    if staged:
        message = f"{args.type}({args.area}): {args.task} {args.summary}"
        run(repo, "commit", "-m", message)
    sha = run(repo, "rev-parse", "HEAD", capture=True)

    if args.push_task:
        run(repo, "fetch", "origin", "--prune")
        if branch != args.integration_branch:
            remote_ref = f"origin/{args.integration_branch}"
            if subprocess.run(
                ["git", "-C", str(repo), "rev-parse", "--verify", remote_ref],
                check=False,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            ).returncode == 0:
                run(repo, "rebase", remote_ref)
                sha = run(repo, "rev-parse", "HEAD", capture=True)
        run(repo, "push", "-u", "origin", "HEAD")

    if args.phase:
        if branch != args.integration_branch:
            raise SystemExit("phase checkpoint must be created on the integration branch")
        tag = f"relaydesk-phase-{args.phase}-complete"
        run(repo, "push", "origin", args.integration_branch)
        if subprocess.run(
            ["git", "-C", str(repo), "rev-parse", "--verify", f"refs/tags/{tag}"],
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        ).returncode != 0:
            run(repo, "tag", "-a", tag, "-m", f"Phase {args.phase} complete")
        run(repo, "push", "origin", tag)

    print(f"branch={branch}")
    print(f"commit={sha}")
    print(f"pushed={args.push_task or bool(args.phase)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
