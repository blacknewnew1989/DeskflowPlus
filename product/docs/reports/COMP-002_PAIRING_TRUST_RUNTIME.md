# COMP-002 Pairing and trust runtime composition

Implementation commits:

- `f9947c45c` shares the discovery UDP socket with pairing so endpoint-bound replies use the advertised source port; it also fixes cancellation propagation and makes persisted trust visible before `Completed` observers run.
- `94bc0d866` composes `PairingTrustRuntime`, `PairingService`, `PairingManager`, `TrustedDeviceStore`, and `PairingWizardModel` into the real `MainWindow`/`DevicesDock` startup path.

## Wired runtime path

1. `MainWindow` supplies the stable local `DeviceInfo` and active Deskflow TLS fingerprint to both `DeviceDiscoveryRuntime` and `PairingTrustRuntime`.
2. `DevicesDock::pairingRequested` starts the real network pairing manager through `PairingTrustRuntime`.
3. Discovery and pairing share the bounded discovery socket. Discovery advertisements continue to update `DiscoveryRegistry`; pairing CBOR envelopes are dispatched to `PairingService` and replies leave from the same UDP source port.
4. `PairingWizardModel` is bound to `IPairingService`; compare, six-digit submission, and cancel actions therefore operate on the network session rather than a GUI-only state machine.
5. Explicit confirmation atomically persists the peer SHA-256 fingerprint. The device card changes to trusted/online only after that durable commit.
6. Later discovery advertisements re-evaluate the saved pin. Matching identities become online; revoked or changed fingerprints become `TrustViolation` without exposing the advertised fingerprint as a trusted pin.
7. An unusable primary and backup trust store blocks pairing instead of silently replacing trust data.

## Verification

Windows Qt 6.7.3 / MinGW 13.1 probe:

```text
ctest --test-dir F:/github/DeskflowPlus-a6-pairing-composition-probe/build --output-on-failure
16/16 PASS, 0 failed, 2.61 s
```

The executable suite covers discovery codec/service/registry/settings/address selection, pairing state/message/manager/service/trust commit, trust persistence and TLS pinning, discovery runtime, service-backed pairing wizard actions, two real loopback UDP peers, wrong code, cancellation, expiry, revocation, fingerprint change, and corrupt trust storage.

## Remaining runtime boundary

- Trust revocation is durable and callable through `PairingTrustRuntime::revoke`, but no Devices Dock "forget/revoke" control invokes it yet.
- `AutoReconnectCoordinator` is not yet owned by the GUI startup path and does not consume the trusted device store.
- The independent file TLS listener/client and `IFileTransferService` are not yet composed; this slice does not claim file transfer runtime completion.
- A full Deskflow GUI link was `NOT_RUN` locally because this Windows session lacks the repository's OpenSSL 3 development package. The affected Qt libraries and executable composition harness compiled, and the canonical Windows/macOS Actions build remains the cross-platform link/package authority after A0 integration.
- Two packaged Windows/macOS peers, OS permission prompts, and real LAN pairing remain `NOT_RUN` until the A0 cross-platform acceptance run; loopback tests are not presented as that evidence.
