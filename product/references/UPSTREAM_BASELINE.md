# Verified Upstream Baseline

Verification date: 2026-08-12

## Official project

- Repository: https://github.com/deskflow/deskflow
- Release: https://github.com/deskflow/deskflow/releases/tag/v1.26.0
- Build documentation: https://github.com/deskflow/deskflow/blob/master/docs/dev/build.md
- Protocol reference: https://github.com/deskflow/deskflow/blob/master/docs/dev/protocol_reference.md
- REUSE metadata: https://github.com/deskflow/deskflow/blob/master/REUSE.toml

## Pinned baseline

- Tag: `v1.26.0`
- Release commit: `760e3b9`
- Release date shown by GitHub: 2026-02-16
- Official repository marks the release as Latest at verification time.

## Verified high-level facts

- Deskflow is an open-source software KVM for sharing keyboard/mouse/trackpad.
- TLS is enabled by default.
- Clipboard sharing is supported.
- Windows, macOS, Linux and BSD-derived systems are supported.
- Repository README currently states Windows 10 v1809+.
- Repository README currently states macOS 14+ for Intel/local builds and newer requirements for some Apple Silicon CI artifacts.
- GUI requires Qt 6.7+ according to current README/build documentation.
- Main code license identifier is `GPL-2.0-only WITH LicenseRef-OpenSSL-Exception`; every file still follows the pinned source REUSE metadata.

## Important

Master can change after this package was generated. Agents must use the pinned tag for the first baseline, and must inspect the actual source rather than relying solely on this summary.
