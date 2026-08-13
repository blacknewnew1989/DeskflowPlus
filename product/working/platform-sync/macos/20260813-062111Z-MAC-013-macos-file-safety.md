# MAC-013 macOS 安全文件提交适配器

- 时间（UTC）：2026-08-13T06:21:11Z
- Owner：A5
- 状态：READY_FOR_INTEGRATION
- 产品基线：`0d091d301aea2140387fdd615150984dfed5bc08`
- 协议冻结标签：`relaydesk-protocol-v1-20260813-01`
- A5 分支：`agent/a5/macos-file-safety`
- 远端提交：`0a024b6a39e522b7147f3a001e7ac32a08e8ee42`
- 本机已测等价提交：`a2273ec3860402ab6406c43e101035fd8fdc74a5`
- 等价树：`d715c15a384bbadf3e3aa967f261561088d1ac2b`

## 变更

仅消费冻结后的 `IPlatformFileSafety`，未修改共享接口、wire header、MessageType、CBOR schema、flags 或稳定错误码。

新增 macOS 平台实现 `MacFileSafety`：

- 以 `lstat`、`open/openat(O_NOFOLLOW)`、`fstatat(AT_SYMLINK_NOFOLLOW)` 验证接收根目录和逐级路径；
- 拒绝根目录、父目录、staging、destination 的符号链接穿越及接收根目录外路径；
- staging 仅允许普通文件，destination 仅允许普通文件或不存在，并拒绝同 inode；
- `FailIfExists` 使用 macOS `renameatx_np(..., RENAME_EXCL)` 原子防覆盖；
- `ReplaceExisting` 使用 `renameat` 原子替换；
- 失败映射为冻结的 `FileSafetyError`，平台诊断字符串不作为稳定协议契约。

## 验证

- `RelayDeskMacFileSafetyTests`：8/8 PASS。
- 定向平台契约与适配器：2/2 PASS。
- 全量 CTest：86/86 PASS。
- `product/scripts/validate-package.py`：PASS（49 required files、7 JSON、60 protocol vectors）。
- `package-macos.sh`：PASS；ad-hoc App/DMG 与 3 个源码包生成。
- 自动 TEST-005：20/20 PASS；含 ZIP/DMG 自包含链接、`codesign --deep --strict`、`hdiutil verify`、隔离 clean install/launch、same-bundle upgrade/launch、app-only uninstall、用户数据保留。
- `SHA256SUMS.txt`：5/5 PASS。
- App ZIP SHA-256：`10aedb0e42bc0b67f3b1e9b43e6d0690a0500e5470eafc5b3669ccf8f47593a0`
- DMG SHA-256：`10d716cbcff0960fae2d7ba56a321b4060778eef2f44261bef252efca060a4ce`
- Bundle：`local.relaydesk.desktop`，版本 `1.26.0.219`，58 个 Mach-O。
- 冻结标签 Actions：[run 31672497950](https://github.com/blacknewnew1989/DeskflowPlus/actions/runs/31672497950) completed/success；Windows x64、macOS arm64、macOS 生命周期、开发材料校验、unsigned draft release 全部成功。

## 集成请求

- A0：以远端提交 `0a024b6a39e522b7147f3a001e7ac32a08e8ee42` 集成。
- A6/A0：后续将 `MacFileSafety` 注入 FileReceiver/生产组合；本提交不自行扩展共享接口。
- A4：无需 Windows 源码改动。

## NOT_RUN / 开放项

- `MacFileSafety` 尚未接入 FileReceiver/生产 composition（owner：A6/A0）。
- 跨文件系统移动语义未覆盖；冻结接口当前定义同一接收根目录内 staging→destination 原子提交。
- Accessibility、Input Monitoring、Local Network 系统授权 UI：NOT_RUN，需最终安装验收。
- Developer ID、notarization、真实 `/Applications` 首开与 Gatekeeper：NOT_RUN；本轮为明确标注的 ad-hoc 内部包并在 `/private/tmp` 隔离验证。
- 工作树原有 9 个翻译文件修改未触碰、未暂存、未提交。
