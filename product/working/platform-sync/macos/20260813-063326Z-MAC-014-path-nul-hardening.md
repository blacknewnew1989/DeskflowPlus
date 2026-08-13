# MAC-014: 拒绝 macOS 路径内嵌空字符

- Message ID: `20260813-063326Z-MAC-014-path-nul-hardening`
- Author: `A5-macOS`
- Target: `all`
- Created UTC: `2026-08-13T06:33:26Z`
- Base product SHA: `0d091d301aea2140387fdd615150984dfed5bc08`
- Platform branch: `agent/a5/macos-file-safety-hardening`
- Commit/tag/run: remote `4421f06f24bbb28bd25a0b0b9b462c35a33ba178`; local tested equivalent `e1210dd84fe33f2d4911fdd8d4437e9354ce8b06`; tree `dae3ae8fb4c122b87044fcce62df20d6682aff1a`; tag `relaydesk-protocol-v1-20260813-01`
- Status: `READY`
- Affected contracts: `IPlatformFileSafety` consumed unchanged; macOS adapter only
- Tests: `RelayDeskMacFileSafetyTests` 9/9 PASS；targeted CTest 1/1 PASS；`git diff --check` PASS
- Blocker: none for this slice；FileReceiver production wiring remains A6/A0
- Requested action: A0 integrate `4421f06f24bbb28bd25a0b0b9b462c35a33ba178` after MAC-013 commit `0a024b6a39e522b7147f3a001e7ac32a08e8ee42`
- In reply to: `product/working/platform-sync/macos/20260813-062111Z-MAC-013-macos-file-safety.md`

## Summary

POSIX filesystem calls terminate paths at the first NUL byte, while the frozen request structs carry
`QString`. The previous adapter could therefore inspect or rename the truncated path rather than the
requested path. The macOS adapter now rejects embedded NUL in receive root, traversal candidates,
staging paths and destination paths before any encoding or syscall. Regression tests prove staging is
not moved and no truncated destination is created. No shared type, wire schema, error enum or codec
changed.
