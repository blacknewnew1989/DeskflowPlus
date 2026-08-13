# PROTO-FREEZE-001 protocol and shared-interface freeze

Status: `PASS`; the immutable protocol tag and its Windows/macOS Actions artifacts are recorded in
`product/docs/19_PROTOCOL_V1_FREEZE.md`.

## Candidate identity

- Integration branch: `product/relaydesk-v1`
- Authoritative commit: `0d091d301aea2140387fdd615150984dfed5bc08`
- Freeze tag: `relaydesk-protocol-v1-20260813-01`
- Tag object: `0ef027c003afa9e4b159e4c3687a46e7e0860f1c`
- Canonical Actions run: `31672497950` — SUCCESS
- Windows artifact: ID `9170492840`, API/local ZIP SHA-256
  `bf435935c748bc57ea1e7f5913a01dc47467bcaec61040068e630c6d7b54b5d0`
- macOS artifact: ID `9170386546`, API ZIP SHA-256
  `04ba64d9ebd49c4655871fc29005fbbc37d641b19bc2029439baa447a0567887`

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

## Canonical tag-run evidence

- Windows job `94359914984`: SUCCESS; 84/84 CTest PASS; package and artifact collection PASS;
  real unsigned MSI clean install, repair, major upgrade, uninstall, registration, service,
  firewall and residue lifecycle PASS.
- macOS job `94359914873`: SUCCESS; 85/85 CTest PASS; deployed App reports `valid on disk` and
  `satisfies its Designated Requirement`; App/DMG packaging and artifact collection PASS.
- macOS lifecycle job `94362205393`: SUCCESS; artifact ID `9170501281`, ZIP SHA-256
  `fee1e9acf631a6f0e3dcc1b81394cc69f799a9d7b210ea54a8934f6cb058a5e9`.
- Windows artifact contents were downloaded and rehashed locally. The unsigned MSI SHA-256 is
  `0d6d859c296e71d9dddb3b5d78a3873104633c32da05f046bc48f7958d0b82ec`; the portable 7Z SHA-256 is
  `ff8597fbbc181b60a25a3f596427c946aadb441d4b1cbf5147cbeb79e70cf14d`.
- The draft internal Release for the same tag contains the ad-hoc App ZIP SHA-256
  `3b2ca7be8206325e373f29c9ed471ae2eedd821b95ce46081574199e74af5b70` and DMG SHA-256
  `08e11b4d6f3cf30b9600b5499558dca92cf4cd55366c3a9a460cf8c3dcb71a66`.

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
