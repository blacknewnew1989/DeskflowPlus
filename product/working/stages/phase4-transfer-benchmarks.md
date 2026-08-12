# Phase 4 transfer benchmark handoff

## TEST-002 — 10 GiB bounded-memory sender

- Result: PASS on Windows x64, Qt 6.7.3 + MinGW 13.1.0.
- Command: `ctest -R RelayDeskLargeFileBoundedBenchmark --output-on-failure`.
- Method: create a 10 GiB logical sparse/extended `QFile`, run the real
  `TransferSenderPump` with a 1 MiB chunk, 4 MiB high water, and 2 MiB low
  water, then stop when backpressure is reached.
- Measurement: the benchmark prints logical size, bytes read before pause,
  largest frame, peak sink queue, retained pending frame, and the combined
  owned-buffer upper bound.
- Scope: this verifies logical-size independence and bounded sender/queue
  memory. It deliberately does not read all 10 GiB and is not a disk or
  network throughput result. Full 10 GiB SHA-256/E2E remains a platform test.
- Gate policy: informational benchmark only; no performance or coverage
  threshold was added.

Cross-platform handoff: run the same CTest target on macOS arm64. Physical
Win↔Mac 10 GiB transfer is `NOT_RUN` in this local Windows benchmark.

## TEST-003 — input/control priority under transfer pressure

- Result: PASS for the deterministic core harness on Windows x64, Qt 6.7.3 +
  MinGW 13.1.0.
- Command: `ctest -R RelayDeskInputPriorityUnderTransferBenchmark --output-on-failure`.
- Method: preload a deterministic scheduler with 128 real
  `TransferSender::nextFrame` worker steps over a 256 MiB logical source.
  Inject one input/control task before every worker dispatch. The scheduler
  always consumes the input queue first and records dispatch latency in ticks.
- I/O boundary probe: only `workerStep()` owns and calls `nextFrame`; the
  modeled network callback consumes already-produced `Frame` values. The test
  snapshots `bytesProduced()` across every callback and fails if it changes,
  demonstrating that the callback path does not read or hash source data.
- Scope: this is a deterministic scheduling/ownership validation, not physical
  Deskflow input injection. Windows↔macOS mouse/keyboard latency, dropped or
  stuck keys, and subjective latency remain `NOT_RUN` and require the platform
  TEST-003/E2E matrix.
- Gate policy: measurements are reported but no absolute latency gate,
  required check, or coverage threshold was added.

Cross-platform handoff: run both benchmark CTest targets on macOS arm64, then
record physical Win↔Mac input p50/p95/p99 separately without reclassifying this
core harness as a true input E2E result.
