#!/usr/bin/env python3
"""Generate and validate the Windows RelayDesk icon from the canonical brand artwork."""

# SPDX-FileCopyrightText: 2026 RelayDesk Contributors
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

from __future__ import annotations

import argparse
import io
import struct
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MARK = ROOT / "product/assets/branding/relaydesk-mark.svg"
APP_ICON = ROOT / "src/apps/res/RelayDesk.icns"
WINDOWS_ICON = ROOT / "src/apps/res/RelayDesk.ico"
ICON_SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)


class AssetError(RuntimeError):
    pass


def validate_source() -> None:
    source = MARK.read_text(encoding="utf-8")
    ET.fromstring(source)
    for required in ("#18262D", "#1EA99A", 'id="device-left"', 'id="device-right"', 'id="relay-point"'):
        if required not in source:
            raise AssetError(f"canonical mark is missing {required}")
    if not APP_ICON.is_file() or APP_ICON.read_bytes()[:4] != b"icns":
        raise AssetError("RelayDesk.icns is missing; generate the canonical macOS asset first")


def generated_bytes() -> bytes:
    try:
        from PIL import Image
    except ImportError as error:
        raise AssetError("generation requires Pillow (python -m pip install Pillow)") from error

    with Image.open(APP_ICON) as source:
        rgba = source.convert("RGBA")
        output = io.BytesIO()
        rgba.save(
            output,
            format="ICO",
            sizes=[(size, size) for size in ICON_SIZES],
            bitmap_format="png",
        )
        return output.getvalue()


def icon_sizes(data: bytes) -> tuple[int, ...]:
    if len(data) < 6 or data[:4] != b"\x00\x00\x01\x00":
        raise AssetError("RelayDesk.ico is not a Windows icon container")
    count = struct.unpack_from("<H", data, 4)[0]
    if len(data) < 6 + count * 16:
        raise AssetError("RelayDesk.ico has a truncated directory")
    sizes: list[int] = []
    for index in range(count):
        width, height = struct.unpack_from("BB", data, 6 + index * 16)
        width = width or 256
        height = height or 256
        if width != height:
            raise AssetError(f"RelayDesk.ico contains a non-square frame: {width}x{height}")
        sizes.append(width)
    return tuple(sorted(sizes))


def check(expected: bytes | None = None) -> None:
    if not WINDOWS_ICON.is_file():
        raise AssetError("RelayDesk.ico is missing")
    actual = WINDOWS_ICON.read_bytes()
    sizes = icon_sizes(actual)
    if sizes != ICON_SIZES:
        raise AssetError(f"RelayDesk.ico sizes differ: expected {ICON_SIZES}, found {sizes}")
    if expected is not None and actual != expected:
        raise AssetError("RelayDesk.ico is stale relative to the canonical application artwork")
    legacy = ROOT / "src/apps/res/deskflow.ico"
    if legacy.is_file() and actual == legacy.read_bytes():
        raise AssetError("RelayDesk.ico must not reuse the Deskflow icon")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="validate the committed ICO without rewriting it")
    args = parser.parse_args()
    try:
        validate_source()
        expected = generated_bytes()
        if not args.check:
            WINDOWS_ICON.write_bytes(expected)
        check(expected)
    except (AssetError, OSError, ET.ParseError) as error:
        print(f"Windows brand asset validation failed: {error}", file=sys.stderr)
        return 1
    print(f"Windows brand asset valid: {len(ICON_SIZES)} ICO sizes generated from RelayDesk artwork")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
