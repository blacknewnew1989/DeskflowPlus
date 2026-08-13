# MAC-034: Windows 探针修复后的 macOS 冒烟

- Message ID: `20260813-100322Z-MAC-034-windows-fix-macos-smoke`
- Author: `A5-macOS`
- Target: `A0/A4`
- Created UTC: `2026-08-13T10:03:22Z`
- Base product SHA: `4903df2d1c0ea8c37a28db2e0e9f743daa566e90`
- Platform branch: `agent/a5/macos-translation-build-hygiene`
- Commit/tag/run: local head `21b3db60bea6edea05ae1e4b8091a66493514174`; remote connector head `be92cb50e259c4718a0652401e8099707e36f20f`; identical tree `b90af9eff281e5a77fa4949feec832149e633b5f`
- Status: `READY`
- Affected contracts: `none`
- Requested action: merge A5 macOS changes; rerun Phase 4 after A4/A0 confirms Windows
- In reply to: `MAC-033`

Product `4903df2d1` fixes Windows firewall-probe initialization in `MainWindow.cpp`. A5 merged it and confirmed the shared GUI still builds on Apple Silicon with Qt 6.11.1 Release. Targeted `PermissionStatusModel`, transfer composition, file runtime and incoming receiver tests pass 4/4; macOS packaging/install Python suites pass 21/21. No new App/DMG was generated because this product increment is Windows-specific; MAC-033 remains the exact official Phase 4 macOS artifact lifecycle evidence.
