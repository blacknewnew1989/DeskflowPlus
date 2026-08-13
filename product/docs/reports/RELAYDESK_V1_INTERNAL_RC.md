# RelayDesk v1 internal release candidate

## Scope

This candidate is an internal unsigned/ad-hoc Windows x64 and Apple Silicon macOS build based on
Deskflow v1.26.0. It includes the frozen RDFT v1 protocol, discovery/pairing/trust, authenticated
reconnect, transfer UI/history, single and multiple files, folders, conflict handling, SHA-256,
atomic commit and interrupted resume.

## Automated evidence already PASS

- Protocol freeze: tag `relaydesk-protocol-v1-20260813-01`, run `31672497950`.
- Windows real MSI clean install, repair, major upgrade, uninstall, service/firewall and residue
  lifecycle: PASS; see `TEST-005_WINDOWS_INSTALL_LIFECYCLE.md`.
- macOS App/DMG seal, mount, isolated launch, replace and uninstall while preserving user data:
  PASS; see `TEST-005_MACOS_INSTALL_LIFECYCLE.md`.
- Production file runtime: real pinned TLS loopbacks cover single file, two files plus nested empty
  folder, four conflict policies, Windows/macOS platform-safe atomic commit and a 20 MiB transfer
  interrupted at a 1 MiB durable checkpoint then resumed after listener restart.
- Final RC tag/run/artifact IDs and SHA-256 are filled after the exact tagged run completes.

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
