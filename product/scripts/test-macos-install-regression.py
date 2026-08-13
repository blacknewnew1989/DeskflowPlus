#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RelayDesk Contributors
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
"""Exercise an isolated macOS App/DMG install lifecycle on a hosted runner.

The harness never writes to /Applications or the runner account's real HOME.
It requires RUNNER_TEMP, creates a uniquely named sandbox beneath it, and removes
only that validated sandbox after the result has been recorded.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import plistlib
import re
import shutil
import stat
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import IO, Any, Iterable


SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
TEST_ROOT_PREFIX = "relaydesk-test005-macos-"
EXPECTED_BONJOUR_SERVICES = ["_relaydesk._udp"]
EXPECTED_MACOS_ARCHITECTURES = ["arm64"]
EXPECTED_MACOS_MINIMUM_VERSION = "14.0"
PERMISSION_NOT_RUN_REASON = (
    "hosted runners cannot grant or observe the macOS System Settings consent UI"
)


class RegressionError(RuntimeError):
    """A deterministic TEST-005 contract failure."""


@dataclass(frozen=True)
class ArtifactSet:
    app_zip: Path
    dmg: Path
    package_variant: str
    manifest: dict[str, Any]


@dataclass(frozen=True)
class BundleInfo:
    path: Path
    identifier: str
    executable_name: str
    version: str
    executable: Path
    core_executable: Path


@dataclass(frozen=True)
class UserDataSentinel:
    path: Path
    marker: bytes
    original_sha256: str | None


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def is_strict_descendant(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
    except ValueError:
        return False
    return path != parent


def validate_test_root(path: Path, runner_temp: Path) -> Path:
    runner_temp = runner_temp.resolve(strict=True)
    path = path.resolve(strict=True)
    if not is_strict_descendant(path, runner_temp):
        raise RegressionError(f"TEST005_UNSAFE_TEST_ROOT: {path}")
    if not path.name.startswith(TEST_ROOT_PREFIX):
        raise RegressionError(f"TEST005_UNSAFE_TEST_ROOT_NAME: {path.name}")
    return path


def safe_remove_tree(path: Path, allowed_parent: Path) -> None:
    resolved_parent = allowed_parent.resolve(strict=True)
    resolved_path = path.resolve(strict=True)
    if not is_strict_descendant(resolved_path, resolved_parent):
        raise RegressionError(f"TEST005_UNSAFE_REMOVE_TARGET: {resolved_path}")
    shutil.rmtree(resolved_path)


def parse_checksum_file(path: Path) -> dict[str, str]:
    checksums: dict[str, str] = {}
    for number, raw_line in enumerate(path.read_text(encoding="ascii").splitlines(), 1):
        if not raw_line:
            continue
        match = re.fullmatch(r"([0-9a-f]{64})  ([^/\\]+)", raw_line)
        if match is None:
            raise RegressionError(f"TEST005_CHECKSUM_LINE_INVALID: line {number}")
        digest, name = match.groups()
        if name in checksums:
            raise RegressionError(f"TEST005_CHECKSUM_DUPLICATE: {name}")
        checksums[name] = digest
    return checksums


def validate_artifacts(artifact_dir: Path, expected_commit: str) -> ArtifactSet:
    artifact_dir = artifact_dir.resolve(strict=True)
    manifest_path = artifact_dir / "artifact-manifest.json"
    checksums_path = artifact_dir / "SHA256SUMS.txt"
    if not manifest_path.is_file():
        raise RegressionError("TEST005_MANIFEST_MISSING")
    if not checksums_path.is_file():
        raise RegressionError("TEST005_CHECKSUMS_MISSING")

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("platform") != "macos-arm64":
        raise RegressionError(f"TEST005_MANIFEST_PLATFORM: {manifest.get('platform')}")
    if manifest.get("commit") != expected_commit:
        raise RegressionError(
            f"TEST005_MANIFEST_COMMIT: expected {expected_commit}, "
            f"got {manifest.get('commit')}"
        )

    package_variant = manifest.get("packageVariant")
    if package_variant != "adhoc":
        raise RegressionError(f"TEST005_PACKAGE_VARIANT: {package_variant}")
    if manifest.get("signed") is not False or manifest.get("notarized") is not False:
        raise RegressionError("TEST005_ADHOC_MANIFEST_BOUNDARY_INVALID")

    entries = manifest.get("files")
    if not isinstance(entries, list) or not entries:
        raise RegressionError("TEST005_MANIFEST_FILES_INVALID")
    declared_checksums: dict[str, str] = {}
    app_zips: list[Path] = []
    dmgs: list[Path] = []
    for entry in entries:
        if not isinstance(entry, dict):
            raise RegressionError("TEST005_MANIFEST_FILE_ENTRY_INVALID")
        name = entry.get("name")
        digest = entry.get("sha256")
        size = entry.get("size")
        if not isinstance(name, str) or Path(name).name != name:
            raise RegressionError(f"TEST005_MANIFEST_FILE_NAME_INVALID: {name}")
        if name in declared_checksums:
            raise RegressionError(f"TEST005_MANIFEST_FILE_DUPLICATE: {name}")
        if not isinstance(digest, str) or SHA256_RE.fullmatch(digest) is None:
            raise RegressionError(f"TEST005_MANIFEST_SHA_INVALID: {name}")
        item = artifact_dir / name
        if not item.is_file():
            raise RegressionError(f"TEST005_ARTIFACT_MISSING: {name}")
        if not isinstance(size, int) or item.stat().st_size != size:
            raise RegressionError(f"TEST005_ARTIFACT_SIZE_MISMATCH: {name}")
        actual_digest = sha256(item)
        if actual_digest != digest:
            raise RegressionError(f"TEST005_ARTIFACT_SHA_MISMATCH: {name}")
        declared_checksums[name] = digest
        if name.endswith(".app.zip"):
            app_zips.append(item)
        elif name.endswith(".dmg"):
            dmgs.append(item)

    checksum_file = parse_checksum_file(checksums_path)
    if checksum_file != declared_checksums:
        raise RegressionError("TEST005_CHECKSUM_MANIFEST_MISMATCH")
    if len(app_zips) != 1:
        raise RegressionError(f"TEST005_APP_ZIP_COUNT: {len(app_zips)}")
    if len(dmgs) != 1:
        raise RegressionError(f"TEST005_DMG_COUNT: {len(dmgs)}")
    return ArtifactSet(app_zips[0], dmgs[0], package_variant, manifest)


def run_command(
    arguments: Iterable[os.PathLike[str] | str],
    log: IO[str],
    *,
    environment: dict[str, str] | None = None,
    input_text: str | None = None,
    timeout: int = 120,
    check: bool = True,
) -> subprocess.CompletedProcess[str]:
    command = [os.fspath(argument) for argument in arguments]
    log.write(f"$ {' '.join(command)}\n")
    if input_text is not None:
        log.write("STDIN=Y (accept embedded DMG license for automated testing)\n")
    log.flush()
    try:
        result = subprocess.run(
            command,
            capture_output=True,
            text=True,
            env=environment,
            input=input_text,
            timeout=timeout,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        log.write(f"COMMAND_EXCEPTION: {error}\n")
        log.flush()
        raise RegressionError(f"TEST005_COMMAND_EXCEPTION: {command[0]}: {error}") from error
    if result.stdout:
        log.write(result.stdout)
        if not result.stdout.endswith("\n"):
            log.write("\n")
    if result.stderr:
        log.write(result.stderr)
        if not result.stderr.endswith("\n"):
            log.write("\n")
    log.write(f"EXIT={result.returncode}\n")
    log.flush()
    if check and result.returncode != 0:
        raise RegressionError(
            f"TEST005_COMMAND_FAILED: {command[0]} exit={result.returncode}"
        )
    return result


def find_single_app(directory: Path, code: str) -> Path:
    apps = sorted(path for path in directory.iterdir() if path.is_dir() and path.suffix == ".app")
    if len(apps) != 1:
        raise RegressionError(f"{code}: expected 1 app, got {len(apps)}")
    return apps[0]


def read_bundle_info(app: Path) -> BundleInfo:
    plist_path = app / "Contents" / "Info.plist"
    if not plist_path.is_file():
        raise RegressionError(f"TEST005_INFO_PLIST_MISSING: {app.name}")
    with plist_path.open("rb") as stream:
        plist = plistlib.load(stream)
    identifier = plist.get("CFBundleIdentifier")
    executable_name = plist.get("CFBundleExecutable")
    version = plist.get("CFBundleShortVersionString")
    if not all(isinstance(item, str) and item for item in (identifier, executable_name, version)):
        raise RegressionError(f"TEST005_BUNDLE_IDENTITY_INVALID: {app.name}")
    if plist.get("CFBundlePackageType") != "APPL":
        raise RegressionError(f"TEST005_BUNDLE_TYPE_INVALID: {app.name}")
    if not plist.get("NSLocalNetworkUsageDescription"):
        raise RegressionError(f"TEST005_LOCAL_NETWORK_USAGE_MISSING: {app.name}")
    if plist.get("NSBonjourServices") != EXPECTED_BONJOUR_SERVICES:
        raise RegressionError(f"TEST005_BONJOUR_SERVICES_INVALID: {app.name}")
    executable = app / "Contents" / "MacOS" / executable_name
    core_executable = app / "Contents" / "MacOS" / "deskflow-core"
    for item in (executable, core_executable):
        if not item.is_file() or not (item.stat().st_mode & stat.S_IXUSR):
            raise RegressionError(f"TEST005_BUNDLE_EXECUTABLE_INVALID: {item.name}")
    return BundleInfo(
        app,
        identifier,
        executable_name,
        version,
        executable,
        core_executable,
    )


def nested_code_candidates(app: Path) -> list[Path]:
    candidates: set[Path] = set()
    code_roots = (
        app / "Contents" / "MacOS",
        app / "Contents" / "Frameworks",
        app / "Contents" / "PlugIns",
        app / "Contents" / "Helpers",
        app / "Contents" / "XPCServices",
    )
    bundle_suffixes = {".app", ".framework", ".bundle", ".plugin", ".xpc"}
    binary_suffixes = {".dylib", ".so"}
    for root in code_roots:
        if not root.exists():
            continue
        for current, directories, files in os.walk(root, followlinks=False):
            current_path = Path(current)
            for directory in directories:
                candidate = current_path / directory
                if candidate.suffix in bundle_suffixes:
                    candidates.add(candidate)
            for filename in files:
                candidate = current_path / filename
                try:
                    mode = candidate.stat().st_mode
                except OSError:
                    continue
                if candidate.suffix in binary_suffixes or mode & stat.S_IXUSR:
                    candidates.add(candidate)
    return sorted(candidates, key=lambda item: (-len(item.parts), str(item)))


def diagnose_nested_codesign(app: Path, log: IO[str]) -> None:
    log.write("NESTED_CODESIGN_DIAGNOSTICS_BEGIN\n")
    for candidate in nested_code_candidates(app):
        result = run_command(
            ["/usr/bin/codesign", "--verify", "--strict", "--verbose=4", candidate],
            log,
            check=False,
        )
        log.write(
            f"NESTED_CODESIGN {'PASS' if result.returncode == 0 else 'FAIL'}: "
            f"{candidate.relative_to(app)}\n"
        )
    log.write("NESTED_CODESIGN_DIAGNOSTICS_END\n")
    log.flush()


def verify_adhoc_bundle(app: Path, log: IO[str]) -> None:
    verification = run_command(
        ["/usr/bin/codesign", "--verify", "--deep", "--strict", "--verbose=4", app],
        log,
        check=False,
    )
    if verification.returncode != 0:
        diagnose_nested_codesign(app, log)
        raise RegressionError(f"TEST005_CODESIGN_VERIFY_FAILED: {app.name}")
    display = run_command(
        ["/usr/bin/codesign", "--display", "--verbose=4", app], log
    )
    signature_details = f"{display.stdout}\n{display.stderr}"
    if "Signature=adhoc" not in signature_details:
        raise RegressionError(f"TEST005_SIGNATURE_NOT_ADHOC: {app.name}")
    if re.search(r"^Authority=", signature_details, re.MULTILINE):
        raise RegressionError(f"TEST005_ADHOC_HAS_AUTHORITY: {app.name}")


def verify_bundle_platform(bundle: BundleInfo, log: IO[str]) -> None:
    for executable in (bundle.executable, bundle.core_executable):
        architectures = run_command(
            ["/usr/bin/lipo", "-archs", executable], log
        ).stdout.split()
        if architectures != EXPECTED_MACOS_ARCHITECTURES:
            raise RegressionError(
                f"TEST005_ARCHITECTURES_INVALID: {executable.name}: {architectures}"
            )

        build = run_command(
            ["/usr/bin/xcrun", "vtool", "-show-build", executable], log
        ).stdout
        if re.search(r"^\s*platform\s+MACOS\s*$", build, re.MULTILINE) is None:
            raise RegressionError(f"TEST005_PLATFORM_INVALID: {executable.name}")
        minimum = re.search(r"^\s*minos\s+(\S+)\s*$", build, re.MULTILINE)
        if minimum is None or minimum.group(1) != EXPECTED_MACOS_MINIMUM_VERSION:
            found = minimum.group(1) if minimum is not None else "missing"
            raise RegressionError(
                f"TEST005_MINIMUM_MACOS_INVALID: {executable.name}: {found}"
            )


def framework_symlink_manifest(app: Path) -> dict[str, str]:
    frameworks_root = app / "Contents" / "Frameworks"
    frameworks = sorted(frameworks_root.glob("*.framework"))
    if not frameworks:
        raise RegressionError(f"TEST005_FRAMEWORKS_MISSING: {app.name}")

    manifest: dict[str, str] = {}
    for framework in frameworks:
        framework_root = framework.resolve(strict=True)
        links = (
            framework / "Versions" / "Current",
            framework / framework.stem,
            framework / "Resources",
        )
        for link in links:
            relative = link.relative_to(app).as_posix()
            if not link.is_symlink():
                raise RegressionError(f"TEST005_FRAMEWORK_LINK_INVALID: {relative}")
            try:
                resolved = link.resolve(strict=True)
                resolved.relative_to(framework_root)
            except (FileNotFoundError, ValueError):
                raise RegressionError(f"TEST005_FRAMEWORK_LINK_UNSAFE: {relative}")
            manifest[relative] = os.readlink(link)
    return manifest


def assert_same_bundle(left: BundleInfo, right: BundleInfo) -> None:
    left_identity = (left.identifier, left.executable_name, left.version)
    right_identity = (right.identifier, right.executable_name, right.version)
    if left_identity != right_identity:
        raise RegressionError(
            f"TEST005_BUNDLE_IDENTITY_MISMATCH: {left_identity} != {right_identity}"
        )
    if sha256(left.executable) != sha256(right.executable):
        raise RegressionError("TEST005_GUI_PAYLOAD_MISMATCH")
    if sha256(left.core_executable) != sha256(right.core_executable):
        raise RegressionError("TEST005_CORE_PAYLOAD_MISMATCH")


def create_user_data_sentinels(home: Path) -> tuple[UserDataSentinel, ...]:
    # Match the paths used by Settings::UserSettingFile and MainWindow's
    # PairingTrustRuntime on macOS. Keeping XDG_CONFIG_HOME out of the smoke
    # environment below is required for these defaults to be exercised.
    payloads = (
        (
            home / "Library" / "RelayDesk" / "RelayDesk.conf",
            b"[relaydesk]\ntest005Sentinel=test005-config\n",
            b"test005-config",
            False,
        ),
        (
            home / "Library" / "RelayDesk" / "relaydesk" / "trusted-devices.json",
            b'{"schemaVersion":1,"devices":[],"test005":true}\n',
            b'"test005":true',
            True,
        ),
        (
            home / "Downloads" / "RelayDesk" / "history" / "completed.json",
            b'{"kept":true}\n',
            b'"kept":true',
            True,
        ),
    )
    sentinels = []
    for path, payload, marker, require_exact_bytes in payloads:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)
        sentinels.append(
            UserDataSentinel(
                path=path,
                marker=marker,
                original_sha256=sha256(path) if require_exact_bytes else None,
            )
        )
    return tuple(sentinels)


def assert_user_data_preserved(sentinels: tuple[UserDataSentinel, ...]) -> None:
    for sentinel in sentinels:
        if not sentinel.path.is_file() or sentinel.marker not in sentinel.path.read_bytes():
            raise RegressionError(f"TEST005_USER_DATA_NOT_PRESERVED: {sentinel.path.name}")
        if sentinel.original_sha256 is not None and sha256(sentinel.path) != sentinel.original_sha256:
            raise RegressionError(f"TEST005_USER_DATA_CHANGED: {sentinel.path.name}")


def copy_app(source: Path, destination: Path, log: IO[str]) -> None:
    if destination.exists():
        raise RegressionError(f"TEST005_INSTALL_DESTINATION_EXISTS: {destination}")
    run_command(["/usr/bin/ditto", source, destination], log)
    if not destination.is_dir():
        raise RegressionError("TEST005_APP_COPY_FAILED")


def replace_app(source: Path, destination: Path, applications: Path, log: IO[str]) -> None:
    if not destination.is_dir() or destination.parent.resolve() != applications.resolve():
        raise RegressionError(f"TEST005_UNSAFE_UPGRADE_TARGET: {destination}")
    safe_remove_tree(destination, applications)
    copy_app(source, destination, log)


def smoke_bundle(bundle: BundleInfo, environment: dict[str, str], log: IO[str]) -> None:
    gui_result = run_command(
        [bundle.executable, "--version"], log, environment=environment, timeout=30
    )
    core_result = run_command(
        [bundle.core_executable, "--version"], log, environment=environment, timeout=30
    )
    if not (gui_result.stdout + gui_result.stderr).strip():
        raise RegressionError("TEST005_GUI_SMOKE_EMPTY")
    if not (core_result.stdout + core_result.stderr).strip():
        raise RegressionError("TEST005_CORE_SMOKE_EMPTY")


def write_report(path: Path, result: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def run_regression(args: argparse.Namespace) -> int:
    report_path = args.report.resolve()
    log_path = report_path.with_suffix(".commands.log")
    result: dict[str, Any] = {
        "task": "TEST-005-macos",
        "status": "NOT_RUN",
        "expectedCommit": args.expected_commit,
        "runner": {
            "os": os.environ.get("RUNNER_OS", ""),
            "arch": os.environ.get("RUNNER_ARCH", ""),
            "image": os.environ.get("ImageOS", ""),
        },
        "checks": {},
        "permissions": {
            "accessibility": {"status": "NOT_RUN", "reason": PERMISSION_NOT_RUN_REASON},
            "inputMonitoring": {"status": "NOT_RUN", "reason": PERMISSION_NOT_RUN_REASON},
            "localNetwork": {"status": "NOT_RUN", "reason": PERMISSION_NOT_RUN_REASON},
        },
        "signatureBoundary": {
            "app": "ad-hoc signature must pass codesign --deep --strict",
            "dmg": "unsigned internal container; integrity covered by SHA-256 and hdiutil verify",
            "developerId": "NOT_RUN",
            "notarization": "NOT_RUN",
        },
    }
    report_path.parent.mkdir(parents=True, exist_ok=True)
    failure: Exception | None = None
    test_root: Path | None = None
    mounted = False
    mount_point: Path | None = None

    with log_path.open("w", encoding="utf-8") as log:
        try:
            if sys.platform != "darwin":
                raise RegressionError("TEST005_MACOS_REQUIRED")
            runner_temp_value = os.environ.get("RUNNER_TEMP")
            if not runner_temp_value:
                raise RegressionError("TEST005_RUNNER_TEMP_REQUIRED")
            runner_temp = Path(runner_temp_value).resolve(strict=True)
            test_root = Path(tempfile.mkdtemp(prefix=TEST_ROOT_PREFIX, dir=runner_temp))
            test_root = validate_test_root(test_root, runner_temp)
            result["isolation"] = {
                "status": "PASS",
                "testRoot": str(test_root),
                "realHomeUntouched": True,
                "applicationsRoot": str(test_root / "Applications"),
            }

            artifacts = validate_artifacts(args.artifact_dir, args.expected_commit)
            result["checks"]["artifactManifestAndSha256"] = "PASS"
            result["artifacts"] = {
                "appZip": {
                    "name": artifacts.app_zip.name,
                    "sha256": sha256(artifacts.app_zip),
                },
                "dmg": {"name": artifacts.dmg.name, "sha256": sha256(artifacts.dmg)},
                "packageVariant": artifacts.package_variant,
            }

            isolated_home = test_root / "home"
            applications = test_root / "Applications"
            extraction = test_root / "app-zip"
            mount_point = test_root / "dmg-mount"
            temporary = test_root / "tmp"
            for directory in (isolated_home, applications, extraction, mount_point, temporary):
                directory.mkdir(parents=True, exist_ok=True)
            smoke_environment = os.environ.copy()
            smoke_environment.pop("XDG_CONFIG_HOME", None)
            smoke_environment.update(
                {
                    "HOME": str(isolated_home),
                    "CFFIXED_USER_HOME": str(isolated_home),
                    "TMPDIR": str(temporary),
                    "XDG_STATE_HOME": str(isolated_home / ".local" / "state"),
                    "XDG_DATA_HOME": str(isolated_home / ".local" / "share"),
                    "XDG_CACHE_HOME": str(isolated_home / ".cache"),
                }
            )
            sentinels = create_user_data_sentinels(isolated_home)

            run_command(["/usr/bin/ditto", "-x", "-k", artifacts.app_zip, extraction], log)
            zip_app = find_single_app(extraction, "TEST005_APP_ZIP_STRUCTURE")
            zip_info = read_bundle_info(zip_app)
            result["checks"]["appZipStructure"] = "PASS"
            zip_framework_links = framework_symlink_manifest(zip_app)
            result["checks"]["appZipFrameworkSymlinks"] = "PASS"
            verify_bundle_platform(zip_info, log)
            result["checks"]["appZipPlatform"] = "PASS"
            verify_adhoc_bundle(zip_app, log)
            result["checks"]["appZipCodesign"] = "PASS"

            run_command(["/usr/bin/hdiutil", "verify", artifacts.dmg], log, timeout=180)
            result["checks"]["dmgVerify"] = "PASS"
            run_command(
                [
                    "/usr/bin/hdiutil",
                    "attach",
                    "-readonly",
                    "-nobrowse",
                    "-noautoopen",
                    "-mountpoint",
                    mount_point,
                    artifacts.dmg,
                ],
                log,
                input_text="Y\n",
                timeout=180,
            )
            mounted = True
            result["checks"]["dmgLicenseAcceptedForAutomation"] = "PASS"
            result["checks"]["dmgAttach"] = "PASS"
            dmg_app = find_single_app(mount_point, "TEST005_DMG_STRUCTURE")
            dmg_info = read_bundle_info(dmg_app)
            dmg_framework_links = framework_symlink_manifest(dmg_app)
            if dmg_framework_links != zip_framework_links:
                raise RegressionError("TEST005_FRAMEWORK_LINK_MISMATCH")
            result["checks"]["dmgAppFrameworkSymlinks"] = "PASS"
            verify_bundle_platform(dmg_info, log)
            result["checks"]["dmgAppPlatform"] = "PASS"
            verify_adhoc_bundle(dmg_app, log)
            result["checks"]["dmgAppCodesign"] = "PASS"
            assert_same_bundle(zip_info, dmg_info)
            result["checks"]["zipAndDmgSameBundle"] = "PASS"
            result["bundle"] = {
                "identifier": zip_info.identifier,
                "version": zip_info.version,
                "executable": zip_info.executable_name,
            }

            installed_app = applications / zip_app.name
            copy_app(zip_app, installed_app, log)
            installed_info = read_bundle_info(installed_app)
            if framework_symlink_manifest(installed_app) != zip_framework_links:
                raise RegressionError("TEST005_INSTALLED_FRAMEWORK_LINK_MISMATCH")
            assert_same_bundle(zip_info, installed_info)
            verify_adhoc_bundle(installed_app, log)
            smoke_bundle(installed_info, smoke_environment, log)
            assert_user_data_preserved(sentinels)
            result["checks"]["cleanInstallAndLaunch"] = "PASS"

            stale_marker = installed_app / "Contents" / "Resources" / "test005-stale-install-marker"
            stale_marker.write_text("must be removed by same-bundle replacement\n", encoding="utf-8")
            replace_app(dmg_app, installed_app, applications, log)
            if stale_marker.exists():
                raise RegressionError("TEST005_UPGRADE_MERGED_STALE_BUNDLE_CONTENT")
            upgraded_info = read_bundle_info(installed_app)
            if framework_symlink_manifest(installed_app) != zip_framework_links:
                raise RegressionError("TEST005_UPGRADED_FRAMEWORK_LINK_MISMATCH")
            assert_same_bundle(zip_info, upgraded_info)
            verify_adhoc_bundle(installed_app, log)
            smoke_bundle(upgraded_info, smoke_environment, log)
            assert_user_data_preserved(sentinels)
            result["checks"]["sameBundleUpgradeAndLaunch"] = "PASS"

            safe_remove_tree(installed_app, applications)
            if installed_app.exists():
                raise RegressionError("TEST005_APP_UNINSTALL_FAILED")
            assert_user_data_preserved(sentinels)
            result["checks"]["appOnlyUninstall"] = "PASS"
            result["checks"]["externalConfigAndUserDataPreserved"] = "PASS"
            result["status"] = "PASS"
        except Exception as error:  # report evidence before returning failure
            failure = error
            result["status"] = "FAIL"
            result["error"] = f"{type(error).__name__}: {error}"
        finally:
            if mounted and mount_point is not None:
                detach_succeeded = False
                try:
                    detach = run_command(
                        ["/usr/bin/hdiutil", "detach", mount_point],
                        log,
                        check=False,
                        timeout=60,
                    )
                    if detach.returncode != 0:
                        detach = run_command(
                            ["/usr/bin/hdiutil", "detach", "-force", mount_point],
                            log,
                            check=False,
                            timeout=60,
                        )
                    detach_succeeded = detach.returncode == 0
                except Exception as detach_error:
                    log.write(f"DMG_DETACH_EXCEPTION: {detach_error}\n")
                if detach_succeeded:
                    result["checks"]["dmgDetachCleanup"] = "PASS"
                else:
                    result["checks"]["dmgDetachCleanup"] = "FAIL"
                    if failure is None:
                        failure = RegressionError("TEST005_DMG_DETACH_FAILED")
                        result["status"] = "FAIL"
                        result["error"] = str(failure)
            if test_root is not None and test_root.exists():
                try:
                    runner_temp = Path(os.environ["RUNNER_TEMP"]).resolve(strict=True)
                    safe_remove_tree(test_root, runner_temp)
                    result["checks"]["sandboxCleanup"] = "PASS"
                except Exception as cleanup_error:
                    result["checks"]["sandboxCleanup"] = "FAIL"
                    if failure is None:
                        failure = cleanup_error
                        result["status"] = "FAIL"
                        result["error"] = f"{type(cleanup_error).__name__}: {cleanup_error}"
            write_report(report_path, result)

    print(json.dumps(result, ensure_ascii=False, indent=2))
    if failure is not None:
        print(result.get("error", "TEST005_FAILED"), file=sys.stderr)
        return 1
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--artifact-dir", type=Path, required=True)
    parser.add_argument("--expected-commit", required=True)
    parser.add_argument("--report", type=Path, required=True)
    return run_regression(parser.parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
