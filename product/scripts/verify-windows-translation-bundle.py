#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RelayDesk Contributors
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
"""Verify RelayDesk catalogs in a staged Windows install tree.

The supported-language set comes only from the shared CMake manifest.  This
verifier intentionally owns no Windows-specific language list.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


LANGUAGE_MANIFEST = Path("translations/RelayDeskLanguages.cmake")
LANGUAGE_VARIABLE = "RELAYDESK_SUPPORTED_LANGUAGES"
LANGUAGE_CODE = re.compile(r"^[a-z]{2}(?:_[A-Z]{2})?$")
QM_MAGIC = bytes.fromhex("3cb86418caef9c95cd211cbf60a1bddd")


class TranslationBundleError(RuntimeError):
    """A deterministic Windows translation resource contract failure."""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def supported_languages(manifest: Path) -> tuple[str, ...]:
    if not manifest.is_file():
        raise TranslationBundleError(f"LANGUAGE_MANIFEST_MISSING: {manifest}")

    uncommented = "\n".join(
        line.split("#", 1)[0] for line in manifest.read_text(encoding="utf-8").splitlines()
    )
    match = re.search(
        rf"\bset\s*\(\s*{LANGUAGE_VARIABLE}\b(?P<body>.*?)\)",
        uncommented,
        flags=re.DOTALL,
    )
    if match is None:
        raise TranslationBundleError(
            f"LANGUAGE_VARIABLE_MISSING: {LANGUAGE_VARIABLE} in {manifest}"
        )

    try:
        languages = tuple(shlex.split(match.group("body").replace(";", " "), posix=True))
    except ValueError as error:
        raise TranslationBundleError(f"LANGUAGE_MANIFEST_INVALID: {error}") from error

    if not languages:
        raise TranslationBundleError("LANGUAGE_LIST_EMPTY")
    if len(languages) != len(set(languages)):
        raise TranslationBundleError("LANGUAGE_LIST_DUPLICATE")
    invalid = [code for code in languages if LANGUAGE_CODE.fullmatch(code) is None]
    if invalid:
        raise TranslationBundleError(f"LANGUAGE_CODE_INVALID: {','.join(invalid)}")
    return languages


def verify_bundle(
    repo_root: Path, translation_dir: Path, lconvert: Path | None = None
) -> dict[str, Any]:
    repo_root = repo_root.resolve(strict=True)
    translation_dir = translation_dir.resolve(strict=True)
    if not translation_dir.is_dir():
        raise TranslationBundleError(f"TRANSLATION_DIRECTORY_INVALID: {translation_dir}")

    languages = supported_languages(repo_root / LANGUAGE_MANIFEST)
    expected = tuple(f"relaydesk_{language}.qm" for language in languages)
    actual = tuple(sorted(path.name for path in translation_dir.glob("relaydesk_*.qm")))
    missing = sorted(set(expected) - set(actual))
    unexpected = sorted(set(actual) - set(expected))
    if missing:
        raise TranslationBundleError(f"CATALOG_MISSING: {','.join(missing)}")
    if unexpected:
        raise TranslationBundleError(f"CATALOG_UNEXPECTED: {','.join(unexpected)}")

    lconvert_path: Path | None = None
    if lconvert is not None:
        lconvert_path = lconvert.resolve(strict=True)
        if not lconvert_path.is_file():
            raise TranslationBundleError(f"LCONVERT_INVALID: {lconvert_path}")

    catalogs: list[dict[str, Any]] = []
    with tempfile.TemporaryDirectory(prefix="relaydesk-windows-qm-load-") as temporary:
        temporary_path = Path(temporary)
        for index, language in enumerate(languages):
            name = expected[index]
            catalog = translation_dir / name
            if catalog.is_symlink() or not catalog.is_file():
                raise TranslationBundleError(f"CATALOG_NOT_REGULAR_FILE: {name}")
            size = catalog.stat().st_size
            with catalog.open("rb") as stream:
                magic = stream.read(len(QM_MAGIC))
            if size <= len(QM_MAGIC) or magic != QM_MAGIC:
                raise TranslationBundleError(f"CATALOG_QM_INVALID: {name}")

            qt_load = "NOT_RUN"
            if lconvert_path is not None:
                converted = temporary_path / f"{language}.ts"
                result = subprocess.run(
                    [lconvert_path, "-i", catalog, "-o", converted],
                    capture_output=True,
                    text=True,
                    check=False,
                    timeout=30,
                )
                if result.returncode != 0 or not converted.is_file():
                    diagnostic = (result.stderr or result.stdout).strip()
                    raise TranslationBundleError(
                        f"CATALOG_QT_LOAD_FAILED: {name}: {diagnostic}"
                    )
                qt_load = "PASS"

            catalogs.append(
                {
                    "language": language,
                    "name": name,
                    "size": size,
                    "sha256": sha256(catalog),
                    "qtQmHeader": "PASS",
                    "qtLoad": qt_load,
                }
            )

    return {
        "schemaVersion": 1,
        "status": "PASS",
        "languageSource": LANGUAGE_MANIFEST.as_posix(),
        "languageVariable": LANGUAGE_VARIABLE,
        "supportedLanguages": list(languages),
        "translationPath": translation_dir.name,
        "catalogs": catalogs,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--translation-dir", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--lconvert", type=Path)
    args = parser.parse_args()

    try:
        result = verify_bundle(args.repo_root, args.translation_dir, args.lconvert)
        report = args.report.resolve()
        report.parent.mkdir(parents=True, exist_ok=True)
        report.write_text(
            json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )
        print(json.dumps(result, ensure_ascii=False))
        return 0
    except (OSError, TranslationBundleError) as error:
        print(f"WINDOWS_TRANSLATION_BUNDLE_FAILED: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
