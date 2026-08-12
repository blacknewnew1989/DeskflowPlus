# Phase 0 双平台 CI 与内部包报告

- 结论：`PASS`
- 集成提交：`808a3307b07422e7ea8c60af46148ce68af13649`
- 集成 run：[31602376403](https://github.com/blacknewnew1989/DeskflowPlus/actions/runs/31602376403)
- 阶段标签：`relaydesk-phase0-20260812-01`
- 标签复验 run：[31602699800](https://github.com/blacknewnew1989/DeskflowPlus/actions/runs/31602699800)，双平台 `PASS`
- 产物性质：unsigned 内部包；缺少正式签名凭据未阻塞构建、测试或打包。

## 构建与测试

| 平台 | Runner / 工具链 | 构建与打包 | CTest |
| --- | --- | --- | --- |
| Windows x64 | `windows-2022`；MSVC 2022；Windows SDK 10.0.26100；Qt 6.10.1 | `PASS` | `27/27 PASS`，1.19 秒 |
| macOS arm64 | `macos-15`；Xcode 16.4；macOS SDK 15.5；Qt 6.10.1 | `PASS` | `28/28 PASS`，3.68 秒 |
| 开发资料诊断 | `ubuntu-latest` | `PASS` | 不适用 |

测试从真实注册目录 `build/src/unittests` 执行。Windows 的 `RelayDeskManifestBuilderTests` 与 macOS 同名测试均已通过，未再出现仅 workflow 绿色但未发现测试的假阳性。

## 已下载产物

本地下载根目录：`F:\github\DeskflowPlus-a7-phase0-ci\dist\actions\31602376403`

GitHub artifact ZIP：

| 平台 | Artifact | ID | 大小 | ZIP SHA-256 |
| --- | --- | ---: | ---: | --- |
| Windows x64 | `relaydesk-windows-x64-808a3307b07422e7ea8c60af46148ce68af13649` | 9143920988 | 32,046,359 | `99abc150ebbab36294f2771034d6aad6a2914f167de615276be932b1e6970d74` |
| macOS arm64 | `relaydesk-macos-arm64-808a3307b07422e7ea8c60af46148ce68af13649` | 9143801915 | 38,235,413 | `12e9763ec66d89ad73691db522e916abe019f6ca5fa68f634685c5973e895b45` |

交付文件：

| 文件 | 字节 | SHA-256 |
| --- | ---: | --- |
| `deskflow-relaydesk-808a3307b07422e7ea8c60af46148ce68af13649-win-x64-portable.7z` | 12,851,423 | `208e93bee5f1080cd8e43369a87fcee1fa18c097e50f4d5e5fa5a4d62c830722` |
| `deskflow-relaydesk-808a3307b07422e7ea8c60af46148ce68af13649-win-x64.msi` | 15,784,493 | `a2325ce33ae9f9e83e6f22392c7de40435d1c9807be63cf5d1dad7fc74389fb1` |
| `RelayDesk-macos-arm64-unsigned-808a3307.app.zip` | 6,085,110 | `7a0655c8e68ef8a290d0e73dd99b4bf664117afb87f8db6fd102525f2275c9a0` |
| `deskflow-relaydesk-808a3307b07422e7ea8c60af46148ce68af13649-macos-arm64.dmg` | 28,372,516 | `0e068a53af783bc7030c941f93950e8ea5e704fc45bef866c96a4a0221b7b812` |

源码包及其摘要记录在 `product/working/actions/31602376403.json` 和两个下载 artifact 内的 `SHA256SUMS.txt`。所有 ZIP 与文件摘要均在下载后本地重新计算，并与 GitHub artifact digest / 内嵌 manifest 一致。

## CI 修复记录

A7 分支上的六个独立修复均已推送，并由 A0 挑选到 `product/relaydesk-v1`：

1. `c1628dc06`：在 macOS 配置前解析 SDK，避免空 `--sysroot`。
2. `2d79dccd4`：保留 Windows vcpkg toolchain 路径，避免 Git Bash 改写盘符反斜杠。
3. `d02d480df`：串行执行 binary/source package target。
4. `bdcce2331`：`run-github-actions.py` 从 `origin` 解析仓库并统一传入 `gh -R`，避免误用 upstream。
5. `1412b0ade`：打包失败后仍保留独立测试诊断。
6. `64015e1a7`：源码包排除 `.git`、in-tree `build`、`dist`、工具/缓存/vcpkg/tmp 目录，消除 CPack 递归复制活动构建树。

集成分支后续还修复了真实 CTest 目录、最终 CPack 产物收集以及跨平台测试时间戳处理。最后一个 Windows 根因是测试辅助函数以只读句柄调用 `setFileTime`；提交 `808a3307b` 在 writer 已关闭后改用可写句柄，最终 Windows 与 macOS 全部测试通过。

早期 bootstrap run `31593498656` 的失败证据已核实：macOS SDK 为空导致链接器找不到 `c++`；Windows vcpkg toolchain 路径被 shell 改写。后续 run 还暴露了 in-tree source package 递归、CTest 目录错误和 artifact collector 遍历 `_CPack_Packages` 等问题，均在最终候选前修复。

## NOT_RUN

以下必须依赖真实双机或系统授权的项目没有伪造为 PASS：

- `NOT_RUN`：Windows↔macOS 双向键盘、鼠标、滚轮和文本剪贴板真机联调。
- `NOT_RUN`：macOS Accessibility、Input Monitoring、Local Network 权限授权流程。
- `NOT_RUN`：干净机器实际安装/首次启动 Windows MSI/便携包和 macOS unsigned App/DMG。
- `NOT_RUN`：正式 Windows/Apple 签名、公证及 SmartScreen/Gatekeeper 信誉验证。

便携包、MSI、App ZIP 与 DMG 已生成且可供最终安装、系统权限授权和验收。
