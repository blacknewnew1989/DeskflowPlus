#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RelayDesk Contributors
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
"""Verify the RelayDesk translation closure in a staged macOS app bundle.

The supported-language set is read from the shared CMake manifest.  This
platform verifier deliberately owns no language list of its own.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shlex
import sys
from pathlib import Path
from typing import Any


LANGUAGE_MANIFEST = Path("translations/RelayDeskLanguages.cmake")
LANGUAGE_VARIABLE = "RELAYDESK_SUPPORTED_LANGUAGES"
LANGUAGE_CODE = re.compile(r"^[a-z]{2}(?:_[A-Z]{2})?$")
QM_MAGIC = bytes.fromhex("3cb86418caef9c95cd211cbf60a1bddd")


class TranslationBundleError(RuntimeError):
    """A deterministic macOS translation resource contract failure."""


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
        tokens = tuple(shlex.split(match.group("body").replace(";", " "), posix=True))
    except ValueError as error:
        raise TranslationBundleError(f"LANGUAGE_MANIFEST_INVALID: {error}") from error

    if not tokens:
        raise TranslationBundleError("LANGUAGE_LIST_EMPTY")
    if len(tokens) != len(set(tokens)):
        raise TranslationBundleError("LANGUAGE_LIST_DUPLICATE")
    invalid = [code for code in tokens if LANGUAGE_CODE.fullmatch(code) is None]
    if invalid:
        raise TranslationBundleError(f"LANGUAGE_CODE_INVALID: {','.join(invalid)}")
    return tokens


def verify_bundle(repo_root: Path, app_bundle: Path) -> dict[str, Any]:
    repo_root = repo_root.resolve(strict=True)
    app_bundle = app_bundle.resolve(strict=True)
    if not app_bundle.is_dir() or app_bundle.suffix != ".app":
        raise TranslationBundleError(f"APP_BUNDLE_INVALID: {app_bundle}")

    languages = supported_languages(repo_root / LANGUAGE_MANIFEST)
    expected = tuple(f"relaydesk_{language}.qm" for language in languages)
    translation_dir = app_bundle / "Contents" / "Resources" / "translations"
    if not translation_dir.is_dir():
        raise TranslationBundleError(f"TRANSLATION_DIRECTORY_MISSING: {translation_dir}")

    actual = tuple(sorted(path.name for path in translation_dir.glob("relaydesk_*.qm")))
    missing = sorted(set(expected) - set(actual))
    unexpected = sorted(set(actual) - set(expected))
    if missing:
        raise TranslationBundleError(f"CATALOG_MISSING: {','.join(missing)}")
    if unexpected:
        raise TranslationBundleError(f"CATALOG_UNEXPECTED: {','.join(unexpected)}")

    catalogs: list[dict[str, Any]] = []
    for language, name in zip(languages, expected, strict=True):
        catalog = translation_dir / name
        if catalog.is_symlink() or not catalog.is_file():
            raise TranslationBundleError(f"CATALOG_NOT_REGULAR_FILE: {name}")
        size = catalog.stat().st_size
        if size <= len(QM_MAGIC) or catalog.read_bytes()[: len(QM_MAGIC)] != QM_MAGIC:
            raise TranslationBundleError(f"CATALOG_QM_INVALID: {name}")
        catalogs.append(
            {
                "language": language,
                "name": name,
                "size": size,
                "sha256": sha256(catalog),
                "qtQmHeader": "PASS",
            }
        )

    return {
        "schemaVersion": 1,
        "status": "PASS",
        "languageSource": LANGUAGE_MANIFEST.as_posix(),
        "languageVariable": LANGUAGE_VARIABLE,
        "supportedLanguages": list(languages),
        "appBundle": app_bundle.name,
        "bundleResourcePath": "Contents/Resources/translations",
        "catalogs": catalogs,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--app-bundle", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()

    try:
        result = verify_bundle(args.repo_root, args.app_bundle)
        report = args.report.resolve()
        report.parent.mkdir(parents=True, exist_ok=True)
        report.write_text(
            json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )
        print(json.dumps(result, ensure_ascii=False))
        return 0
    except (OSError, TranslationBundleError) as error:
        print(f"MACOS_TRANSLATION_BUNDLE_FAILED: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
