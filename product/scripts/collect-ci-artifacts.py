#!/usr/bin/env python3
"""Collect platform packages, app bundles, checksums, and a manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
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


def package_candidates(build: Path) -> list[Path]:
    """Return final CPack outputs without walking temporary staging trees."""
    return sorted(
        item for item in build.iterdir() if item.is_file() and is_candidate(item)
    )


def app_candidates(build: Path) -> list[Path]:
    ignored_parts = {"_CPack_Packages", "vcpkg_installed", "CMakeFiles"}
    return sorted(
        path
        for path in build.rglob("*.app")
        if path.is_dir() and not ignored_parts.intersection(path.parts)
    )


def app_archive_name(platform: str, variant: str, commit: str) -> str:
    return f"RelayDesk-{platform}-{variant}-{commit[:8]}.app"


def archive_app_bundle(app: Path, archive_base: Path) -> Path:
    """Archive a macOS app without flattening framework symlinks.

    Python's zipfile-based shutil.make_archive follows symlinks. That turns
    paths such as QtCore.framework/QtCore into regular files and makes the
    extracted framework bundle fail codesign verification. ditto is the macOS
    bundle-preserving archive tool; retain the portable fallback for unit tests
    and non-macOS artifact inspection.
    """
    if sys.platform == "darwin":
        archive = Path(f"{archive_base}.zip")
        archive.unlink(missing_ok=True)
        subprocess.run(
            [
                "/usr/bin/ditto",
                "-c",
                "-k",
                "--sequesterRsrc",
                "--keepParent",
                str(app),
                str(archive),
            ],
            check=True,
        )
        if not archive.is_file():
            raise SystemExit(f"ditto did not create app archive: {archive}")
        return archive
    return Path(
        shutil.make_archive(
            str(archive_base), "zip", root_dir=app.parent, base_dir=app.name
        )
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--platform", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument(
        "--package-variant", choices=("unsigned", "adhoc", "signed"), default="unsigned"
    )
    parser.add_argument("--app-bundle", type=Path)
    parser.add_argument("--signed", action="store_true")
    parser.add_argument("--notarized", action="store_true")
    args = parser.parse_args()

    if args.platform.startswith("macos"):
        if (args.package_variant == "signed") != args.signed:
            raise SystemExit("signed macOS package variant and --signed must be specified together")
        if args.notarized and not args.signed:
            raise SystemExit("a notarized macOS package must also be signed")

    build = args.build_dir.resolve()
    out = args.out_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)

    copied: list[Path] = []
    for item in package_candidates(build):
        target = unique_target(out, item)
        if not target.exists():
            shutil.copy2(item, target)
        copied.append(target)

    # Always preserve a directly installable app bundle in addition to a DMG.
    if args.platform.startswith("macos"):
        if args.app_bundle:
            explicit_app = args.app_bundle.resolve()
            if not explicit_app.is_dir() or explicit_app.suffix.lower() != ".app":
                raise SystemExit(f"invalid explicit app bundle: {explicit_app}")
            apps = [explicit_app]
        else:
            apps = app_candidates(build)
        if apps:
            archive_base = out / app_archive_name(
                args.platform, args.package_variant, args.commit
            )
            archive = archive_app_bundle(apps[0], archive_base)
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
        "signed": args.signed,
        "notarized": args.notarized,
        "packageVariant": args.package_variant,
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
