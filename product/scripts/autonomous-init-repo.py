#!/usr/bin/env python3
"""Bootstrap RelayDesk inside the current GitHub-connected repository.

Default behavior is fully autonomous: preserve ``origin``, add Deskflow as
``upstream``, fetch and verify the pinned tag, create/reuse the product worktree,
install the development package and GitHub Actions workflow, commit, and push.
No force-push or history rewriting is used.
"""

from __future__ import annotations

import argparse
import datetime as dt
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Iterable

DEFAULT_TAG = "v1.26.0"
DEFAULT_EXPECTED_SHORT = "760e3b9"
DEFAULT_BRANCH = "product/relaydesk-v1"
DEFAULT_UPSTREAM_URL = "https://github.com/deskflow/deskflow.git"


class CommandError(RuntimeError):
    pass


def run(
    args: Iterable[str],
    *,
    cwd: Path,
    check: bool = True,
    capture: bool = False,
) -> str:
    cmd = [str(x) for x in args]
    print("+", " ".join(cmd))
    result = subprocess.run(
        cmd,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.PIPE if capture else None,
        check=False,
    )
    if check and result.returncode != 0:
        detail = (result.stderr or result.stdout or "command failed").strip()
        raise CommandError(f"{' '.join(cmd)}: {detail}")
    return (result.stdout or "").strip()


def git(cwd: Path, *args: str, check: bool = True, capture: bool = False) -> str:
    return run(["git", *args], cwd=cwd, check=check, capture=capture)


def is_source_tree(path: Path) -> bool:
    return all(
        [
            (path / "CMakeLists.txt").is_file(),
            (path / "src/apps").is_dir(),
            (path / "src/lib").is_dir(),
            (path / "src/lib/platform").is_dir(),
            (path / "deploy").is_dir(),
        ]
    )


def remote_url(repo: Path, name: str) -> str:
    return git(repo, "remote", "get-url", name, capture=True, check=False)


def ref_exists(repo: Path, ref: str) -> bool:
    return (
        subprocess.run(
            ["git", "show-ref", "--verify", "--quiet", ref],
            cwd=repo,
            check=False,
        ).returncode
        == 0
    )


def current_branch(repo: Path) -> str:
    return git(repo, "branch", "--show-current", capture=True)


def ensure_identity(repo: Path) -> None:
    if not git(repo, "config", "user.name", capture=True, check=False):
        git(repo, "config", "user.name", "Codex RelayDesk")
    if not git(repo, "config", "user.email", capture=True, check=False):
        git(repo, "config", "user.email", "codex-relaydesk@users.noreply.github.com")


def branch_worktree(repo: Path, branch: str) -> Path | None:
    output = git(repo, "worktree", "list", "--porcelain", capture=True)
    worktree: Path | None = None
    wanted = f"refs/heads/{branch}"
    for line in output.splitlines():
        if line.startswith("worktree "):
            worktree = Path(line.removeprefix("worktree ")).resolve()
        elif line == f"branch {wanted}" and worktree is not None:
            return worktree
    return None


def choose_worktree(repo: Path, requested: Path | None) -> Path:
    if requested is not None:
        return requested.expanduser().resolve()
    base = repo.parent / f"{repo.name}-relaydesk"
    if not base.exists():
        return base
    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    return repo.parent / f"{repo.name}-relaydesk-{stamp}"


def prepare_product_worktree(
    repo: Path,
    *,
    branch: str,
    tag: str,
    requested: Path | None,
) -> Path:
    # Reuse an already active worktree first.
    active = branch_worktree(repo, branch)
    if active is not None:
        return active

    # If the current directory already is the product source worktree, keep it.
    if is_source_tree(repo) and current_branch(repo) == branch:
        return repo

    target = choose_worktree(repo, requested)
    if target.exists():
        raise CommandError(f"worktree destination already exists: {target}")

    if ref_exists(repo, f"refs/heads/{branch}"):
        git(repo, "worktree", "add", str(target), branch)
    elif ref_exists(repo, f"refs/remotes/origin/{branch}"):
        git(repo, "worktree", "add", "-b", branch, str(target), f"origin/{branch}")
    else:
        git(repo, "worktree", "add", "-b", branch, str(target), tag)
    return target


def install_product_materials(package_root: Path, source_repo: Path) -> None:
    installer = package_root / "scripts/install-package.py"
    workflow_installer = package_root / "scripts/install-github-workflows.py"
    for required in (installer, workflow_installer):
        if not required.is_file():
            raise CommandError(f"required installer missing: {required}")

    run(
        [
            sys.executable,
            str(installer),
            "--package-root",
            str(package_root),
            "--repo",
            str(source_repo),
            "--force-agents",
        ],
        cwd=source_repo,
    )
    run(
        [
            sys.executable,
            str(workflow_installer),
            "--package-root",
            str(package_root),
            "--repo",
            str(source_repo),
        ],
        cwd=source_repo,
    )


def write_report(
    source_repo: Path,
    original_repo: Path,
    *,
    branch: str,
    tag: str,
    expected_short: str,
) -> None:
    """Write a deterministic tracked bootstrap record.

    The report intentionally excludes timestamps, local absolute paths and the
    origin URL. This keeps re-runs idempotent, prevents machine-specific churn,
    and avoids persisting credentials that might be embedded in a remote URL.
    """
    del original_repo  # local path is intentionally not written to the repository
    verified_full = git(source_repo, "rev-parse", f"{tag}^{{commit}}", capture=True)
    report = source_repo / "product/working/bootstrap-report.md"
    report.parent.mkdir(parents=True, exist_ok=True)
    # Keep re-runs idempotent. Package/workflow updates can still create a new
    # commit, but the original bootstrap evidence must not change on every session.
    if report.exists():
        return
    report.write_text(
        "\n".join(
            [
                "# RelayDesk Bootstrap Report",
                "",
                "- Bootstrap mode: autonomous current-repository import",
                f"- Product branch: `{branch}`",
                f"- Upstream: `{DEFAULT_UPSTREAM_URL}`",
                f"- Pinned tag: `{tag}`",
                f"- Verified baseline short commit: `{expected_short}`",
                f"- Verified baseline full commit: `{verified_full}`",
                "- Development materials: root `AGENTS.md` and `product/`",
                "- GitHub workflow: `.github/workflows/relaydesk-build.yml`",
                "- User action required: none until final acceptance",
                "",
            ]
        ),
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    parser.add_argument(
        "--package-root", type=Path, default=Path(__file__).resolve().parent.parent
    )
    parser.add_argument("--worktree", type=Path)
    parser.add_argument("--upstream-url", default=DEFAULT_UPSTREAM_URL)
    parser.add_argument("--tag", default=DEFAULT_TAG)
    parser.add_argument("--expected-short", default=DEFAULT_EXPECTED_SHORT)
    parser.add_argument("--branch", default=DEFAULT_BRANCH)
    parser.add_argument("--no-push", action="store_true", help="test-only; default is push")
    args = parser.parse_args()

    if shutil.which("git") is None:
        raise CommandError("git is required")

    package_root = args.package_root.expanduser().resolve()
    if not (package_root / "AGENTS.md").is_file():
        raise CommandError(f"invalid package root: {package_root}")

    candidate = args.repo.expanduser().resolve()
    root_text = git(candidate, "rev-parse", "--show-toplevel", capture=True)
    repo = Path(root_text).resolve()

    origin = remote_url(repo, "origin")
    if not origin:
        raise CommandError("current repository has no origin remote")
    git(repo, "ls-remote", "origin")

    upstream = remote_url(repo, "upstream")
    if upstream:
        git(repo, "remote", "set-url", "upstream", args.upstream_url)
    else:
        git(repo, "remote", "add", "upstream", args.upstream_url)

    git(repo, "fetch", "origin", "--prune", check=False)
    git(repo, "fetch", "upstream", "--tags", "--prune")
    actual = git(
        repo, "rev-parse", "--short=7", f"{args.tag}^{{commit}}", capture=True
    )
    if actual != args.expected_short:
        raise CommandError(
            f"{args.tag} resolved to {actual}, expected {args.expected_short}"
        )

    ensure_identity(repo)
    source_repo = prepare_product_worktree(
        repo, branch=args.branch, tag=args.tag, requested=args.worktree
    )
    if not is_source_tree(source_repo):
        raise CommandError(f"product worktree is not a Deskflow source tree: {source_repo}")

    # When a local product branch already existed, fast-forward it from origin
    # before generating new materials. Never hide local work or create a merge.
    if ref_exists(repo, f"refs/remotes/origin/{args.branch}"):
        dirty = git(source_repo, "status", "--porcelain", capture=True)
        if dirty:
            print("PRODUCT_BRANCH_SYNC_DEFERRED=dirty-worktree")
        else:
            git(source_repo, "merge", "--ff-only", f"origin/{args.branch}")

    install_product_materials(package_root, source_repo)
    write_report(
        source_repo,
        repo,
        branch=args.branch,
        tag=args.tag,
        expected_short=args.expected_short,
    )

    git(source_repo, "add", "AGENTS.md", "product", ".github/workflows")
    staged = (
        subprocess.run(
            ["git", "diff", "--cached", "--quiet"], cwd=source_repo, check=False
        ).returncode
        != 0
    )
    if staged:
        git(
            source_repo,
            "commit",
            "-m",
            f"chore(bootstrap): initialize RelayDesk from Deskflow {args.tag}",
        )

    if not args.no_push:
        git(source_repo, "push", "-u", "origin", args.branch)

    print("\nRelayDesk autonomous bootstrap complete")
    print(f"SOURCE_WORKTREE={source_repo}")
    print(f"BRANCH={args.branch}")
    print(f"PUSHED={str(not args.no_push).lower()}")
    print("USER_ACTION_REQUIRED=none")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except CommandError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(2)
