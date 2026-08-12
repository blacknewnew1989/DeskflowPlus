# TEST-004 Interruption and Recovery Matrix

## Execution record

| Field | Value |
|---|---|
| Test implementation commit | `672ba27ef102783119b19d3b93aeabc2d484ec5a` |
| Integration baseline | `d2cb3f780805cdc5c43cad6aee35408de1d43e0a` |
| Date | 2026-08-13 |
| Platform | Windows 11 x64 |
| Toolchain | Qt 6.7.3, MinGW 13.1.0, CMake 3.30.5 |
| Operator | Codex A6 |
| Test target | `RelayDeskTransferRecoveryMatrixTests` |
| Command | `ctest --test-dir F:/github/DeskflowPlus-a6-sender-build/build -R ^RelayDeskTransferRecoveryMatrixTests$ --output-on-failure` |
| Result | **PASS** — 9 passed, 0 failed, 0 skipped |

The automated target uses the real `TransferSender`, `FrameCodec`, `FileMessageCodec`,
`FileReceiver`, `ResumeStore`, `ResumeNegotiator`, `TransferControlStateMachine`,
`ManifestBuilder`, and `PathPolicy`. It does not introduce a second test protocol.

For interrupted cases, a sender frame is encoded to RDFT bytes, only half of the
frame is delivered, `NeedMoreData` is verified, and the connection buffer is then
discarded. Both sender and receiver objects are destroyed. The restarted pair uses
the persisted durable checkpoint and negotiated offset; the discarded frame is
replayed with sequence zero, the receiver rehashes its `.part` prefix, and final
size plus SHA-256 are verified before atomic commit.

## Deterministic component matrix

| ID | Combination | Expected | Actual | Result |
|---|---|---|---|---|
| TEST-004-C01 | 0 B + half `FILE_END` lost + sender/receiver restart | resume offset 0; commit empty file | offset 0 negotiated; empty target committed | PASS |
| TEST-004-C02 | 10% durable + next frame lost + process restart | resume from durable offset, not zero | resumed at 64/640 bytes; SHA-256 matched | PASS |
| TEST-004-C03 | 90% durable + next frame lost + process restart | resume near end without replaying prefix | resumed at 576/640 bytes; SHA-256 matched | PASS |
| TEST-004-C04 | active transfer + pause + 100 scheduler ticks + resume | no sender production or receiver growth while paused | byte counters stayed fixed; resumed and committed | PASS |
| TEST-004-C05 | active transfer + cancel + delete partial | terminal cancel; `.part` and state removed | repeated cancel remained idempotent | PASS |
| TEST-004-C06 | active transfer + cancel + keep partial | terminal cancel; `.part` and state retained | repeated cancel remained idempotent | PASS |
| TEST-004-C07 | Unicode folder + nested file + zero-byte file + empty directory + restart | stable manifest; every file recovers; empty directory preserved | two files committed with matching content; empty directory exists | PASS |

## Platform interruption matrix

These rows require operating-system or physical network control. A deterministic
component result is not reported as a substitute for the real platform scenario.

| Test ID | Platform/steps | Expected | Actual | Result |
|---|---|---|---|---|
| RESUME-005 | Windows/macOS peer changes IP during transfer | reconnect by device ID and resume at durable offset | no controlled second peer or DHCP environment in this Windows session | NOT_RUN |
| RESUME-006 | Win↔Mac transfer switches Wi-Fi to Ethernet | reconnect and resume without corrupt commit | no controllable dual-interface Win↔Mac lab in this session | NOT_RUN |
| RESUME-007 | sender and receiver sleep/wake during transfer | reconnect, renegotiate offset, and resume | physical sleep/wake was not exercised by this component run | NOT_RUN |
| TEST-004-WM | Windows sender to Apple Silicon macOS receiver | interruption matrix passes with real TLS sockets and filesystems | macOS peer unavailable in the current session | NOT_RUN |
| TEST-004-MW | Apple Silicon macOS sender to Windows receiver | interruption matrix passes with real TLS sockets and filesystems | macOS peer unavailable in the current session | NOT_RUN |

## Handoff

- Windows and macOS CI should run `RelayDeskTransferRecoveryMatrixTests` at the
  same integration SHA.
- A7 should retain the component PASS evidence and execute the five `NOT_RUN`
  rows only when a controlled Win↔Mac hardware/network environment is available.
- Real platform execution must record the integration commit, exact network or
  sleep steps, logs, artifact paths, and PASS/FAIL without reclassifying this
  component simulation as physical E2E coverage.
