#!/usr/bin/env python3
"""Generate and validate macOS RelayDesk artwork from the canonical SVG mark."""

# SPDX-FileCopyrightText: 2026 RelayDesk Contributors
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

from __future__ import annotations

import argparse
import re
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MARK = ROOT / "product/assets/branding/relaydesk-mark.svg"
GENERATED_DIR = ROOT / "product/assets/branding/generated"
TEMPLATE = GENERATED_DIR / "relaydesk-menu-bar-template.svg"
APP_ICON = ROOT / "src/apps/res/RelayDesk.icns"
DMG_BACKGROUND = ROOT / "deploy/mac/dmg-background.tiff"

APP_COLOR_DARK = "#18262D"
APP_COLOR_TEAL = "#1EA99A"
TEMPLATE_COLOR = "#000000"
ICONSET_SLOTS = {
    "icon_16x16.png": 16,
    "icon_16x16@2x.png": 32,
    "icon_32x32.png": 32,
    "icon_32x32@2x.png": 64,
    "icon_128x128.png": 128,
    "icon_128x128@2x.png": 256,
    "icon_256x256.png": 256,
    "icon_256x256@2x.png": 512,
    "icon_512x512.png": 512,
    "icon_512x512@2x.png": 1024,
}


class AssetError(RuntimeError):
    pass


def canonical_mark() -> str:
    source = MARK.read_text(encoding="utf-8")
    ET.fromstring(source)
    for required in (APP_COLOR_DARK, APP_COLOR_TEAL, 'id="device-left"', 'id="device-right"', 'id="relay-point"'):
        if required not in source:
            raise AssetError(f"canonical mark is missing {required}")
    return source


def template_svg(source: str) -> str:
    generated = source.replace(APP_COLOR_DARK, TEMPLATE_COLOR).replace(APP_COLOR_TEAL, TEMPLATE_COLOR)
    generated = generated.replace(
        "<!-- SPDX-FileCopyrightText: (C) 2026 RelayDesk Contributors -->",
        "<!-- Generated from product/assets/branding/relaydesk-mark.svg; do not hand edit. -->\n"
        "<!-- SPDX-FileCopyrightText: (C) 2026 RelayDesk Contributors -->",
    )
    return generated


def mark_contents(source: str) -> str:
    body = re.sub(r"^.*?<svg\b[^>]*>", "", source, count=1, flags=re.DOTALL)
    body = re.sub(r"</svg>\s*$", "", body, count=1, flags=re.DOTALL)
    body = re.sub(r"\s*<(?:title|desc)\b.*?</(?:title|desc)>\s*", "\n", body, flags=re.DOTALL)
    return body.strip()


def dmg_background_svg(source: str) -> str:
    geometry = mark_contents(source)
    return f'''<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="600" height="400" viewBox="0 0 600 400">
  <rect width="300" height="400" fill="#18262D"/>
  <rect x="300" width="300" height="400" fill="#F8FAFC"/>
  <rect x="67" y="116" width="160" height="160" rx="12" fill="#FFFFFF" fill-opacity="0.10" stroke="#FFFFFF" stroke-opacity="0.32"/>
  <rect x="373" y="116" width="160" height="160" rx="12" fill="#FFFFFF" stroke="#CBD5E1"/>
  <rect x="18" y="16" width="52" height="54" rx="11" fill="#FFFFFF"/>
  <svg x="24" y="22" width="40" height="42" viewBox="0 0 64 64">{geometry}</svg>
  <text x="82" y="52" fill="#FFFFFF" font-family="-apple-system, BlinkMacSystemFont, sans-serif" font-size="25" font-weight="600">RelayDesk</text>
  <path d="M262 196h69m-14-14 14 14-14 14" fill="none" stroke="#1EA99A" stroke-width="5" stroke-linecap="round" stroke-linejoin="round"/>
  <text x="147" y="326" fill="#E2E8F0" font-family="-apple-system, BlinkMacSystemFont, sans-serif" font-size="15" text-anchor="middle">Drag RelayDesk</text>
  <text x="453" y="326" fill="#334155" font-family="-apple-system, BlinkMacSystemFont, sans-serif" font-size="15" text-anchor="middle">to Applications</text>
</svg>
'''


def run(command: list[str]) -> None:
    result = subprocess.run(command, text=True, capture_output=True)
    if result.returncode:
        detail = (result.stderr or result.stdout).strip()
        raise AssetError(f"command failed ({' '.join(command)}): {detail}")


def render_svg(svg: Path, output: Path, width: int, height: int, fmt: str = "png") -> None:
    run(["/usr/bin/sips", "-s", "format", fmt, "-z", str(height), str(width), str(svg), "--out", str(output)])


def generate(source: str) -> None:
    if sys.platform != "darwin":
        raise AssetError("generation requires macOS sips and iconutil; use --check on other platforms")
    for tool in ("/usr/bin/sips", "/usr/bin/iconutil"):
        if not Path(tool).is_file():
            raise AssetError(f"required macOS tool is missing: {tool}")

    GENERATED_DIR.mkdir(parents=True, exist_ok=True)
    TEMPLATE.write_text(template_svg(source), encoding="utf-8")

    with tempfile.TemporaryDirectory(prefix="relaydesk-brand-") as temp_name:
        temp = Path(temp_name)
        iconset = temp / "RelayDesk.iconset"
        iconset.mkdir()
        for filename, size in ICONSET_SLOTS.items():
            render_svg(MARK, iconset / filename, size, size)
        run(["/usr/bin/iconutil", "-c", "icns", str(iconset), "-o", str(APP_ICON)])

        background_svg = temp / "relaydesk-dmg-background.svg"
        background_svg.write_text(dmg_background_svg(source), encoding="utf-8")
        render_svg(background_svg, DMG_BACKGROUND, 600, 400, "tiff")


def png_dimensions(path: Path) -> tuple[int, int]:
    data = path.read_bytes()
    if len(data) < 24 or data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        raise AssetError(f"not a PNG image: {path}")
    return struct.unpack(">II", data[16:24])


def check(source: str) -> None:
    expected_template = template_svg(source)
    if not TEMPLATE.is_file() or TEMPLATE.read_text(encoding="utf-8") != expected_template:
        raise AssetError("menu bar template is missing or stale; run the generator")
    template_root = ET.fromstring(expected_template)
    fills = {value.upper() for node in template_root.iter() if (value := node.attrib.get("fill"))}
    if APP_COLOR_DARK in fills or APP_COLOR_TEAL in fills:
        raise AssetError("menu bar template still contains a colorful application fill")
    if not {"#000000", "#FFFFFF"}.issuperset(fills):
        raise AssetError(f"menu bar template contains unexpected fills: {sorted(fills)}")

    if not APP_ICON.is_file() or APP_ICON.stat().st_size < 4096 or APP_ICON.read_bytes()[:4] != b"icns":
        raise AssetError("RelayDesk.icns is missing or invalid")
    if not DMG_BACKGROUND.is_file() or DMG_BACKGROUND.stat().st_size < 4096:
        raise AssetError("RelayDesk DMG background is missing or invalid")

    if sys.platform == "darwin":
        with tempfile.TemporaryDirectory(prefix="relaydesk-icon-check-") as temp_name:
            temp = Path(temp_name)
            iconset = temp / "RelayDesk.iconset"
            expected_iconset = temp / "Expected.iconset"
            expected_iconset.mkdir()
            run(["/usr/bin/iconutil", "-c", "iconset", str(APP_ICON), "-o", str(iconset)])
            extracted_slots = {path.name for path in iconset.iterdir()}
            # iconutil omits the legacy 1x 16 px and 32 px filenames when their
            # representations are canonicalized into the equivalent Retina
            # payloads. The remaining eight files are independent ICNS payloads
            # and must always survive a round trip through iconutil.
            canonical_slots = set(ICONSET_SLOTS) - {"icon_16x16.png", "icon_32x32.png"}
            missing = canonical_slots - extracted_slots
            if missing:
                raise AssetError(f"RelayDesk.icns is missing slots: {sorted(missing)}")
            for filename, size in ICONSET_SLOTS.items():
                if filename not in extracted_slots:
                    continue
                if png_dimensions(iconset / filename) != (size, size):
                    raise AssetError(f"RelayDesk.icns slot has wrong dimensions: {filename}")
                if filename not in canonical_slots:
                    continue
                render_svg(MARK, expected_iconset / filename, size, size)
                if (iconset / filename).read_bytes() != (expected_iconset / filename).read_bytes():
                    raise AssetError(f"RelayDesk.icns is stale relative to the SVG source: {filename}")

            background_svg = temp / "relaydesk-dmg-background.svg"
            expected_background = temp / "relaydesk-dmg-background.tiff"
            background_svg.write_text(dmg_background_svg(source), encoding="utf-8")
            render_svg(background_svg, expected_background, 600, 400, "tiff")
            if DMG_BACKGROUND.read_bytes() != expected_background.read_bytes():
                raise AssetError("RelayDesk DMG background is stale relative to the SVG source")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="validate committed derivatives without rewriting them")
    args = parser.parse_args()
    try:
        source = canonical_mark()
        if not args.check:
            generate(source)
        check(source)
    except (AssetError, OSError, ET.ParseError) as error:
        print(f"macOS brand asset validation failed: {error}", file=sys.stderr)
        return 1
    print("macOS brand assets valid: SVG source, template icon, ICNS slots, and DMG artwork")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
