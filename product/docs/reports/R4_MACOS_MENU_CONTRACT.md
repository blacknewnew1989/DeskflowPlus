# R4 macOS Menu Bar 与生命周期自动契约

## 范围

本报告只记录 `agent/a0/redevelop-p0@38247729b3916ecc0c21d39a2fef8e85fab3dda4` 的 hosted
macOS 自动证据。它不是物理 macOS menu bar 或发布验收。

## Hosted 回执

- GitHub Actions run [`33464083567`](https://github.com/blacknewnew1989/DeskflowPlus/actions/runs/33464083567)
  终态为 `success`。
- macos-14 package job
  [`99720205727`](https://github.com/blacknewnew1989/DeskflowPlus/actions/runs/33464083567/job/99720205727)
  成功：CTest 102/102，耗时 276.98 s；`MainWindowQuitRegression-menu` 为 #14、0.23 s，
  `MainWindowQuitRegression-tray` 为 #15、0.21 s。两项使用 `QT_QPA_PLATFORM=offscreen`，验证
  MainWindow 生产 QAction 的 menu/tray Quit 路由与退出，不声称菜单栏图标被 macOS 显示或被点击。
- package job 完成 staged app 的 strict ad-hoc codesign 验证及七语言 translation bundle 校验。
  macOS artifact `9784406288`：
  `relaydesk-macos-arm64-38247729b3916ecc0c21d39a2fef8e85fab3dda4`，66,338,026 bytes，
  `sha256:031a5f73b794b57f5e4328433faaa396e33bec0ede6fcb198f2fbbe90f483687`。

## 隔离安装生命周期

- macOS install lifecycle job
  [`99723079671`](https://github.com/blacknewnew1989/DeskflowPlus/actions/runs/33464083567/job/99723079671)
  成功，下载 package artifact 时确认 SHA-256 与 `9784406288` 的 API digest 一致。
- lifecycle evidence artifact `9784543016`：
  `relaydesk-macos-install-regression-38247729b3916ecc0c21d39a2fef8e85fab3dda4`，12,591 bytes，
  `sha256:536634d4ce19f9e0e4785b9029145ec5efeec217928ceb5d9aeeef9ac2dfe935`。
- TEST-005 在 runner temp 的隔离 Home/Applications 中通过：artifact manifest and SHA-256、
  staged translation、App ZIP/DMG structure、App ZIP/DMG codesign、translation resource equality、
  DMG verify/attach、clean install and launch、same-bundle upgrade and launch、app-only uninstall、
  external config/user data preservation、DMG detach 和 sandbox cleanup。
- 包内摘要：App ZIP
  `c92ddc26f3b7fa1225be738dbea7c141e62916efd8632543ebe2c6cb1796acd1`；DMG
  `a557283e8581e7e701f195148611e2cb3b6d4bc0a8a33ca467c2acb988bde386`。

## 状态与边界

`R4-UI-010B` 为 `PASS`，范围严格限定为 hosted macOS 的当前 SHA build/CTest、offscreen
MainWindow menu/tray Quit QAction contract、ad-hoc bundle 及隔离 install lifecycle。`R4-UI-010`
保持 `IN_PROGRESS`。

下列项目仍为 `NOT_RUN`：物理 menu bar 图标、点击和 Show/Hide；TCC 与 System Settings 往返；
Dock/Finder；人工安装；Developer ID 与 notarization；物理 Windows↔macOS；正式发布。单一
workflow 的 Windows matrix 成功仅作附带构建回执，不扩展本报告的 macOS UI 结论。
