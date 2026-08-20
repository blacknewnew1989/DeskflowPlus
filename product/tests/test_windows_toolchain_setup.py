from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SETUP = (ROOT / "product/scripts/setup-windows.ps1").read_text(encoding="utf-8")
BUILD = (ROOT / "product/scripts/build-windows.ps1").read_text(encoding="utf-8")
PACKAGE = (ROOT / "product/scripts/package-windows.ps1").read_text(encoding="utf-8")
BUILD_SCRIPT = ROOT / "product/scripts/build-windows.ps1"
PWSH = shutil.which("pwsh")
DEPENDENCY_PROBE_OBJECT = (
    "src/lib/relaydesk/i18n/CMakeFiles/relaydesk_i18n.dir/ProductStrings.cpp.obj"
)


class WindowsToolchainSetupTests(unittest.TestCase):
    def test_qt_setup_installs_and_requires_declarative_deployment_tools(self) -> None:
        self.assertRegex(
            SETUP,
            r'foreach \(\$archive in @\([^)]*"qtdeclarative"[^)]*\)\)',
        )
        self.assertIn(r'"bin\qmlimportscanner.exe"', SETUP)

    def test_vcpkg_setup_fetches_the_manifest_baseline_for_shallow_clones(self) -> None:
        self.assertIn("ConvertFrom-Json", SETUP)
        self.assertIn("'builtin-baseline'", SETUP)
        self.assertIn("GIT_NO_LAZY_FETCH", SETUP)
        self.assertIn(":versions/baseline.json", SETUP)
        self.assertRegex(
            SETUP,
            r"fetch --refetch --depth=1 --no-tags --no-filter origin \$VcpkgBaseline",
        )
        self.assertIn("refs/relaydesk/vcpkg-baselines", SETUP)

    def test_windows_build_uses_stable_msvc_dependency_output(self) -> None:
        self.assertIn('$env:VSLANG = "1033"', BUILD)
        self.assertIn("TextInfo.OEMCodePage", BUILD)
        self.assertIn("[Text.Encoding]::GetEncoding($CodePage)", BUILD)
        self.assertIn("& chcp.com $CodePage | Out-Null", BUILD)
        self.assertNotIn("chcp.com 65001", BUILD)
        self.assertIn("MSVC_NINJA_DEPENDENCY_DISCOVERY_FAILED", BUILD)

    def test_release_packaging_uses_guarded_clean_build(self) -> None:
        self.assertIn('[switch]$CleanBuild', BUILD)
        self.assertIn('BUILD_CLEAN_PATH_OUTSIDE_REPOSITORY', BUILD)
        self.assertRegex(PACKAGE, r'-PackageVariant \$SigningPlan\.Status\s+`\s*\n\s*-CleanBuild')


@unittest.skipUnless(os.name == "nt" and PWSH, "Windows PowerShell behavior test")
class WindowsBuildCleanBehaviorTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_root = Path(tempfile.mkdtemp(prefix="relaydesk-clean-build-"))
        self.addCleanup(shutil.rmtree, self.temp_root)

    def prepare_case(
        self,
        name: str,
        dependency_count: int = 1,
        dependency_banner: str = "",
    ) -> tuple[Path, dict[str, str]]:
        case_root = self.temp_root / name
        repo_root = case_root / "repo"
        tools_root = case_root / "tools"
        qt_root = case_root / "qt"
        repo_root.mkdir(parents=True)
        tools_root.mkdir()

        (tools_root / "cmake.cmd").write_text("@exit /b 0\r\n", encoding="ascii")
        ninja_lines = ["@echo off"]
        if dependency_banner:
            ninja_lines.append(f"echo {dependency_banner}")
        ninja_lines.extend(
            (
                f"echo {DEPENDENCY_PROBE_OBJECT}: #deps {dependency_count}, "
                "deps mtime 1 ^(VALID^)",
                "@exit /b 0",
            )
        )
        (tools_root / "ninja.cmd").write_text(
            "\r\n".join(ninja_lines) + "\r\n",
            encoding="ascii",
        )
        shutil.copy2(Path(os.environ["SystemRoot"]) / "System32/cmd.exe", tools_root / "cl.exe")

        for relative in (
            "lib/cmake/Qt6/Qt6Config.cmake",
            "lib/cmake/Qt6Svg/Qt6SvgConfig.cmake",
            "bin/lrelease.exe",
            "plugins/platforms/qwindows.dll",
        ):
            target = qt_root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.touch()

        escaped_qt_root = str(qt_root).replace("'", "''")
        (repo_root / ".relaydesk-toolchain-windows.ps1").write_text(
            f"$env:RELAYDESK_QT_PREFIX = '{escaped_qt_root}'\n",
            encoding="utf-8",
        )
        environment = os.environ.copy()
        environment["PATH"] = str(tools_root) + os.pathsep + environment["PATH"]
        return repo_root, environment

    def make_junction(self, link: Path, target: Path) -> None:
        environment = os.environ.copy()
        environment["RELAYDESK_TEST_JUNCTION_LINK"] = str(link)
        environment["RELAYDESK_TEST_JUNCTION_TARGET"] = str(target)
        command = (
            "New-Item -ItemType Junction "
            "-Path $env:RELAYDESK_TEST_JUNCTION_LINK "
            "-Target $env:RELAYDESK_TEST_JUNCTION_TARGET | Out-Null"
        )
        subprocess.run(
            [PWSH, "-NoProfile", "-Command", command],
            check=True,
            capture_output=True,
            text=True,
            encoding="utf-8",
            env=environment,
        )

    def run_clean_build(self, repo_root: Path, environment: dict[str, str]) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                PWSH,
                "-NoProfile",
                "-File",
                str(BUILD_SCRIPT),
                "-RepoRoot",
                str(repo_root),
                "-Configuration",
                "Release",
                "-SkipAutoSetup",
                "-CleanBuild",
            ],
            check=False,
            capture_output=True,
            text=True,
            encoding="oem",
            env=environment,
        )

    def test_clean_build_replaces_a_normal_configuration_directory(self) -> None:
        repo_root, environment = self.prepare_case("normal")
        build_dir = repo_root / "build/windows/release"
        build_dir.mkdir(parents=True)
        sentinel = build_dir / "stale-object.txt"
        sentinel.write_text("stale", encoding="ascii")

        result = self.run_clean_build(repo_root, environment)

        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertTrue(build_dir.is_dir())
        self.assertFalse(sentinel.exists())

    def test_build_rejects_empty_msvc_ninja_dependencies(self) -> None:
        repo_root, environment = self.prepare_case("empty-dependencies", dependency_count=0)

        result = self.run_clean_build(repo_root, environment)

        self.assertNotEqual(0, result.returncode)
        self.assertIn(
            "MSVC_NINJA_DEPENDENCY_DISCOVERY_FAILED",
            result.stdout + result.stderr,
        )

    def test_build_accepts_dependency_report_after_a_wrapper_banner(self) -> None:
        repo_root, environment = self.prepare_case(
            "dependency-banner",
            dependency_banner="ninja wrapper warning",
        )

        result = self.run_clean_build(repo_root, environment)

        self.assertEqual(0, result.returncode, result.stdout + result.stderr)

    def test_clean_build_rejects_each_reparse_point_in_the_delete_path(self) -> None:
        for component in ("build", "build/windows", "build/windows/release"):
            with self.subTest(component=component):
                repo_root, environment = self.prepare_case(component.replace("/", "-"))
                external_target = repo_root.parent / "outside" / component.replace("/", "-")
                external_target.mkdir(parents=True)
                sentinel = external_target / "sentinel.txt"
                sentinel.write_text("keep", encoding="ascii")

                link = repo_root / component
                link.parent.mkdir(parents=True, exist_ok=True)
                self.make_junction(link, external_target)

                result = self.run_clean_build(repo_root, environment)

                self.assertNotEqual(0, result.returncode)
                self.assertIn(
                    "BUILD_CLEAN_PATH_OUTSIDE_REPOSITORY",
                    result.stdout + result.stderr,
                )
                self.assertTrue(sentinel.exists())


if __name__ == "__main__":
    unittest.main()
