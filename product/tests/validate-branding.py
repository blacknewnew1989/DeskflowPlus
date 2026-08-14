#!/usr/bin/env python3
"""Validate that RelayDesk product identity is generated from one CMake file."""

from __future__ import annotations

import hashlib
import json
import re
import struct
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BRAND_FILE = ROOT / "product/branding/RelayDeskBrand.cmake"

REQUIRED_VALUES = {
    "RELAYDESK_PRODUCT_NAME": "RelayDesk",
    "RELAYDESK_BUNDLE_IDENTIFIER": "local.relaydesk.desktop",
    "RELAYDESK_WINDOWS_APP_USER_MODEL_ID": "RelayDesk.Internal.Desktop",
    "RELAYDESK_WINDOWS_WIX_UPGRADE_GUID": "50C1FCAB-2BF8-447C-806D-A53C21C6A237",
    "RELAYDESK_PACKAGE_ID": "relaydesk",
    "RELAYDESK_ICON_SOURCE": "product/assets/branding/relaydesk-mark.svg",
    "RELAYDESK_BRAND_MARK_SOURCE": "product/assets/branding/relaydesk-mark.svg",
    "RELAYDESK_WINDOWS_ICON_SOURCE": "src/apps/res/RelayDesk.ico",
    "RELAYDESK_MACOS_ICON_FILE": "RelayDesk.icns",
    "RELAYDESK_MACOS_ICON_SOURCE": "src/apps/res/RelayDesk.icns",
    "RELAYDESK_MACOS_MENU_BAR_ICON_NAME": "${RELAYDESK_BUNDLE_IDENTIFIER}-symbolic",
    "RELAYDESK_MACOS_MENU_BAR_TEMPLATE_SOURCE": "product/assets/branding/generated/relaydesk-menu-bar-template.svg",
    "RELAYDESK_MACOS_DMG_BACKGROUND_SOURCE": "deploy/mac/dmg-background.tiff",
    "RELAYDESK_FILE_PROTOCOL": "RDFT",
    "RELAYDESK_FILE_PROTOCOL_MAJOR": "1",
    "RELAYDESK_FILE_FALLBACK_PORT": "24801",
    "RELAYDESK_DEFAULT_RECEIVE_FOLDER": "RelayDesk",
    "RELAYDESK_UPDATE_CHECK_ENABLED": "OFF",
}

CONSUMERS = {
    ROOT / "CMakeLists.txt": (
        "include(product/branding/RelayDeskBrand.cmake)",
        "${RELAYDESK_PRODUCT_NAME}",
        "${RELAYDESK_BUNDLE_IDENTIFIER}",
    ),
    ROOT / "src/lib/common/Constants.h.in": (
        "@RELAYDESK_WINDOWS_APP_USER_MODEL_ID@",
        "@RELAYDESK_DEFAULT_RECEIVE_FOLDER@",
        "@RELAYDESK_FILE_FALLBACK_PORT@",
        "#cmakedefine01 RELAYDESK_UPDATE_CHECK_ENABLED",
    ),
    ROOT / "deploy/mac/generate_ds_store.applescript": ("@CMAKE_PROJECT_PROPER_NAME@.app",),
    ROOT / "src/apps/deskflow-gui/CMakeLists.txt": (
        "${RELAYDESK_WINDOWS_ICON_SOURCE}",
        "${RELAYDESK_MACOS_ICON_FILE}",
        "${RELAYDESK_MACOS_ICON_SOURCE}",
        "${RELAYDESK_MACOS_LOCAL_NETWORK_USAGE_DESCRIPTION}",
        "relaydesk-brand.qrc.in",
    ),
    ROOT / "src/apps/deskflow-core/CMakeLists.txt": ("${RELAYDESK_WINDOWS_ICON_SOURCE}",),
    ROOT / "src/apps/deskflow-daemon/CMakeLists.txt": ("${RELAYDESK_WINDOWS_ICON_SOURCE}",),
    ROOT / "src/apps/res/deskflow.qrc": (
        "icons/deskflow-dark/apps/64/local.relaydesk.desktop.svg",
        "icons/deskflow-dark/apps/64/local.relaydesk.desktop-symbolic.svg",
        "icons/deskflow-light/apps/64/local.relaydesk.desktop.svg",
        "icons/deskflow-light/apps/64/local.relaydesk.desktop-symbolic.svg",
    ),
    ROOT / "src/apps/res/relaydesk-brand.qrc.in": (
        "@RELAYDESK_BRAND_MARK_SOURCE@",
        "@RELAYDESK_MACOS_MENU_BAR_ICON_NAME@",
        "@RELAYDESK_MACOS_MENU_BAR_TEMPLATE_SOURCE@",
    ),
    ROOT / "src/lib/gui/dialogs/AboutDialog.cpp": (
        'setWindowTitle(tr("About %1").arg(kAppName))',
        "ui->lblName->setText(kAppName)",
    ),
    ROOT / "src/apps/res/deskflow.plist.in": ("@BUNDLE_LOCAL_NETWORK_USAGE_DESCRIPTION@",),
    ROOT / "deploy/CMakeLists.txt": ("${RELAYDESK_PACKAGE_ID}",),
    ROOT / "deploy/mac/deploy.cmake": (
        "${RELAYDESK_MACOS_ICON_SOURCE}",
        "${RELAYDESK_MACOS_DMG_BACKGROUND_SOURCE}",
    ),
    ROOT / "deploy/windows/deploy.cmake": ("${RELAYDESK_WINDOWS_WIX_UPGRADE_GUID}",),
    ROOT / "src/apps/deskflow-gui/deskflow-gui.cpp": ("kWindowsAppUserModelId",),
    ROOT / "product/config/branding.example.json": ("assets/branding/relaydesk-mark.svg",),
}

LIGHT_ICON = ROOT / "src/apps/res/icons/deskflow-light/apps/64/local.relaydesk.desktop.svg"
DARK_ICON = ROOT / "src/apps/res/icons/deskflow-dark/apps/64/local.relaydesk.desktop.svg"
THEMED_ICONS = (LIGHT_ICON, DARK_ICON)

SYMBOLIC_ICONS = (
    ROOT / "src/apps/res/icons/deskflow-light/apps/64/local.relaydesk.desktop-symbolic.svg",
    ROOT / "src/apps/res/icons/deskflow-dark/apps/64/local.relaydesk.desktop-symbolic.svg",
)


def svg_geometry(path: Path, errors: list[str]) -> dict[str, tuple[str, ...]]:
    try:
        root = ET.parse(path).getroot()
    except (ET.ParseError, OSError) as exc:
        errors.append(f"{path.relative_to(ROOT)} is not readable SVG: {exc}")
        return {}

    if root.get("viewBox") != "0 0 64 64":
        errors.append(f"{path.relative_to(ROOT)} must use the 64x64 pixel-aligned viewBox")

    geometry: dict[str, tuple[str, ...]] = {}
    for element in root.iter():
        element_id = element.get("id")
        tag = element.tag.rsplit("}", 1)[-1]
        if element_id in {"device-left", "device-right"} and tag == "path":
            geometry[element_id] = (tag, element.get("d", ""))
        elif element_id == "relay-point" and tag == "circle":
            geometry[element_id] = (
                tag,
                element.get("cx", ""),
                element.get("cy", ""),
                element.get("r", ""),
            )

    required_ids = {"device-left", "device-right", "relay-point"}
    if set(geometry) != required_ids:
        missing = ", ".join(sorted(required_ids - set(geometry)))
        errors.append(f"{path.relative_to(ROOT)} is missing mark geometry: {missing}")
    return geometry


def validate_icon_assets(errors: list[str]) -> None:
    canonical = ROOT / REQUIRED_VALUES["RELAYDESK_ICON_SOURCE"]
    canonical_text = canonical.read_text(encoding="utf-8")
    canonical_geometry = svg_geometry(canonical, errors)

    if "#18262D" not in canonical_text or "#1EA99A" not in canonical_text:
        errors.append("canonical RelayDesk mark must use deep ink #18262D and teal #1EA99A")
    if "gradient" in canonical_text.lower() or "url(" in canonical_text.lower():
        errors.append("canonical RelayDesk mark must not use gradients or referenced paint servers")
    if 'id="relay-link"' in canonical_text or "<line" in canonical_text or "stroke=" in canonical_text:
        errors.append("canonical RelayDesk mark must use solid device blocks and no horizontal connection line")
    if '<path id="device-left" fill="#18262D"' not in canonical_text:
        errors.append("left device block must use deep ink #18262D")
    if '<path id="device-right" fill="#1EA99A"' not in canonical_text:
        errors.append("right device block must use teal #1EA99A")
    if '<circle id="relay-point" cx="32" cy="32" r="3.5" fill="#18262D"' not in canonical_text:
        errors.append("central relay point must use deep ink #18262D")

    expected_small_geometry = {
        "device-left": (
            "path",
            "M14 8C8.477 8 4 12.477 4 18v28c0 5.523 4.477 10 10 10s10-4.477 10-10v-6a8 8 0 0 1 0-16v-6c0-5.523-4.477-10-10-10Z",
        ),
        "device-right": (
            "path",
            "M50 8c-5.523 0-10 4.477-10 10v6a8 8 0 0 1 0 16v6c0 5.523 4.477 10 10 10s10-4.477 10-10V18c0-5.523-4.477-10-10-10Z",
        ),
        "relay-point": ("circle", "32", "32", "3.5"),
    }
    if canonical_geometry != expected_small_geometry:
        errors.append("canonical RelayDesk mark must retain 16px-safe blocks, circular openings, and relay point spacing")

    for path in THEMED_ICONS:
        text = path.read_text(encoding="utf-8")
        if svg_geometry(path, errors) != canonical_geometry:
            errors.append(f"{path.relative_to(ROOT)} geometry diverges from the canonical mark")
        if "#1EA99A" not in text:
            errors.append(f"{path.relative_to(ROOT)} does not preserve RelayDesk teal #1EA99A")
        if "gradient" in text.lower() or "url(" in text.lower():
            errors.append(f"{path.relative_to(ROOT)} must not use gradients")
        if 'id="relay-link"' in text or "<line" in text or "stroke=" in text:
            errors.append(f"{path.relative_to(ROOT)} must not restore a horizontal connection line")

    light_text = LIGHT_ICON.read_text(encoding="utf-8")
    if '<path id="device-left" fill="#18262D"' not in light_text or (
        '<circle id="relay-point" cx="32" cy="32" r="3.5" fill="#18262D"' not in light_text
    ):
        errors.append("light RelayDesk icon must retain deep ink device and relay point")

    dark_text = DARK_ICON.read_text(encoding="utf-8")
    if '<path id="device-left" fill="#F4F7F7"' not in dark_text or (
        '<circle id="relay-point" cx="32" cy="32" r="3.5" fill="#F4F7F7"' not in dark_text
    ):
        errors.append("dark RelayDesk icon must use Cloud #F4F7F7 against the Ink header")
    if '<path id="device-left" fill="#18262D"' in dark_text:
        errors.append("dark RelayDesk icon must not hide its left device on the Ink header")

    for path in SYMBOLIC_ICONS:
        text = path.read_text(encoding="utf-8")
        if svg_geometry(path, errors) != canonical_geometry:
            errors.append(f"{path.relative_to(ROOT)} geometry diverges from the canonical mark")
        symbolic_fills = (
            '<path id="device-left" fill="currentColor"',
            '<path id="device-right" fill="currentColor"',
            '<circle id="relay-point" cx="32" cy="32" r="3.5" fill="currentColor"',
        )
        for marker in symbolic_fills:
            if marker not in text:
                errors.append(f"{path.relative_to(ROOT)} geometry must follow the platform foreground color")
        if "gradient" in text.lower() or "url(" in text.lower():
            errors.append(f"{path.relative_to(ROOT)} must not use gradients")
        if 'id="relay-link"' in text or "<line" in text or "stroke=" in text:
            errors.append(f"{path.relative_to(ROOT)} must not restore a horizontal connection line")


def validate_packaged_icons(errors: list[str]) -> None:
    windows_path = ROOT / REQUIRED_VALUES["RELAYDESK_WINDOWS_ICON_SOURCE"]
    macos_path = ROOT / REQUIRED_VALUES["RELAYDESK_MACOS_ICON_SOURCE"]
    windows_data = windows_path.read_bytes()
    macos_data = macos_path.read_bytes()

    if len(windows_data) < 4096 or not windows_data.startswith(b"\x00\x00\x01\x00"):
        errors.append("RelayDesk Windows icon is not a valid multi-image ICO resource")
    elif struct.unpack_from("<H", windows_data, 4)[0] < 7:
        errors.append("RelayDesk Windows icon must include small through high-DPI sizes")

    if len(macos_data) < 4096 or not macos_data.startswith(b"icns"):
        errors.append("RelayDesk macOS icon is not a valid ICNS resource")
    elif struct.unpack_from(">I", macos_data, 4)[0] != len(macos_data):
        errors.append("RelayDesk macOS icon has an invalid container length")

    legacy_windows = ROOT / "src/apps/res/deskflow.ico"
    legacy_macos = ROOT / "src/apps/res/Deskflow.icns"
    if legacy_windows.is_file() and windows_data == legacy_windows.read_bytes():
        errors.append("RelayDesk Windows icon must not be a relabeled Deskflow binary")
    if legacy_macos.is_file() and macos_data == legacy_macos.read_bytes():
        errors.append("RelayDesk macOS icon must not be a relabeled Deskflow binary")

    provenance_path = ROOT / "product/assets/branding/relaydesk-generated.json"
    try:
        provenance = json.loads(provenance_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        errors.append(f"RelayDesk generated icon provenance is unreadable: {exc}")
        return

    def digest(path: Path) -> str:
        return hashlib.sha256(path.read_bytes()).hexdigest()

    expected = {
        "source": {
            "path": REQUIRED_VALUES["RELAYDESK_ICON_SOURCE"],
            "sha256": digest(ROOT / REQUIRED_VALUES["RELAYDESK_ICON_SOURCE"]),
        },
        "windows": {
            "path": REQUIRED_VALUES["RELAYDESK_WINDOWS_ICON_SOURCE"],
            "size": windows_path.stat().st_size,
            "sha256": digest(windows_path),
        },
        "macos": {
            "path": REQUIRED_VALUES["RELAYDESK_MACOS_ICON_SOURCE"],
            "size": macos_path.stat().st_size,
            "sha256": digest(macos_path),
        },
    }
    for key, value in expected.items():
        if provenance.get(key) != value:
            errors.append(f"RelayDesk generated icon provenance is stale for {key}")


def main() -> int:
    brand_text = BRAND_FILE.read_text(encoding="utf-8")
    errors: list[str] = []
    actual_values: dict[str, str | None] = {}
    for key, expected in REQUIRED_VALUES.items():
        match = re.search(rf"^set\({re.escape(key)}\s+\"?([^\"\)]+)\"?\)$", brand_text, re.MULTILINE)
        actual = match.group(1) if match else None
        actual_values[key] = actual
        if actual != expected:
            errors.append(f"{key}: expected {expected!r}, found {actual!r}")

    for key in ("RELAYDESK_ICON_SOURCE", "RELAYDESK_WINDOWS_ICON_SOURCE", "RELAYDESK_MACOS_ICON_SOURCE"):
        value = actual_values[key]
        if value and not (ROOT / value).is_file():
            errors.append(f"{key} does not exist: {value}")

    for path, needles in CONSUMERS.items():
        text = path.read_text(encoding="utf-8")
        for needle in needles:
            if needle not in text:
                errors.append(f"{path.relative_to(ROOT)} does not consume {needle}")

    about_ui = (ROOT / "src/lib/gui/dialogs/AboutDialog.ui").read_text(encoding="utf-8")
    for stale in ("<string>About Deskflow</string>", '<string notr="true">Deskflow</string>'):
        if stale in about_ui:
            errors.append(f"AboutDialog.ui still contains stale product text: {stale}")

    direct_icon_literals = {
        ROOT / "src/apps/deskflow-gui/CMakeLists.txt": ("src/apps/res/deskflow.ico", "src/apps/res/Deskflow.icns"),
        ROOT / "src/apps/deskflow-core/CMakeLists.txt": ("src/apps/res/deskflow.ico",),
        ROOT / "src/apps/deskflow-daemon/CMakeLists.txt": ("src/apps/res/deskflow.ico",),
    }
    for path, literals in direct_icon_literals.items():
        text = path.read_text(encoding="utf-8")
        for literal in literals:
            if literal in text:
                errors.append(f"{path.relative_to(ROOT)} bypasses RelayDeskBrand.cmake with {literal}")

    validate_icon_assets(errors)
    validate_packaged_icons(errors)

    if errors:
        print("branding validation failed:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1

    print(
        "branding validation passed: "
        f"{len(REQUIRED_VALUES)} values, {len(CONSUMERS)} consumers, "
        f"{1 + len(THEMED_ICONS) + len(SYMBOLIC_ICONS)} synchronized SVG assets"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
