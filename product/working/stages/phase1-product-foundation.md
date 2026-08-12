# Phase 1 产品基础报告

- 当前结论：`IN_PROGRESS`（已修复候选测试与 CI 结果语义，等待 `-02` 标签复验）
- 功能基线提交：`ead6acbd56506b92e1b755471dd7a105845fd63f`
- 当前修复提交：`99c98f500`（含 `300a3c68a`）
- 当前集成 run：[31618176846](https://github.com/blacknewnew1989/DeskflowPlus/actions/runs/31618176846)
- 首次标签：`relaydesk-phase1-20260813-01`（run `31619248628`，保留失败证据）
- 目标复验标签：`relaydesk-phase1-20260813-02`
- 产物性质：unsigned 内部包；签名凭据不是构建、测试和打包前置条件。

## 已集成范围

| 范围 | 结果 | 主要证据 |
| --- | --- | --- |
| BRAND-001 | `PASS` | 名称、包标识和产物命名集中配置；`e9cb1121a`、`2f25cea3c` |
| I18N-001 | `PASS` | 上游中文补全、RelayDesk 中英文语义目录和测试；`a36ad2a91`、`f69555c6c`、`c097c2157` |
| DEV-001/002 | `PASS` | 持久 DeviceId/DeviceInfo 与 Deskflow TLS identity 复用；`2168f3941`、`acc06b567` |
| DISC-001..004 | `PASS` | 严格 discovery codec、多网卡 UDP、TTL registry、手动地址/异步候选解析；`f82ef0eac`、`68c423a63`、`7894979a0`、`a0ea42ba3` |
| PAIR-001..004 | `PASS` | 有界 SAS、消息交换、原子 trust store、TLS pinning、绑定 session/device/endpoint/fingerprint 的真实 UDP manager/service；`2b851fe28`、`9b67b2f13`、`46682deb7`、`c1c35bf14` |
| PAIR-005 | `PASS` | 可信设备候选排序、重连退避、指纹变化 hard-stop；`9da6930db` |
| UI-001..004 | `PASS` | 设备首页模型、配对向导、Devices Dock、权限状态契约与本地化提示；`e7890507a`、`05152e338`、`36004dda0`、`acc20f843` |

发现广播中的指纹不会被误当作已 pin 的信任指纹；配对完成前仍需显式确认。文件传输继续使用独立 TLS 连接、队列和缓冲区，不改写 Deskflow 已有键鼠/剪贴板通道。

## 本地验证

- Qt 6.7.3 / MinGW 13.1：当前共享 transfer probe 在整仓等价的 `QT_NO_KEYWORDS` 条件下重新配置、全量编译并完成 `15/15 PASS`。
- PairingStateMachine、PairingManager、PairingService 三个真实 Qt harness 均为 `1/1 PASS`，Service 包含 UDP loopback。
- Device/Discovery/Trust/Reconnect 各提交均在独立 Qt MinGW harness 编译并运行对应测试；候选解析包含真实 localhost 异步解析，discovery 包含真实 loopback。
- UI-001..004 的集成验证为 `6/6 PASS`；中英文目录 `111/111` 语义 key 一致。
- `git diff --check` 与受影响 C++ 的 clang-format 检查通过。

## CI 诊断

较早的 runs `31615988933`、`31616655023` 和 `31617153832` 在 Windows/macOS 同一位置失败：整仓定义 `QT_NO_KEYWORDS`，而 `ResumeStoreTests` 使用了 `private slots:`。独立 probe 起初未启用该定义，因此曾产生本地假绿。`1ff1a2a09` 将唯一违规点改为 `private Q_SLOTS:`；随后 probe 在等价定义下完成全量编译和 `15/15 PASS`。

候选 run `31618176846` 的 Windows x64 与 macOS arm64 均完成 configure、build、package、CTest 日志收集与 artifact 上传，但进一步审阅原始 CTest 日志发现两平台的 `TransferSenderTests` 均失败。旧 workflow 因 `continue-on-error` 保留诊断后没有恢复测试退出状态，导致 job conclusion 错误显示为 success；本报告不把该 run 记作测试 `PASS`。

首次标签 run `31619248628` 暴露了两个独立问题。Windows 在源码配置前下载 `vcpkg.exe` 时收到 WinHTTP `0x2F78`，属于依赖服务器瞬断；同一 SHA 的前一 run 已完成 Windows 全流程。macOS 完成构建和打包，但 `TransferSenderTests` 的同大小覆写用例依赖文件系统自动推进 mtime，APFS 可在同一时间粒度内保留原 mtime，因而未触发预期快照变化。`300a3c68a` 在两个 mutation 用例中显式设置不同 mtime，并在 Windows/MinGW 连续运行 20 次通过。`99c98f500` 保留 `continue-on-error` 以便始终上传诊断，同时在 artifact 上传后恢复 package/CTest 的真实失败状态，避免 job 假绿。失败的 `-01` 标签未删除或改写；修复由 `-02` 标签重新验证。

## 阶段完成条件

- [x] Phase 1 功能以独立小提交合入并推送 `product/relaydesk-v1`。
- [x] 当前可用本地 Qt 测试通过。
- [x] 创建并推送 `relaydesk-phase1-20260813-01`；失败证据保留且未改写。
- [ ] 创建并推送 `relaydesk-phase1-20260813-02`。
- [ ] 标签 run 的 Windows x64 与 macOS arm64 构建、CTest、打包均通过。
- [ ] 下载两平台 artifact，复算 SHA-256 并把结果写回本报告。

## NOT_RUN

- `NOT_RUN`：Windows 与 macOS 两台真机的发现、配对、地址变化自动重连。
- `NOT_RUN`：macOS Local Network、Accessibility、Input Monitoring 的真实系统授权。
- `NOT_RUN`：干净机器上的 MSI/便携包与 unsigned App/DMG 首次安装启动。
- `NOT_RUN`：正式 Windows/Apple 代码签名、公证及系统信誉验证。

这些项目保留给最终安装、系统权限授权和验收，不阻塞 unsigned 内部包继续生成。
