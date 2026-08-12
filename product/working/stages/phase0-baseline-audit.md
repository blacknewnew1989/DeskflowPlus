# Phase 0 baseline audit (BASE-004)

Date: 2026-08-12 (Asia/Shanghai)
Owner: A1 upstream/build investigation
Branch: `agent/a1/phase0-audit`
Audited integration baseline: `9b0a4111141abe0a619d5eaeea87b8690b771f70`

## Result

Deskflow v1.26.0 is imported on the expected immutable baseline. The tag resolves to
`760e3b99b00053647a96b405276bf614bd860075`, and that commit is the direct parent of
the RelayDesk bootstrap commit. The bootstrap diff adds only RelayDesk materials and
the dedicated workflow; no upstream Deskflow source, CMake, test, or deploy file is
modified.

The upstream build layout is suitable for incremental product work: input capture,
injection, screen switching, clipboard, TLS, client/server protocol, and platform
implementations are already separated into static libraries. RelayDesk should add
shared services alongside these targets and keep the existing `platform`, `client`,
`server`, and `app` paths intact.

Local Windows configure/build is currently **NOT_RUN** because the host lacks CMake,
Ninja, Qt, OpenSSL, vcpkg, and an MSVC C++ toolset. The checked-in
`relaydesk-build.yml` has the required Windows 2022 and macOS 15 fallback matrix.

## Baseline evidence

- `git rev-parse v1.26.0^{commit}`:
  `760e3b99b00053647a96b405276bf614bd860075`.
- `git show -s HEAD`: bootstrap commit `9b0a4111`, parent `760e3b99`.
- `git merge-base --is-ancestor v1.26.0^{commit} HEAD`: exit 0.
- `git diff v1.26.0^{commit}..HEAD`: 89 added files under `product/`, root
  `AGENTS.md`, and `.github/workflows/relaydesk-build.yml`; no existing upstream
  source/build/deploy file changed.
- Required upstream layout exists: `CMakeLists.txt`, `src/apps`, `src/lib`,
  `src/lib/platform`, `src/unittests`, and `deploy/{windows,mac}`.
- Root and `product/AGENTS.md` have identical SHA-256
  `35920AC37366A18653D1DD1C1FBAA79D2121765B92BD7B42ED6A4FACDF812D4A`.

## Real source modules and CMake targets

The top-level project requires CMake 3.24, C++20, Qt 6.7.0+, and OpenSSL 3.0+.
`src/CMakeLists.txt` builds libraries first, then applications, then tests when
`BUILD_TESTS=ON`.

| Area | CMake target | Confirmed responsibility/evidence |
|---|---|---|
| OS abstraction | `arch` | Win32/Unix daemon, logging, threading, and network abstractions. |
| Foundation | `base` | Event queue, logging, strings, exceptions, and shared event types. |
| Client role | `client` | `Client` and `ServerProxy`; keep for the existing Deskflow client role. |
| Shared settings | `common` | Settings, i18n, coordinates, platform information, and Qt settings proxy. |
| Core application library | `app` | App lifecycle, client/server app code, clipboard/chunk/key-map/screen logic. |
| Streams | `io` | Stream interfaces, buffering, filters, and I/O exceptions. |
| Threads | `mt` | Mutex, condition variable, lock, and thread wrappers. |
| Network/TLS | `net` | Sockets, multiplexer, secure sockets, fingerprints, and trust database. |
| Native platform | `platform` | Windows and macOS screen, key, clipboard, power, event, and session adapters. |
| Server role | `server` | Client listener/proxies, primary screen routing, and server configuration/runtime. |
| Qt GUI | `gui` | GUI models/controllers, connection helpers, settings UI, diagnostics, and hotkeys. |

Confirmed executable targets:

- `deskflow-core` on both platforms, linked to the existing shared/core libraries;
- `deskflow-daemon` on Windows only for UAC/secure desktop handling;
- GUI target `deskflow` on Windows and bundle executable `Deskflow` on macOS;
- `legacytests`, built separately from CTest-registered Qt tests;
- `wix-custom` on Windows when the installer deploy directory is enabled.

Generated utility/package targets include `run_tests` (unless
`SKIP_BUILD_TESTS=ON`), `package`, and `package_source`.

## Tests

`src/unittests/CMakeLists.txt` registers focused Qt Test executables with CTest and
builds a separate GoogleTest-based `legacytests` binary (GoogleTest 1.15.2 is fetched
only when a system package is unavailable).

- Windows registers 22 focused CTest tests: base (4), common (2), deskflow (5), GUI
  (5), network/TLS (3), Windows clipboard (1), and server (2), plus `legacytests`.
- macOS registers 23 focused CTest tests: the same common groups with macOS clipboard
  and key-state tests (2), plus `legacytests`.
- Upstream CI runs CTest and `legacytests` separately, then installs/opens the package
  enough to execute `deskflow-core --version`.
- RelayDesk CI configures with `BUILD_TESTS=ON` and `SKIP_BUILD_TESTS=ON`, then runs
  CTest explicitly. Its test step is diagnostic (`continue-on-error`) and is not a
  required check, matching the repository policy.

No existing upstream test covers the planned independent RelayDesk file channel,
resume metadata, path policy, or transfer scheduling; those require new focused
targets rather than changes to input-path tests.

## Packaging structure

### Windows

- `deploy/windows/deploy.cmake` always enables CPack `7Z` output.
- If `wix` is discoverable it also enables the WiX generator (CPack WiX v4 API),
  x64/arm64 architecture metadata, UI/Util/Firewall extensions, firewall/service
  patching, and the `wix-custom` MSI custom-action DLL.
- Installed runtime set includes the GUI, `deskflow-core`, Windows-only daemon,
  required runtime DLLs, Qt deployment output, translations, licenses, and metadata.
- `product/scripts/package-windows.ps1` runs setup, Release build, CTest,
  `package package_source`, then collects accepted packages into
  `dist/windows/<full-commit>/` with `SHA256SUMS.txt` and
  `artifact-manifest.json`.

### macOS

- The GUI target is a `Deskflow.app` bundle. `deskflow-core` is installed in the
  bundle's `Contents/MacOS` directory, with bundle resources and licenses.
- `deploy/mac/deploy.cmake` invokes the Qt deploy tool with ad-hoc signing
  (`-codesign=-`) and configures the CPack `DragNDrop` generator for a DMG.
- `product/scripts/package-macos.sh` targets arm64/macOS 14, runs CTest and
  `package package_source`, and collects the DMG/source package. The collector also
  zips the app bundle as `RelayDesk-macos-arm64-unsigned-<sha>.app.zip` and records
  SHA-256 values.

### GitHub Actions fallback

`.github/workflows/relaydesk-build.yml` uses:

- `windows-2022`, amd64, Qt 6.10.1, vcpkg `x64-windows-release`, Ninja;
- `macos-15`, arm64, deployment target 14, Qt 6.10.1, Ninja;
- configure, build, binary/source package, explicit CTest diagnostics, collection,
  SHA-256 manifest, and 30-day artifacts named
  `relaydesk-<platform>-<full-sha>`.

The workflow is triggered by the integration/release branches, phase/version tags,
or manual dispatch and does not introduce an approval gate.

## Local Windows toolchain audit

Host observed during this audit: Windows NT 10.0.26200.0 x64, PowerShell 7.6.3.

| Component | Status | Evidence |
|---|---|---|
| Git | PASS | 2.50.1.windows.1 |
| Python | PASS | 3.13.7 |
| winget | PASS | 1.29.280 |
| .NET SDK | PASS | 10.0.400 (also 10.0.100-rc.1 installed) |
| Visual Studio/MSBuild | PARTIAL | Build Tools 18.4.2 and MSBuild exist. |
| MSVC C++ x64 | MISSING | `vswhere -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64` returns no installation; no `VC/Tools/MSVC` or `vcvars64.bat`. |
| Windows SDK | MISSING | No discovered Windows Kits 10 SDK bin directory. |
| CMake/CTest/CPack | MISSING | Commands absent from PATH and VS has no bundled CMake component. |
| Ninja | MISSING | Command and VS bundled path absent. |
| Qt 6 | MISSING | No command, configured prefix, conventional `C:\Qt`, or ready worktree Qt tree. |
| OpenSSL 3 | MISSING | Command absent; no configured vcpkg tree. |
| vcpkg | MISSING | No `VCPKG_ROOT`, conventional checkout, or worktree checkout. |
| WiX | MISSING | `wix` and the user dotnet-tools directory are absent. |
| 7-Zip | MISSING | `7z` absent from PATH. |
| GitHub CLI | MISSING | `gh` absent locally; A0 can use its connected GitHub tooling. |

Because CMake and the compiler are unavailable, a truthful local configure, target
enumeration from a generated build tree, compilation, CTest run, and CPack run are
all **NOT_RUN**. No large dependency installation or full package script was started
by A1; A0/A4 own environment preparation and the package run.

Important setup finding: `setup-windows.ps1` currently tests only whether
`vswhere.exe` exists before deciding whether to install VS 2022 Build Tools. This host
has `vswhere.exe` from VS Build Tools 18 but no VC toolset, so the script skips the
VS workload installation. `build-windows.ps1` later detects missing `cl.exe` and
fails over correctly, but setup can under-report the compiler gap after the other
tools are installed. A4 should change the setup check to query
`Microsoft.VisualStudio.Component.VC.Tools.x86.x64` (and a usable Windows SDK), or
accept the Actions fallback explicitly.

## Validation performed

PASS:

- `python product/scripts/validate-package.py` — 48 required files, 4 JSON files,
  and 6 protocol vectors validated.
- Python bytecode compilation of `product/scripts` (generated cache removed after
  the check).
- PowerShell parser check for Windows setup/build/package scripts.
- `bash -n` parser check for macOS setup/build/package scripts using Git for Windows.
- YAML parse of `.github/workflows/relaydesk-build.yml`.
- Git baseline ancestry and bootstrap-only diff checks.

NOT_RUN:

- Windows CMake configure/build/CTest/CPack: incomplete local toolchain.
- macOS local build/App/DMG: this session is Windows; use the macOS Actions job.
- Windows↔macOS input, scroll, clipboard, permissions, sleep/reconnect, and package
  smoke tests: require generated packages and both real platforms.

## Handoff to A0/A4/A5/A7

1. Keep the current upstream library/application structure; do not replace the
   mature `client`, `server`, `app`, `net`, or native `platform` paths.
2. Use the checked-in Actions matrix immediately while local Windows dependencies
   are incomplete.
3. Harden the VS workload detection before treating local setup as ready.
4. Record real CTest/CPack results and artifact checksums only after the corresponding
   Actions jobs or platform builds complete; retain the `NOT_RUN` labels above until
   then.
