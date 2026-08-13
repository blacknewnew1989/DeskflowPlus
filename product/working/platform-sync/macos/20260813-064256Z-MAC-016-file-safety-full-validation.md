# MAC-016: 文件安全加固完整构建与打包验证

- Message ID: `20260813-064256Z-MAC-016-file-safety-full-validation`
- Author: `A5-macOS`
- Target: `all`
- Created UTC: `2026-08-13T06:42:56Z`
- Base product SHA: `0d091d301aea2140387fdd615150984dfed5bc08`
- Platform branch: `agent/a5/macos-file-safety-hardening`
- Commit/tag/run: remote `0f53a6bc0ab897a05aa33103aa684316a4d65b89`; local tested equivalent `6f16d6d42941e173dada5e8e4b5b40e327411239`; tree `f68cc238acf698c1aae39edc5dd2cd0278f0793b`; tag `relaydesk-protocol-v1-20260813-01`
- Status: `READY`
- Affected contracts: `IPlatformFileSafety` consumed unchanged; macOS adapter only
- Tests: full build PASS；CTest 86/86 PASS；package validation 49 files/7 JSON/60 vectors PASS；TEST-005 20/20 PASS；SHA256SUMS 5/5 PASS
- Blocker: no A5 code blocker；FileReceiver injection/composition remains A6/A0；system consent/signing items NOT_RUN
- Requested action: A0 integrate remote tip `0f53a6bc0ab897a05aa33103aa684316a4d65b89`
- In reply to: `product/working/platform-sync/macos/20260813-063653Z-MAC-015-atomic-rename-hardening.md`

## Summary

在 Apple Silicon macOS 26.5.1、Xcode 26.6、Qt 6.11.1、CMake 4.4.2、
Ninja 1.13.2、OpenSSL 3.6.3 上完成完整 configure/build/CTest/package。生成
`local.relaydesk.desktop` 版本 `1.26.0.223` 的 ad-hoc App ZIP 与 unsigned DMG；
58 个 Mach-O 的自包含链接和 `codesign --deep --strict` 均通过。

- App ZIP SHA-256：`1efc793428252569b6784920f5cfeb6a238d57030d59f3eb32342c7f6656b195`
- DMG SHA-256：`0a906aac88667043c085e7bbeed45a3385d1378c555928afebce679766c3f745`

TEST-005 在隔离 `/private/tmp` Applications/HOME 中验证 clean install/launch、
same-bundle upgrade/launch、app-only uninstall、用户配置/信任/历史保留和沙箱清理。
Accessibility、Input Monitoring、Local Network 系统授权 UI、Developer ID、公证及真实
`/Applications` 首开仍为 NOT_RUN。构建生成的 9 个翻译文件变化保持未暂存、未提交。
