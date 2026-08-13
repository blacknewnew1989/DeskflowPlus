# RelayDesk v1 internal release candidate

## Scope

This candidate is an internal unsigned/ad-hoc Windows x64 and Apple Silicon macOS build based on
Deskflow v1.26.0. It includes the frozen RDFT v1 protocol, discovery/pairing/trust, authenticated
reconnect, transfer UI/history, single and multiple files, folders, conflict handling, SHA-256,
atomic commit and interrupted resume.

## Final release identity

- Product commit: `05f92a1ab721f7fd8b893e47e05643d5988e1719`.
- Annotated tag: `relaydesk-phase4-20260813-03` (tag object
  `7254073dc61b1053f67dbea7e55c3e249a80e782`).
- GitHub Actions run: `31706167585` (`SUCCESS`).
- Draft unsigned release: `RelayDesk internal relaydesk-phase4-20260813-03`.

The earlier tags remain immutable. `relaydesk-phase4-20260813-01` records a Windows compile failure
fixed by `4903df2d1`; `relaydesk-phase4-20260813-02` is the first complete internal candidate. The
current `-03` candidate additionally fixes the missing pairing-to-Deskflow-layout composition bridge:
trusted input-capable peers are inserted idempotently into `ServerConfig` after pairing or trusted
rediscovery, while external configurations, invalid names and full layouts remain untouched.

## Automated evidence PASS

- Protocol freeze: tag `relaydesk-protocol-v1-20260813-01`, run `31672497950`.
- Windows real MSI clean install, repair, major upgrade, uninstall, service/firewall and residue
  lifecycle: PASS; see `TEST-005_WINDOWS_INSTALL_LIFECYCLE.md`.
- macOS App/DMG seal, mount, isolated launch, replace and uninstall while preserving user data:
  PASS; see `TEST-005_MACOS_INSTALL_LIFECYCLE.md`.
- Production file runtime: real pinned TLS loopbacks cover single file, two files plus nested empty
  folder, four conflict policies, Windows/macOS platform-safe atomic commit and a 20 MiB transfer
  interrupted at a 1 MiB durable checkpoint then resumed after listener restart.
- Exact-tag Windows job `94467163015`: build/package PASS, CTest 89/89, and the real unsigned MSI
  clean-install/repair/major-upgrade/uninstall/service/firewall/residue/data-preservation suite PASS.
- Exact-tag macOS job `94467163121`: build/package PASS, CTest 90/90, strict ad-hoc App verification
  PASS. Install lifecycle job `94470799096` verifies ZIP symlinks, DMG, isolated launch, replacement,
  App-only uninstall and user-data preservation.
- `RelayDeskInputLayoutTests` covers first insertion and persistence, repeated-observation idempotency,
  trust/input eligibility, invalid names and external-configuration preservation on both platforms.
- Draft release publication job `94470799137`: PASS.

## Final packages

All four files were downloaded from the exact-tag draft release and hashed again locally. The local
hashes match both the GitHub release-asset digests and `SHA256SUMS.txt`.

| Platform | Package | Bytes | SHA-256 |
|---|---|---:|---|
| Windows x64 | `relaydesk-05f92a1ab721f7fd8b893e47e05643d5988e1719-win-x64-unsigned.msi` | 16,243,769 | `28340705a8c31d663cd5f10ea605679210c5fec393048c5a2070ae92335d2f07` |
| Windows x64 | `relaydesk-05f92a1ab721f7fd8b893e47e05643d5988e1719-win-x64-unsigned-portable.7z` | 13,244,230 | `51e88f915007d51f7efcbe0a9e8496720edebb2b1ac98371584070eedf22655d` |
| macOS arm64 | `RelayDesk-macos-arm64-adhoc-05f92a1a.app.zip` | 28,821,846 | `ad1a56cd74b32a7ebb499b73376a019745fe3a8e42ce69f1e73bc0696430b8af` |
| macOS arm64 | `relaydesk-05f92a1ab721f7fd8b893e47e05643d5988e1719-macos-arm64-adhoc.dmg` | 29,068,808 | `0377d49f7bbb9284f666f2033219b5f39c73d7a496238257881ef299a35e2b29` |

Actions artifact evidence:

- Windows artifact `9183676968`, API ZIP digest
  `d0cd7ab0aee49473d62cd0673a2f0b9e80c6b04a6906fc43c375b2f748161e2c`.
- macOS artifact `9183524798`, API ZIP digest
  `4e03738e2186ff214081546875594c9c463615401dd5e81130683ba2f371013f`.
- macOS lifecycle artifact `9183692586`, API ZIP digest
  `afc76c0ab786e7be7606e4b2f3f0622085f75f036c803273c09db6412c8630b8`.

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
