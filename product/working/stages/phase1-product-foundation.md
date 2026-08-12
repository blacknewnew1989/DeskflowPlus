# Phase 1 产品基础报告

- 当前结论：`PASS`
- 功能基线提交：`ead6acbd56506b92e1b755471dd7a105845fd63f`
- 当前修复提交：`d2cb3f780`（含 `300a3c68a`、`99c98f500`、`d789fb0a6`、`0e8f6b416`）
- 当前集成 run：[31618176846](https://github.com/blacknewnew1989/DeskflowPlus/actions/runs/31618176846)
- 首次标签：`relaydesk-phase1-20260813-01`（run `31619248628`，保留失败证据）
- 第二次标签：`relaydesk-phase1-20260813-02`（run `31620547696`，保留失败证据）
- 通过标签：`relaydesk-phase1-20260813-03`（run `31621226862`）
- Release 资产复验标签：`relaydesk-phase1-20260813-04`（run `31623677270`，PASS）
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

第二次标签 run `31620547696` 中 macOS 完成 `61/61 PASS`、App/DMG/source 打包与 artifact 上传，证明 mutation 测试修复和 CI 失败恢复语义有效。Windows 再次在同一 `vcpkg.exe` URL 返回 WinHTTP `0x2F78`，仍未进入源码配置。`d789fb0a6` 因而为整个依赖安装增加恰好一次自动重试；成功的首次尝试不会重复执行，连续失败仍会让 job 失败。失败的 `-02` 标签同样保留且不改写。

第三次标签 run `31621226862` 完整通过。Windows 首次依赖安装成功且自动跳过重试，完成 CMake/MSVC 构建、CTest `60/60 PASS`、MSI/7Z/source 打包与上传；macOS arm64 完成构建、CTest `61/61 PASS`、App/DMG/source 打包与上传。Windows artifact ID `9151621850`，GitHub ZIP digest `eb0c8e10e9dc1c0ccfd11b9902df868b85cadceae6892229991953156371efbc`；macOS artifact ID `9151451146`，GitHub ZIP digest `0e736638bd4d930cef282e6883ad598dc211aeb6fea64b0c5b23e676c519344e`。

当前网络到 GitHub Actions Azure Blob 下载端点持续停留在 0 bytes；未把该外部传输问题误记为产物失败，也未交给用户处理。`0e8f6b416` 在唯一 `relaydesk-build.yml` 内增加 tag-only 草稿 Release 发布，`d2cb3f780` 显式设置 `GH_REPO=${{ github.repository }}`，将同一批已验证平台包复制为稳定 Release assets。`-04` 用于验证该下载通路并在本机复算每个交付包 SHA-256。

`-04` run `31623677270` 的 Development materials、Windows x64、macOS arm64 和 `Publish unsigned draft release` 四个 job 全部成功。Windows 再次为 `60/60 PASS`，macOS 为 `61/61 PASS`；草稿 Release 已包含 MSI、Windows portable 7Z、macOS App ZIP、DMG、源码包和三份摘要清单。

四个交付二进制已通过 authenticated Release Asset API 自动下载到 `F:\github\DeskflowPlus-relaydesk\dist\releases\relaydesk-phase1-20260813-04`。本地复算值与 Release API digest、聚合 `SHA256SUMS.txt` 三方一致：

| 交付文件 | Bytes | SHA-256 |
| --- | ---: | --- |
| `RelayDesk-macos-arm64-unsigned-2fe393ef.app.zip` | 6,218,904 | `21b2a727ff94a4bee5102baf843883e3bf961e59c787be4dffb7af805e4a05d8` |
| `relaydesk-2fe393ef298721f469dc3932c5f9f999bf13df56-macos-arm64.dmg` | 28,480,152 | `65d35578cad0008bd06bb55731ec12900e15a154a16b5ed53112ba44a03a58a8` |
| `relaydesk-2fe393ef298721f469dc3932c5f9f999bf13df56-win-x64.msi` | 15,854,400 | `c365ff5de2c6d50bede407aa43a850e7b591a242c5bb2f8f20627dba371d31fd` |
| `relaydesk-2fe393ef298721f469dc3932c5f9f999bf13df56-win-x64-portable.7z` | 12,909,493 | `bf1814a949d8aed6b3e91ad313acf4cdc21797a1235fc2e0c92ffe72fa9032b9` |

## 阶段完成条件

- [x] Phase 1 功能以独立小提交合入并推送 `product/relaydesk-v1`。
- [x] 当前可用本地 Qt 测试通过。
- [x] 创建并推送 `relaydesk-phase1-20260813-01`；失败证据保留且未改写。
- [x] 创建并推送 `relaydesk-phase1-20260813-02`；macOS PASS、Windows 依赖下载失败的证据保留。
- [x] 创建并推送 `relaydesk-phase1-20260813-03`。
- [x] 标签 run 的 Windows x64 与 macOS arm64 构建、CTest、打包均通过。
- [x] 创建并推送 `relaydesk-phase1-20260813-04`，验证草稿 Release job。
- [x] 下载两平台 Release assets，复算 SHA-256 并把结果写回本报告。

## NOT_RUN

- `NOT_RUN`：Windows 与 macOS 两台真机的发现、配对、地址变化自动重连。
- `NOT_RUN`：macOS Local Network、Accessibility、Input Monitoring 的真实系统授权。
- `NOT_RUN`：干净机器上的 MSI/便携包与 unsigned App/DMG 首次安装启动。
- `NOT_RUN`：正式 Windows/Apple 代码签名、公证及系统信誉验证。

这些项目保留给最终安装、系统权限授权和验收，不阻塞 unsigned 内部包继续生成。
