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
