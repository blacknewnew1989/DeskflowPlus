#!/usr/bin/env python3
"""Validate that RelayDesk product identity is generated from one CMake file."""

from __future__ import annotations

import re
import struct
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BRAND_FILE = ROOT / "product/branding/RelayDeskBrand.cmake"

REQUIRED_VALUES = {
    "RELAYDESK_PRODUCT_NAME": "RelayDesk",
    "RELAYDESK_BUNDLE_IDENTIFIER": "local.relaydesk.desktop",
    "RELAYDESK_WINDOWS_APP_USER_MODEL_ID": "RelayDesk.Internal.Desktop",
    "RELAYDESK_WINDOWS_WIX_UPGRADE_GUID": "50C1FCAB-2BF8-447C-806D-A53C21C6A237",
    "RELAYDESK_PACKAGE_ID": "relaydesk",
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
    ROOT / "src/apps/res/relaydesk-brand.qrc.in": (
        "@RELAYDESK_BRAND_MARK_SOURCE@",
        "@RELAYDESK_MACOS_MENU_BAR_ICON_NAME@",
        "@RELAYDESK_MACOS_MENU_BAR_TEMPLATE_SOURCE@",
    ),
    ROOT / "src/apps/res/deskflow.plist.in": ("@BUNDLE_LOCAL_NETWORK_USAGE_DESCRIPTION@",),
    ROOT / "deploy/CMakeLists.txt": ("${RELAYDESK_PACKAGE_ID}",),
    ROOT / "deploy/mac/deploy.cmake": (
        "${RELAYDESK_MACOS_ICON_SOURCE}",
        "${RELAYDESK_MACOS_DMG_BACKGROUND_SOURCE}",
    ),
    ROOT / "deploy/windows/deploy.cmake": ("${RELAYDESK_WINDOWS_WIX_UPGRADE_GUID}",),
    ROOT / "src/apps/deskflow-gui/deskflow-gui.cpp": ("kWindowsAppUserModelId",),
}


def main() -> int:
    brand_text = BRAND_FILE.read_text(encoding="utf-8")
    errors: list[str] = []
    for key, expected in REQUIRED_VALUES.items():
        match = re.search(rf"^set\({re.escape(key)}\s+\"?([^\"\)]+)\"?\)$", brand_text, re.MULTILINE)
        actual = match.group(1) if match else None
        if actual != expected:
            errors.append(f"{key}: expected {expected!r}, found {actual!r}")

    for path, needles in CONSUMERS.items():
        text = path.read_text(encoding="utf-8")
        for needle in needles:
            if needle not in text:
                errors.append(f"{path.relative_to(ROOT)} does not consume {needle}")

    windows_icon = ROOT / REQUIRED_VALUES["RELAYDESK_WINDOWS_ICON_SOURCE"]
    if not windows_icon.is_file():
        errors.append("RelayDesk Windows icon is missing")
    else:
        data = windows_icon.read_bytes()
        if len(data) < 6 or data[:4] != b"\x00\x00\x01\x00":
            errors.append("RelayDesk Windows icon is not a valid ICO container")
        elif struct.unpack_from("<H", data, 4)[0] < 7:
            errors.append("RelayDesk Windows icon must contain small through high-DPI sizes")
        legacy_icon = ROOT / "src/apps/res/deskflow.ico"
        if legacy_icon.is_file() and data == legacy_icon.read_bytes():
            errors.append("RelayDesk Windows icon must not reuse the Deskflow icon")

    for path in (
        ROOT / "src/apps/deskflow-gui/CMakeLists.txt",
        ROOT / "src/apps/deskflow-core/CMakeLists.txt",
        ROOT / "src/apps/deskflow-daemon/CMakeLists.txt",
    ):
        if "src/apps/res/deskflow.ico" in path.read_text(encoding="utf-8"):
            errors.append(f"{path.relative_to(ROOT)} still embeds the Deskflow icon")

    if errors:
        print("branding validation failed:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1

    print(f"branding validation passed: {len(REQUIRED_VALUES)} values, {len(CONSUMERS)} consumers")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
