# TEST-005 macOS Install Lifecycle Regression

## Final execution record

| Field | Value |
|---|---|
| Result | **PASS** |
| Tested commit | `4377afeed9816fc503c30705681532af274fa5a9` |
| Branch | `agent/a7/macos-install-regression` |
| Canonical workflow | `relaydesk-build.yml` |
| Workflow run | [31657596578](https://github.com/blacknewnew1989/DeskflowPlus/actions/runs/31657596578) |
| macOS package job | `94315371007` — PASS |
| macOS lifecycle job | `94317213373` — PASS |
| Runner | GitHub-hosted `macos-14`, Apple Silicon (`ARM64`) |
| Package boundary | ad-hoc App signature; unsigned internal DMG container |
| Test root | `/Users/runner/work/_temp/relaydesk-test005-macos-z0u6c5ae` |
| Date | 2026-08-13 |

The canonical workflow built both package matrix entries before running the macOS
lifecycle job. The macOS package job generated the DMG, staged the final App,
ran `codesign --verify --deep --strict --verbose=4`, and reported both `valid on
disk` and `satisfies its Designated Requirement`. The job also passed all 76
tests at this commit.

## Artifact identity

| Artifact | GitHub artifact ID | GitHub API digest | Size |
|---|---:|---|---:|
| macOS packages | `9165097233` | `sha256:ae11fd8d46e4f63c964e8cab76b2aaa6345b73a7b2e494138278deea952db1cf` | 61,594,340 bytes |
| TEST-005 evidence | `9165178241` | `sha256:8bf7febb76c53965af6a922707307f1673edc079ef3644497620d5c1b83078e9` | 9,227 bytes |

The package manifest, `SHA256SUMS.txt`, lifecycle report, and hashes calculated
again after downloading the artifacts agree:

| File | SHA-256 |
|---|---|
| `RelayDesk-macos-arm64-adhoc-4377afee.app.zip` | `245e58be387855d669aec315d59b479605ec0d6c5530184b9b72755be3cf8dbe` |
| `relaydesk-4377afeed9816fc503c30705681532af274fa5a9-macos-arm64-adhoc.dmg` | `bae891ad14835943e9eb755d4085e6f0f29ba74cdb68f7ae943c1497e5501ed3` |
| `test005-macos-install-regression.json` | `8ed6be86e1793c2a80c49a55babbe6d603722973b492e84e57913723886b9636` |
| `test005-macos-install-regression.commands.log` | `b55771058142765f315b1282cddfe72712e0d0b964c007438baed8e4e2040dd3` |

The evidence report records `status=PASS`, expected commit
`4377afeed9816fc503c30705681532af274fa5a9`, bundle identifier
`local.relaydesk.desktop`, version `1.26.0.9999`, and package variant `adhoc`.

## ZIP structure and signature evidence

The App ZIP was produced and extracted with `/usr/bin/ditto`. A read-only
inspection of its ZIP central directory after artifact download found:

| Check | Actual | Result |
|---|---:|---|
| ZIP entries | 121 | PASS |
| Unix symbolic links | 18 | PASS |
| Qt frameworks | 6 | PASS |
| `Versions/Current` framework links | 6 / 6 | PASS |
| Top-level framework executable links | 6 / 6 | PASS |
| Top-level framework `Resources` links | 6 / 6 | PASS |

The six frameworks are QtCore, QtDBus, QtGui, QtNetwork, QtSvg, and QtWidgets.
This topology proves the ZIP retained the framework links that Python's former
generic ZIP collector had flattened. After extraction, the App passed
`codesign --verify --deep --strict --verbose=4`; `codesign --display
--verbose=4` reported `Signature=adhoc`, `TeamIdentifier=not set`, and no
`Authority=` entry.

## Install lifecycle evidence

| Step | Actual evidence | Result |
|---|---|---|
| Manifest and SHA validation | exact commit/platform/variant, size, and SHA matched the downloaded App ZIP and DMG | PASS |
| App ZIP extraction | `ditto -x -k` into the unique runner-temp sandbox | PASS |
| App ZIP signature | full nested `codesign --deep --strict --verbose=4`; App valid on disk | PASS |
| DMG integrity | `hdiutil verify`; image checksum reported valid | PASS |
| Embedded SLA automation | `hdiutil attach` received `Y` on stdin; the GPL SLA remains embedded for user installation | PASS |
| DMG mount | read-only, no-browse, no-auto-open mount at the sandbox mount point (`/dev/disk4s1`) | PASS |
| Mounted App signature | full nested strict codesign; same bundle identity and GUI/core payload hashes as the ZIP App | PASS |
| Isolated install | `ditto` copied the ZIP App only to the sandbox `Applications` directory; strict codesign passed after copy | PASS |
| Controlled launch | installed GUI and `deskflow-core` each ran `--version` non-interactively and exited zero | PASS |
| Same-bundle upgrade | a stale marker was inserted, the validated App target was removed, and the mounted-DMG App replaced it; the stale marker was absent | PASS |
| Upgraded launch | strict codesign plus GUI/core `--version` passed again from the same bundle path | PASS |
| App-only uninstall | only the App beneath the sandbox `Applications` root was removed | PASS |
| User-data preservation | config, trusted-device, and received-history sentinels retained their SHA-256 values | PASS |
| Cleanup | `hdiutil detach` ejected `disk4`; sandbox removal passed | PASS |

The smoke environment redirected `HOME`, `TMPDIR`, and XDG configuration,
data, and cache roots beneath `RUNNER_TEMP`. It did not write to the runner's
real home or `/Applications`. The report explicitly records
`realHomeUntouched=true`.

## Failure lineage and resolved causes

- Phase 1 run `31623677270` is not valid signature evidence. Its log contained
  `codesign verification error: nested code is modified or invalid`, but the
  old install-time command did not propagate the nonzero result, so the job
  appeared successful.
- Phase 2 run `31654263274` exposed the same nested-signature failure as a hard
  CPack failure. Qt was raised from 6.10.1 to 6.10.2, which contains the upstream
  macdeployqt signing-order correction.
- Run `31655714399` then correctly failed strict verification at
  `QtDBus.framework`, proving the generic Python ZIP path had flattened Qt
  framework symlinks. Commit `f18863736` switched App ZIP collection to
  `ditto`.
- Commit `c6e238938` registered all App resource install rules before
  macdeployqt, making deployment/signing the final bundle mutation, and added
  final Stage strict verification rather than masking the problem with a second
  manual signature.
- Run `31656450252` passed ZIP strict verification and `hdiutil verify`, then
  stopped at the embedded DMG license prompt with `hdiutil: attach canceled`.
  Commit `4377afeed` supplied the documented interactive acceptance input for
  automation without removing the SLA. Final run `31657596578` passed the
  complete lifecycle.

## Explicit NOT_RUN boundary

The hosted runner did not and cannot approve or observe the macOS System
Settings UI for Accessibility, Input Monitoring, or Local Network; all three
remain **NOT_RUN**. A real Developer ID signature, notarization, Gatekeeper
first-open confirmation, interactive application session, and installation into
the real `/Applications` directory also remain **NOT_RUN**. These do not alter
the verified ad-hoc/unsigned internal-package result and must not be reported as
production signing or end-user permission acceptance.
