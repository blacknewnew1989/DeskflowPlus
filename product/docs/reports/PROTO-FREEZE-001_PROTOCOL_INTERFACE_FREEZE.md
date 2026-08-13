# PROTO-FREEZE-001 protocol and shared-interface freeze

Status: `CANDIDATE` until the protocol tag and its Windows/macOS Actions artifacts are recorded in
`product/docs/19_PROTOCOL_V1_FREEZE.md`.

## Candidate identity

- Integration branch: `product/relaydesk-v1`
- Candidate implementation through: `bb4bdc4ac7e25a046a6a6415c507501ba765efdf`
- Final documentation commit: pending
- Freeze tag: pending
- Canonical Actions run and artifact digests: pending

## Frozen candidate surface

- RDFT/1 fixed envelope: 32-byte big-endian header.
- Message registry: 24/24 implemented message types from one registry definition.
- Shared vectors: 60 total — 24 positive full frames, 25 metadata-negative vectors, and 11
  frame-negative vectors.
- Every positive vector is decoded into its typed message, canonically re-encoded, and compared to
  metadata and full-frame bytes.
- Stable wire errors: `AuthResultErrorCode`, `ProtocolErrorCode`, catalog-derived ERROR retryability,
  exact header/metadata agreement.
- Stable pairing and transfer errors: `PairingFailureReason`, `TransferErrorCode`, typed control
  outcomes, schema-v2 history, and safe schema-v1 history migration.
- Strong non-null `DeviceId`, `TransferId`, and `FileId` values.
- One typed pairing facade and one `IFileTransferService` UI/service bridge; public UI headers expose no
  raw `Frame`, `TransferAccept`, or `TransferReject` business bypass.
- Real queued-QThread coverage for all public transfer values and `NegotiatedCapabilities`.
- Directional capability truth: `file.v1` is the base protocol; only `file.receive.v1` authorizes an
  incoming offer. The current uncomposed receiver does not advertise it and keeps discovery disabled.
- One bounded sender/sink/backpressure boundary and typed conflict-to-platform commit disposition.

## Candidate verification

- `python product/scripts/validate-package.py`: PASS, 49 required files, 7 JSON files, 60 protocol
  vectors.
- Qt 6.7.3 / MinGW 13.1 isolated build: PASS for the shared RelayDesk libraries and affected tests.
- `RelayDeskSharedInterfaceFreezeTests`, `RelayDeskFileTransferRuntimeTests`,
  `RelayDeskCapabilityCodecTests`: 3/3 PASS after directional capability freeze.
- `RelayDeskSharedInterfaceFreezeTests`, `RelayDeskConflictResolverTests`: 2/2 PASS after putting the
  MinGW 13.1 runtime before the older Qt-bundled MinGW runtime.
- Transfer failure/history/UI targeted suite: 8/8 PASS.
- `git diff --check`: PASS.

The first conflict-test launch returned Windows `0xc0000139` before Qt test initialization. Root-cause
evidence showed Qt's `mingw_64/bin` contained an older 2021 `libstdc++-6.dll`; the test executable was
built by MinGW 13.1 and imported newer `std::future/thread` entry points. Reordering PATH to place the
MinGW 13.1 runtime first made the same executable and all tests pass. No product-code workaround was
added.

## Explicit non-blocking implementation state

- `NOT_WIRED`: product MainWindow ownership of file runtime/UI runtime, incoming receiver/resume/
  conflict/history/progress composition, auto-reconnect, and Windows permission UI composition.
- `NOT_IMPLEMENTED`: Windows/macOS `IPlatformFileSafety` adapters.
- `NOT_RUN`: real Windows-to-macOS and macOS-to-Windows transfer, sleep/network-change recovery,
  platform privacy prompts, and signed/notarized distribution.

These items remain Phase 3/4 implementation or final acceptance work. They do not authorize either
platform to invent a different protocol or shared interface.
