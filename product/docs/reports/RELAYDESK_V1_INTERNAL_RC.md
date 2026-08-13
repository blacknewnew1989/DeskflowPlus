# RelayDesk v1 internal release candidate

## Scope

This candidate is an internal unsigned/ad-hoc Windows x64 and Apple Silicon macOS build based on
Deskflow v1.26.0. It includes the frozen RDFT v1 protocol, discovery/pairing/trust, authenticated
reconnect, transfer UI/history, single and multiple files, folders, conflict handling, SHA-256,
atomic commit and interrupted resume.

## Final release identity

- Product commit: `4903df2d1c0ea8c37a28db2e0e9f743daa566e90`.
- Annotated tag: `relaydesk-phase4-20260813-02` (tag object
  `bf6de5412e587039469d7baa0416ef6a2a2cc8a0`).
- GitHub Actions run: `31688962563` (`SUCCESS`).
- Draft unsigned release: `RelayDesk internal relaydesk-phase4-20260813-02`.

The earlier `relaydesk-phase4-20260813-01` tag is retained as an immutable failed-attempt record. Its
Windows build exposed a file-scope initialization error that was fixed by commit `4903df2d1`; it is
not a release candidate and was not moved.

## Automated evidence PASS

- Protocol freeze: tag `relaydesk-protocol-v1-20260813-01`, run `31672497950`.
- Windows real MSI clean install, repair, major upgrade, uninstall, service/firewall and residue
  lifecycle: PASS; see `TEST-005_WINDOWS_INSTALL_LIFECYCLE.md`.
- macOS App/DMG seal, mount, isolated launch, replace and uninstall while preserving user data:
  PASS; see `TEST-005_MACOS_INSTALL_LIFECYCLE.md`.
- Production file runtime: real pinned TLS loopbacks cover single file, two files plus nested empty
  folder, four conflict policies, Windows/macOS platform-safe atomic commit and a 20 MiB transfer
  interrupted at a 1 MiB durable checkpoint then resumed after listener restart.
- Exact-tag Windows job `94411592107`: build/package PASS, CTest 88/88, and the real unsigned MSI
  clean-install/repair/major-upgrade/uninstall/service/firewall/residue/data-preservation suite PASS.
- Exact-tag macOS job `94411592029`: build/package PASS, CTest 89/89, strict ad-hoc App verification
  PASS. Install lifecycle job `94415359394` verifies ZIP symlinks, DMG, isolated launch, replacement,
  App-only uninstall and user-data preservation.
- Draft release publication job `94415359536`: PASS.

## Final packages

All four files were downloaded from the exact-tag draft release and hashed again locally. The local
hashes match both the GitHub release-asset digests and `SHA256SUMS.txt`.

| Platform | Package | Bytes | SHA-256 |
|---|---|---:|---|
| Windows x64 | `relaydesk-4903df2d1c0ea8c37a28db2e0e9f743daa566e90-win-x64-unsigned.msi` | 16,239,673 | `35c7ebcc5538b553e866b1f8e38bda2d0951248defddaef557a03da732845d1c` |
| Windows x64 | `relaydesk-4903df2d1c0ea8c37a28db2e0e9f743daa566e90-win-x64-unsigned-portable.7z` | 13,241,161 | `c4bf6ba0ca094233dff4246be3b6cbce8fa8cae4908e87057cc3556c4f12bfd2` |
| macOS arm64 | `RelayDesk-macos-arm64-adhoc-4903df2d.app.zip` | 28,818,619 | `9ac817a661081b519a5009579bca502611f6d9c0da0758799a5a753c9ed77097` |
| macOS arm64 | `relaydesk-4903df2d1c0ea8c37a28db2e0e9f743daa566e90-macos-arm64-adhoc.dmg` | 29,063,661 | `7d4af9b3a4935a49d791fc2837992e50703bed9879fe21c0ce10d1659bab1d27` |

Actions artifact evidence:

- Windows artifact `9177022266`, API ZIP digest
  `e3e6387cdf054aa1a1fb596e38bb7ce00dc971e1047c35cb29da5da073d6af54`.
- macOS artifact `9176744262`, API ZIP digest
  `bbba52bd0f2785848cc3971d5f3abcb073c7b09f67f4e56287b4621d108efdda`.
- macOS lifecycle artifact `9177032890`, API ZIP digest
  `e5110d6d38e4ccc24f3fecbdabb979fddfb30c0714ecc31b59900ab3c5df077f`.

## Internal installation

### Windows

Use the `unsigned.msi` for installation or the `unsigned-portable.7z` for a portable trial. The
package is intentionally unsigned when no certificate is configured. Windows may show an unknown
publisher warning; inspect the recorded SHA-256 before continuing. The MSI installs the RelayDesk
service and private-network firewall rules and preserves user configuration on uninstall.

### macOS

Use the ad-hoc App ZIP or DMG. Drag RelayDesk to Applications. Because the internal build is not
notarized, the first launch may require Finder Open or the macOS Privacy & Security confirmation.
Grant Local Network, Accessibility and Input Monitoring only when macOS prompts. The App-only
uninstall preserves RelayDesk settings/trust/history under the user's Library.

## Final user acceptance required

The following require two physical machines and OS interaction and therefore remain `NOT_RUN` in
automation:

1. Grant macOS Local Network, Accessibility and Input Monitoring permissions.
2. Confirm Windows private-network firewall access when prompted.
3. Pair one Windows and one macOS device and confirm the displayed code/fingerprint.
4. Verify mouse, keyboard, wheel, text clipboard and image clipboard in both directions.
5. Send a single file, multiple files and a folder in both directions; exercise AutoRename,
   Overwrite, Skip and Ask.
6. Interrupt a large transfer by disconnecting the network, reconnect, and confirm it resumes from
   a non-zero durable offset.
7. Sleep/wake each device once and confirm discovery/reconnect recovers.

Developer ID, Windows Authenticode, notarization and interactive SmartScreen/Gatekeeper behavior are
`NOT_RUN` without real signing credentials. Their absence does not invalidate the internal package.
