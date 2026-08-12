#!/usr/bin/env python3
"""Validate that RelayDesk product identity is generated from one CMake file."""

from __future__ import annotations

import re
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
    ROOT / "deploy/CMakeLists.txt": ("${RELAYDESK_PACKAGE_ID}",),
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

    if errors:
        print("branding validation failed:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1

    print(f"branding validation passed: {len(REQUIRED_VALUES)} values, {len(CONSUMERS)} consumers")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
