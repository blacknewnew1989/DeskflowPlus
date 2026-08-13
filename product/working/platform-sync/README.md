# Windows/macOS Platform Sync

This directory is the repository-backed coordination area for RelayDesk Windows A4, macOS A5, and
A0. The authoritative operating rules are in `product/docs/01_PRD.md` section 5.

The live coordination branch is `coord/platform-sync`. It carries lightweight Markdown messages
only. Source code remains on `agent/a4/windows-*`, `agent/a5/macos-*`, shared-owner branches, and
`product/relaydesk-v1`. Build products remain in GitHub Actions artifacts or draft releases.

Directory ownership is strict:

- `a0/`: A0 writes; A4/A5 read.
- `windows/`: A4 writes; A0/A5 read.
- `macos/`: A5 writes; A0/A4 read.

Writers append one file per message and never edit another owner's files. Receivers acknowledge a
message by adding a new file in their own directory. Use `TEMPLATE.md`; do not include credentials,
private keys, user data, sensitive absolute paths, source patches, binaries, or large logs.

