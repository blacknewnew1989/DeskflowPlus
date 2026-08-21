#!/usr/bin/env python3
"""Trigger/monitor RelayDesk GitHub Actions and download every artifact.

A0/A7 owns this operation. The script never transfers GitHub work to the user.
It resolves annotated tags to their commit, avoids reusing a stale workflow run
when a new dispatch was requested, records failed logs, calculates checksums for
downloaded artifacts, and commits the lightweight run report when safe.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import Any
from urllib.parse import urlparse


class CommandError(RuntimeError):
    pass


def run(
    args: list[str],
    cwd: Path,
    *,
    capture: bool = False,
    check: bool = True,
) -> str:
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
        raise CommandError((result.stderr or result.stdout or "command failed").strip())
    return (result.stdout or "").strip()


def run_result(args: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    print("+", " ".join(args))
    return subprocess.run(
        args,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def git(repo: Path, *args: str, capture: bool = False, check: bool = True) -> str:
    return run(["git", *args], repo, capture=capture, check=check)


def origin_repository(repo: Path) -> str:
    origin_url = git(repo, "remote", "get-url", "origin", capture=True)
    parsed = urlparse(origin_url)
    if parsed.scheme in {"http", "https"} and parsed.hostname == "github.com":
        repository = parsed.path.strip("/")
    elif origin_url.startswith("git@github.com:"):
        repository = origin_url.removeprefix("git@github.com:")
    else:
        raise CommandError(f"unsupported GitHub origin URL: {origin_url}")
    repository = repository.removesuffix(".git")
    if repository.count("/") != 1 or not all(repository.split("/")):
        raise CommandError(f"invalid GitHub origin repository: {origin_url}")
    return repository


def gh_repository_command(repository: str, *args: str) -> list[str]:
    return ["gh", *args, "-R", repository]


def list_runs(
    repo: Path, workflow: str, repository: str
) -> list[dict[str, Any]]:
    output = run(
        gh_repository_command(
            repository,
            "run",
            "list",
            "--workflow",
            workflow,
            "--limit",
            "50",
            "--json",
            (
                "databaseId,headSha,headBranch,displayTitle,status,conclusion,"
                "createdAt,event,url"
            ),
        ),
        repo,
        capture=True,
    )
    value = json.loads(output or "[]")
    if not isinstance(value, list):
        raise CommandError("gh run list returned an unexpected payload")
    return value


def run_matches(item: dict[str, Any], *, ref: str, head_sha: str) -> bool:
    if item.get("headSha") != head_sha:
        return False
    head_branch = str(item.get("headBranch") or "")
    # GitHub normally reports the source branch or tag here. Keep a SHA-only
    # fallback because some workflow-dispatch runs omit/normalize headBranch.
    return not head_branch or head_branch == ref


def latest_matching_run(
    runs: list[dict[str, Any]],
    *,
    ref: str,
    head_sha: str,
    excluded_ids: set[int] | None = None,
) -> dict[str, Any] | None:
    excluded_ids = excluded_ids or set()
    candidates = [
        item
        for item in runs
        if run_matches(item, ref=ref, head_sha=head_sha)
        and int(item.get("databaseId") or 0) not in excluded_ids
    ]
    if not candidates:
        # Fallback to the commit when GitHub normalized headBranch differently.
        candidates = [
            item
            for item in runs
            if item.get("headSha") == head_sha
            and int(item.get("databaseId") or 0) not in excluded_ids
        ]
    if not candidates:
        return None
    candidates.sort(key=lambda item: str(item.get("createdAt") or ""), reverse=True)
    return candidates[0]


def wait_for_run(
    repo: Path,
    workflow: str,
    repository: str,
    ref: str,
    head_sha: str,
    timeout: int,
    *,
    excluded_ids: set[int] | None = None,
) -> dict[str, Any]:
    deadline = time.time() + timeout
    while time.time() < deadline:
        match = latest_matching_run(
            list_runs(repo, workflow, repository),
            ref=ref,
            head_sha=head_sha,
            excluded_ids=excluded_ids,
        )
        if match is not None:
            return match
        time.sleep(5)
    raise CommandError(f"workflow run did not appear for {ref} / {head_sha}")


def sha256(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(block)
    return hasher.hexdigest()


def checksum_downloads(download_dir: Path) -> list[dict[str, Any]]:
    entries: list[dict[str, Any]] = []
    lines: list[str] = []
    for path in sorted(download_dir.rglob("*")):
        if not path.is_file() or path.name == "DOWNLOAD_SHA256SUMS.txt":
            continue
        relative = path.relative_to(download_dir).as_posix()
        digest = sha256(path)
        entries.append(
            {"path": relative, "size": path.stat().st_size, "sha256": digest}
        )
        lines.append(f"{digest}  {relative}")
    if lines:
        (download_dir / "DOWNLOAD_SHA256SUMS.txt").write_text(
            "\n".join(lines) + "\n", encoding="ascii"
        )
    return entries


def portable_path(repo: Path, path: Path) -> str:
    """Prefer repository-relative paths in tracked reports."""
    try:
        return path.resolve().relative_to(repo.resolve()).as_posix()
    except ValueError:
        return str(path.resolve())


def commit_report_if_safe(repo: Path, report_path: Path, run_id: str) -> bool:
    branch = git(repo, "branch", "--show-current", capture=True)
    if not branch:
        print("REPORT_COMMIT_DEFERRED=detached-head")
        return False

    pre_staged = subprocess.run(
        ["git", "diff", "--cached", "--quiet"], cwd=repo, check=False
    ).returncode != 0
    if pre_staged:
        print("REPORT_COMMIT_DEFERRED=pre-existing-staged-changes")
        return False

    relative = report_path.relative_to(repo).as_posix()
    git(repo, "add", "-f", "--", relative)
    staged = subprocess.run(
        ["git", "diff", "--cached", "--quiet"], cwd=repo, check=False
    ).returncode != 0
    if not staged:
        return False

    git(repo, "commit", "-m", f"记录(Actions): 保存运行 {run_id} [skip ci]")
    git(repo, "push", "origin", branch)
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    parser.add_argument("--workflow", default="relaydesk-build.yml")
    parser.add_argument("--ref", default="product/relaydesk-v1")
    parser.add_argument("--download-dir", type=Path)
    parser.add_argument("--no-trigger", action="store_true")
    parser.add_argument("--timeout", type=int, default=1800)
    parser.add_argument("--no-commit-report", action="store_true")
    args = parser.parse_args()

    if shutil.which("gh") is None:
        raise CommandError(
            "gh CLI is unavailable; A0 must use the connected GitHub tool to "
            "monitor runs, read logs, and download artifacts"
        )

    repo = Path(
        git(args.repo.resolve(), "rev-parse", "--show-toplevel", capture=True)
    ).resolve()
    repository = origin_repository(repo)
    run(["gh", "auth", "status"], repo)
    git(repo, "fetch", "origin", "--prune", "--tags")
    # ``^{commit}`` is required for annotated stage tags; GitHub headSha is the
    # commit SHA, not the annotated tag-object SHA.
    head_sha = git(repo, "rev-parse", f"{args.ref}^{{commit}}", capture=True)

    before = list_runs(repo, args.workflow, repository)
    before_ids = {int(item.get("databaseId") or 0) for item in before}
    if args.no_trigger:
        run_info = latest_matching_run(
            before, ref=args.ref, head_sha=head_sha
        ) or wait_for_run(
            repo,
            args.workflow,
            repository,
            args.ref,
            head_sha,
            args.timeout,
        )
    else:
        # Always create a new workflow-dispatch run. Reusing an earlier run for
        # the same SHA can hide a newly changed workflow or transient failure.
        run(
            gh_repository_command(
                repository, "workflow", "run", args.workflow, "--ref", args.ref
            ),
            repo,
        )
        run_info = wait_for_run(
            repo,
            args.workflow,
            repository,
            args.ref,
            head_sha,
            args.timeout,
            excluded_ids=before_ids,
        )

    run_id = str(run_info["databaseId"])
    started_monitoring = dt.datetime.now(dt.timezone.utc)
    watch = run_result(
        gh_repository_command(repository, "run", "watch", run_id, "--exit-status"),
        repo,
    )
    if watch.stdout:
        print(watch.stdout, end="" if watch.stdout.endswith("\n") else "\n")
    if watch.stderr:
        print(watch.stderr, file=sys.stderr, end="" if watch.stderr.endswith("\n") else "\n")

    download_dir = (
        args.download_dir.resolve()
        if args.download_dir
        else repo / "dist" / "actions" / run_id
    )
    download_dir.mkdir(parents=True, exist_ok=True)
    download = run_result(
        gh_repository_command(
            repository, "run", "download", run_id, "--dir", str(download_dir)
        ),
        repo,
    )
    artifact_files = checksum_downloads(download_dir)

    report_dir = repo / "product" / "working" / "actions"
    report_dir.mkdir(parents=True, exist_ok=True)
    failed_log = report_dir / f"{run_id}-failed.log"
    if watch.returncode != 0:
        output = run(
            gh_repository_command(
                repository, "run", "view", run_id, "--log-failed"
            ),
            repo,
            capture=True,
            check=False,
        )
        failed_log.write_text(output + "\n", encoding="utf-8")

    report_path = report_dir / f"{run_id}.json"
    report = {
        "workflow": args.workflow,
        "ref": args.ref,
        "headSha": head_sha,
        "runId": int(run_id),
        "runUrl": run_info.get("url", ""),
        "event": run_info.get("event", ""),
        "createdAt": run_info.get("createdAt", ""),
        "startedMonitoringAt": started_monitoring.isoformat(),
        "downloadDir": portable_path(repo, download_dir),
        "workflowExitCode": watch.returncode,
        "artifactDownloadExitCode": download.returncode,
        "artifactFiles": artifact_files,
        "failedLog": portable_path(repo, failed_log) if failed_log.is_file() else "",
        "userActionRequired": False,
    }
    report_path.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )

    committed = False
    if not args.no_commit_report:
        committed = commit_report_if_safe(repo, report_path, run_id)

    print(f"ACTIONS_RUN_ID={run_id}")
    print(f"ACTIONS_RUN_URL={run_info.get('url', '')}")
    print(f"ACTIONS_ARTIFACTS={download_dir}")
    print(f"ACTIONS_ARTIFACT_FILE_COUNT={len(artifact_files)}")
    print(f"ACTIONS_REPORT_COMMITTED={str(committed).lower()}")
    print("USER_ACTION_REQUIRED=none")

    if watch.returncode != 0:
        return watch.returncode
    if download.returncode != 0 or not artifact_files:
        return 3
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except CommandError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(2)
