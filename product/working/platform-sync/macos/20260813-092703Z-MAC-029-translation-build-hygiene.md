# MAC-029: macOS 构建不再改写翻译源

- Message ID: `20260813-092703Z-MAC-029-translation-build-hygiene`
- Author: `A5-macOS`
- Target: `A0`
- Created UTC: `2026-08-13T09:27:03Z`
- Base product SHA: `479a0f78f3e267347f872a41141493d9f2e018d2`
- Platform branch: `agent/a5/macos-translation-build-hygiene`
- Commit/tag/run: feature `39e7d558e5605b998416d4296511f843b12e7334`; branch head `b3ec4dbefbedcccf7515bdbf6c2ef461baf01d05`; tree `e5837346933f50310fdbe4b5c4b80f08bee1180b`
- Status: `READY`
- Affected contracts: `none`
- Tests: Qt 6.11.1 Release build PASS; `app_translations` 10/10 `.qm` PASS and 9 tracked `.ts` unchanged; macOS Python 19/19 PASS; `I18NTests` / `RelayDeskI18NTests` / `RelayDeskTransferRuntimeCompositionTests` 3/3 PASS; staged App RelayDesk `.qm` + strict ad-hoc codesign PASS
- Blocker: `none`
- Requested action: merge the platform branch into `product/relaydesk-v1`
- In reply to: `N/A`

## Summary

Canonical macOS configure now sets `RELAYDESK_UPDATE_TRANSLATION_SOURCES=OFF`. Normal Release/package builds compile the reviewed `.ts` catalogs to `.qm` without running `lupdate` against tracked source files; maintainers can still explicitly enable the existing extraction path. This removes the reproducible thousands-line translation worktree pollution and reduces accidental cross-agent commits without changing RDFT or shared business interfaces.

The branch also merges the latest product COMP-006 history bridge and MainWindow transfer composition. The remote connector commit has the same final tree as the locally verified merge commit `3ec688db463e5b82081bf09a860942d01b9680cc`.

DMG/TEST-005 was not rerun for this translation-build-only slice; MAC-028 remains the latest full Qt 6.11 App/DMG lifecycle evidence. Developer ID, notarization and macOS permission authorization remain `NOT_RUN` and require final user acceptance only.
