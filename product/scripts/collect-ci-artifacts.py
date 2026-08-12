#!/usr/bin/env python3
"""Collect platform packages, app bundles, checksums, and a manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
from datetime import datetime, timezone
from pathlib import Path

ALLOWED_SUFFIXES = (
    ".exe", ".msi", ".msix", ".dmg", ".zip", ".7z", ".tgz", ".tar",
    ".tar.gz", ".tar.xz", ".tar.bz2",
)


def digest(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(block)
    return hasher.hexdigest()


def is_candidate(path: Path) -> bool:
    name = path.name.lower()
    return ("deskflow" in name or "relaydesk" in name) and any(
        name.endswith(suffix) for suffix in ALLOWED_SUFFIXES
    )


def unique_target(out: Path, source: Path) -> Path:
    target = out / source.name
    if not target.exists() or digest(target) == digest(source):
        return target
    return out / f"{source.parent.name}-{source.name}"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--platform", required=True)
    parser.add_argument("--commit", required=True)
    args = parser.parse_args()

    build = args.build_dir.resolve()
    out = args.out_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)

    copied: list[Path] = []
    for item in sorted(build.rglob("*")):
        if item.is_file() and is_candidate(item):
            target = unique_target(out, item)
            if not target.exists():
                shutil.copy2(item, target)
            copied.append(target)

    # Always preserve a directly installable app bundle in addition to a DMG.
    if args.platform.startswith("macos"):
        apps = sorted(path for path in build.rglob("*.app") if path.is_dir())
        if apps:
            archive_base = out / f"RelayDesk-{args.platform}-unsigned-{args.commit[:8]}.app"
            archive = Path(
                shutil.make_archive(
                    str(archive_base), "zip", root_dir=apps[0].parent, base_dir=apps[0].name
                )
            )
            copied.append(archive)

    copied = sorted(set(path for path in copied if path.is_file()))
    if not copied:
        raise SystemExit(f"no package artifacts found under {build}")

    manifest_files = []
    checksum_lines = []
    for item in copied:
        sha = digest(item)
        checksum_lines.append(f"{sha}  {item.name}")
        manifest_files.append(
            {"name": item.name, "size": item.stat().st_size, "sha256": sha}
        )

    (out / "SHA256SUMS.txt").write_text(
        "\n".join(checksum_lines) + "\n", encoding="ascii"
    )
    manifest = {
        "platform": args.platform,
        "commit": args.commit,
        "signed": False,
        "generatedAt": datetime.now(timezone.utc).isoformat(),
        "runnerOs": os.environ.get("RUNNER_OS", ""),
        "runnerArch": os.environ.get("RUNNER_ARCH", ""),
        "workflowRunId": os.environ.get("GITHUB_RUN_ID", ""),
        "files": manifest_files,
    }
    (out / "artifact-manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(f"Collected {len(copied)} artifacts into {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
