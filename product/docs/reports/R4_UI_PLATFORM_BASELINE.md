# R4 UI / 平台服务基线

## 1. 基线与规则

- 审计基线：`agent/a0/redevelop-p0@2c0328f069f878f54dc36575f1c32422b04790fb`；
- 产品分支仍为 `product/relaydesk-v1@c544dc76fb4f29aefb6ef30c8acc4475b6778e07`；
- 旧 UI/Phase 报告只用于定位源码和候选测试，不继承状态；
- run `33385968319` 属于 `043d6b3fb`，只证明该 SHA 的 hosted 构建与既有测试，不证明
  `2c0328f` 的原生 UI、Explorer/Finder 拖放、系统权限或物理交互；
- 当前状态只依据 production 调用链和当前 SHA 已实际执行的证据。没有当前运行证据的项目为
  `NOT_RUN`；源码可确定丢失用户反馈的项目为 `FAIL`。

A3 曾尝试重建最小 UI 目标，但未加载有效 MSVC STL 环境，编译在 Qt header 报 `utility` 缺失。
该结果是工具链 `NOT_RUN`，不是源码测试失败，也不用于给任何项目标 PASS。

## 2. Production 调用链矩阵

| ID | 表面 | UI intent | Production service / typed boundary | 失败反馈 | 状态 |
|---|---|---|---|---|---|
| R4-UI-001 | 设备卡、信任、手动地址 | DevicesDock pairing/revoke/autoAccept/manual save | MainWindow -> PairingTrustRuntime / DiscoverySettingsStore / DeviceDiscoveryRuntime | 001A/001B/001C 均已验证 | `PASS` |
| R4-UI-001A | 信任卡片 | auto-accept/revoke | DevicesDock -> PairingTrustRuntime -> TrustedDeviceStore | 成功动作与 revoke 写失败非模态反馈已验证 | `PASS` |
| R4-UI-001B | 手动地址 | add/save/remove | DevicesDock -> DiscoverySettingsStore -> DeviceDiscoveryRuntime | 持久化与listener启动/刷新入口已验证 | `PASS` |
| R4-UI-001C | 信任卡片失败反馈 | auto-accept | DevicesDock -> PairingTrustRuntime -> TrustedDeviceStore | primary 写失败改为固定脱敏非模态反馈 | `PASS` |
| R4-UI-002 | 配对 | pairingRequested、SAS confirm/cancel | DevicesDock -> PairingTrustRuntime -> UDP pairing -> trust store | localhost production widget/UDP/trust 已验证 | `PASS` |
| R4-UI-003 | 权限 | PermissionStatusModel openSettingsRequested | Windows/Mac permission probe 与原生 settings opener | 003A 权限卡/门控、003B Windows current-host probe 已验证；原生往返未运行 | `IN_PROGRESS` |
| R4-UI-003A | 权限卡与能力门控 | PermissionSnapshot refresh | MainWindow -> PermissionStatusModel -> DevicesDock | 分项禁用、固定文案与同窗口恢复已验证 | `PASS` |
| R4-UI-003B | Windows probe/current-host 卡片 | WindowsFirewallProbe refresh | MainWindow -> WindowsFirewallProbe -> PermissionStatusModel -> DevicesDock | 受控端口转换、当前主机 snapshot 与权限卡逐项一致 | `PASS` |
| R4-UI-004 | 拖放/选择发送 | DevicesDock sendItemsRequested | TransferUiRuntime -> IFileTransferService::send -> FileTransferRuntime | typed start failure 写回现有本地化反馈 | `PASS` |
| R4-UI-005 | Incoming Offer / Ask | accept/reject/conflict decision | IncomingOfferModel / TransferUiRuntime -> FileTransferRuntime | 真实 TLS offer 经 production widget accept/reject 完成或拒绝 | `PASS` |
| R4-UI-006 | 传输中心 | pause/resume/cancel/retry | TransferCenterModel -> TransferUiRuntime -> FileTransferRuntime | 006A 控制与 006B history retry 均已验证 | `PASS` |
| R4-UI-006A | 传输中心控制 | pause/resume/cancel | TransferCenterDock -> TransferCenterModel -> FileTransferRuntime | 真实 widget 手势、双端状态与文件状态已验证 | `PASS` |
| R4-UI-006B | 历史重试 | retry failed outgoing | TransferCenterDock -> TransferCenterModel -> FileTransferRuntime::retry | 真实 history row 产生新 transfer/offer 并完成 | `PASS` |
| R4-UI-007 | 迷你条 | primary action、details | TransferCenterModel typed intents；details 打开 TransferCenterDock | 007A 真实传输刷新/主操作与007B dock路由已验证 | `PASS` |
| R4-UI-007A | 迷你条后台同步 | pause/resume、details intent | FileTransferRuntime -> TransferRuntimeComposition -> TransferCenterModel -> TransferMiniBar | 非零进度、精确指标与暂停恢复已验证 | `PASS` |
| R4-UI-007B | 迷你条详情路由 | details body gesture | MainWindow -> TransferMiniBar -> TransferCenterDock | 无active不可操作、dock前置与对应row已验证 | `PASS` |
| R4-UI-008 | 历史/打开位置 | openFile/openFolder/history retry | TransferHistoryRuntime / TransferUiRuntime validated resolver / QDesktopServices | opener 拒绝与 history load/persist error 写入本地化非模态反馈 | `PASS` |
| R4-UI-009 | 设置 | Save / incomingOfferSettingsRequested | SettingsDialog -> TransferSettingsStore -> MainWindow composition snapshot | Qt localhost/offscreen 动态证据已覆盖保存、重开与同窗口更新；原生 OS 窗口交互未运行 | `PASS` |
| R4-UI-010 | 托盘/menu bar | restore/pause/settings/quit | BackgroundLifecycleController -> core/transfer/discovery shutdown | 接线存在；OS tray/menu bar 未运行 | `NOT_RUN` |

关键 production 入口：

- `MainWindow.cpp`：发现/配对/设备动作、transfer composition、设置、托盘与 shutdown；
- `TransferUiRuntime.cpp`：发送、offer、冲突、控制、历史打开 typed intent；
- `DevicesDock.cpp`、`TransferCenterDock.cpp`、`TransferMiniBar.cpp`：用户手势转业务 intent；
- `FileTransferRuntime.cpp`、`PairingTrustRuntime.cpp`：真实 network/file service；
- `TransferHistoryRuntime.cpp`：历史持久化与打开位置桥接。

## 3. 当前自动测试能证明什么

当前源码存在以下候选目标，但本轮未在 `2c0328f` 上执行，因此状态不继承为 PASS：

| 范围 | 候选测试 | 仅能证明 | 不能证明 |
|---|---|---|---|
| 设备卡/配对 | DeviceHomeModel、DevicesDock、PairingWizardModel、PairingTrustRuntime、DeviceDiscoveryRuntime | localhost Pair/Confirm/Cancel、trust card与manual-address widget intent | 实际probe报文、原生窗口、真实 LAN、多网卡、物理配对 |
| 权限 | PermissionStatusModel、DevicesDock、MainWindowLayout | 门控、文案、控件绑定 | TCC、Windows/macOS 系统设置往返 |
| 拖放发送 | DevicesDock、TransferUiRuntime | local URL 过滤、immutable intent、fake service adapter | Explorer/Finder DnD、真实 service 失败反馈 |
| Incoming Offer | IncomingOfferModel、DevicesDock、TransferRuntimeComposition、FileTransferRuntime | localhost TLS offer、widget accept/reject、文件 SHA/row/清理 | Ask、原生窗口系统、物理双机 |
| 传输中心/迷你条 | TransferCenterModel/Dock、TransferMiniBar、FileTransferRuntime | localhost 真实传输期间 pause/resume/cancel/retry widget intent | 原生窗口与物理交互 |
| 历史/打开 | TransferUiRuntime、TransferRuntimeComposition | receive-root 路径校验、可注入 opener、失败反馈与交错状态 | OS shell 实际打开 |
| 设置/托盘 | MainWindowLayout、BackgroundLifecycle、QuitRegression | 保存绑定、close/minimize/quit policy | OS tray/menu bar、系统后台行为 |

Hosted CI、offscreen Qt 和 fake service 测试均不能替代物理 Win↔Mac、TCC 或原生文件拖放。

## 4. 首个纵向修复切片

选择 `R4-UI-004`，原因：

1. 用户已经完成本地文件/目录选择和目标设备选择，失败发生在最昂贵的操作时刻；
2. production `IFileTransferService::send()` 返回 typed `TransferStartResult`，当前 adapter 明确丢弃；
3. 目标刚离线、registry 变化或 service 未启动是正常运行条件，不应只写日志；
4. 可用一个失败测试覆盖 UI intent -> production adapter -> service rejection -> 用户可见反馈，范围小且可演示。

红测要求：

- 通过现有 DevicesDock/TransferUiRuntime typed intent 发起发送；
- service 返回 NotRunning 或 PeerUnavailable；
- UI model/dock 保留已选目标和路径，并显示本地化失败文本；
- 不创建 transfer row，不清空为成功态，不依赖真实物理设备；
- 成功发送和既有 transfer snapshot 路径保持不变。

实现结果：

- owner：`agent/a3/r4-send-failure-feedback@b036e1f7bbfaeb5fbc81b0223dac80240eeed8d2`；
- A0 集成：`agent/a0/redevelop-p0@5aa0bfc4bc53a2532c6be99629e8a839902fb32e`；
- `publishSendIntent` 先清旧提示再发送 intent，避免同步失败提示被覆盖；
- TransferUiRuntime 使用自身作为连接 context，消费 typed `TransferStartResult`；
- PeerUnavailable 复用设备不可用文案，其余 start failure 复用 `Transfer failed`，不显示内部 diagnostic；
- 失败保留设备与路径选择，不创建 transfer row；后续成功会清除失败提示；
- 新测试修复前退出 1，修复后定向槽、完整 RelayDeskTransferUiRuntimeTests、完整
  RelayDeskDevicesDockTests 均退出 0；A0 两完整目标复验退出 0；
- 独立 transfer reviewer 给出 GO，未发现 P0/P1、UAF、同步覆盖或 service 接口越界。

未新增视觉组件、翻译键、协议或 service API。托盘、权限、七语言和打包没有在本切片展开。

## 5. 第二个纵向修复切片

选择 `R4-UI-008`，范围只包含历史打开失败与历史存储失败反馈。

实现结果：

- owner：`agent/a3/r4-history-failure-feedback@258d7aa6e7419b9037c93b54aa072cd64f13e2e0`；
- A0 内容等价集成：`agent/a0/redevelop-p0@941149532447907c95dcca677bc9e82b2f7e1476`。两提交
  parent 均为 `b97f51271992fb71379c34b49d584abb255dc9ba`、tree 均为
  `2760d6a2e9c03653912383b23297a16b2a188561`，由 cherry-pick
  产生不同 commit id，不构成 owner -> A0 的 ancestry；
- `completionOpenRejected` 与 `TransferHistoryRuntime::historyError` 接入现有 Transfer Center 非模态状态面；
- 用户文案只使用固定翻译键，不显示 opener URL、receive root、completed path 或内部 diagnostic；
- 打开失败与历史错误使用独立状态；打开成功只清打开失败，仍有效的历史错误继续显示；
- 失败及后续成功均保留当前选择、打开按钮状态和历史记录；
- 新增两条翻译键，并同步 `en/es/it/ja/ko/ru/zh_CN` 七份 catalog；
- 原始两条红测均退出 1，失败点为 production UI 中不存在反馈 receiver；状态交错红测在旧单一状态实现下
  退出 1。日志分别为
  `C:\Users\52323\AppData\Local\Temp\relaydesk-a3-r4-red\open-red.log`（21:55:25）、
  `history-red.log`（21:55:59）和 `interleaving-red.log`（22:19:25）；
- owner 源码目录为 `F:\github\DeskflowPlus\working\relaydesk-a3-r4-history-failure-feedback`，fresh build 为
  `C:\Users\52323\AppData\Local\Temp\relaydesk-a3-r4-ui-debug`。先加载
  `Launch-VsDevShell.ps1 -Arch amd64 -HostArch amd64 -SkipAutomaticLocation`，再执行：

  ```powershell
  cmake --build 'C:\Users\52323\AppData\Local\Temp\relaydesk-a3-r4-ui-debug' --target RelayDeskTransferRuntimeCompositionTests RelayDeskTransferCenterDockTests RelayDeskI18NTests RelayDeskTransferUiRuntimeTests --parallel 2
  ```

  测试使用该 build 的对应 EXE、Qt/vcpkg Debug DLL `PATH`；三个 GUI 目标设置
  `QT_QPA_PLATFORM=offscreen`，均以 `-o <log>,txt` 输出。2026-08-31 22:22 的 receipts 为：

  | 目标 | Totals / exit | 日志 |
  |---|---|---|
  | RelayDeskTransferRuntimeCompositionTests | 10 passed, 0 failed, 0 skipped / 0 | `C:\Users\52323\AppData\Local\Temp\relaydesk-a3-r4-red\composition-green.log` |
  | RelayDeskTransferCenterDockTests | 4 passed, 0 failed, 0 skipped / 0 | `C:\Users\52323\AppData\Local\Temp\relaydesk-a3-r4-red\dock-green.log` |
  | RelayDeskI18NTests | 7 passed, 0 failed, 0 skipped / 0 | `C:\Users\52323\AppData\Local\Temp\relaydesk-a3-r4-red\i18n-green.log` |
  | RelayDeskTransferUiRuntimeTests | 8 passed, 0 failed, 0 skipped / 0 | `C:\Users\52323\AppData\Local\Temp\relaydesk-a3-r4-red\ui-runtime-green.log` |
- 独立 UI reviewer 首轮因 history/open 状态互相覆盖给出 NO-GO；修复并补交错测试后复核 GO，
  未发现新的 P0/P1 或范围扩张；
- A0 在 `F:\github\DeskflowPlus\working\relaydesk-redevelop-p0` 加载同一 VS 环境后执行：

  ```powershell
  cmake --build 'F:\github\DeskflowPlus\working\relaydesk-redevelop-p0\build\windows\r0-debug-fresh' --target RelayDeskTransferRuntimeCompositionTests RelayDeskTransferCenterDockTests RelayDeskI18NTests RelayDeskTransferUiRuntimeTests --parallel 2
  ```

  首次构建 43/43 完成、退出 0；2026-08-31 22:46:57 在相同 SHA 再执行为 `ninja: no work to do.`、
  退出 0，receipt 为
  `C:\Users\52323\AppData\Local\Temp\relaydesk-a0-r4-ui008-build-941149532.log`。该旧 build 的 QtTest
  进程在进入测试输出前超时，A0 本机动态复验记 `NOT_RUN`，不把超时写成测试失败，也不覆盖 owner
  fresh build 的通过证据。

`R4-UI-008` 的 `PASS` 只表示 production signal 已有本地化、非模态、脱敏的 UI receiver，并且相关
状态契约已由 fake opener/fixture 自动测试覆盖。fake opener、offscreen Qt 或 hosted runner 都不能证明
Explorer/Finder/QDesktopServices 在真实操作系统中实际打开文件或目录；该项仍为平台运行/最终验收
`NOT_RUN`。

## 6. 第三个纵向证据切片

选择 `R4-UI-005`，范围只包含 Incoming Offer 的 production UI 接受与拒绝链路。首次新增动态场景
直接通过，因此本切片只增加测试证据，没有修改 production 代码。

实现与验收结果：

- owner：`agent/a7/r4-incoming-offer-production-ui@5ac175f900e489323ba8019c9339592723266189`；
- A0 内容等价集成：`agent/a0/redevelop-p0@36d83e77cddaa49573a10aa15b21bdb7dc7be2d7`。两提交
  parent 均为 `d8a36cb59e905a9334dde8aace01b6463604de85`、tree 均为
  `a5a14f142787abbef577441763ce025adbf74dd3`，由 cherry-pick 产生不同 commit id；
- sender/receiver 均为真实 `FileTransferRuntime`，使用 TLS identity、双向 trusted fingerprint store、
  discovery registry advertisement、localhost 动态监听和真实临时文件系统；
- receiver 由 production `TransferRuntimeComposition` 持有 service，offer 进入 `IncomingOfferModel` 并驱动
  `DevicesDock` 可见面板；测试使用 `QTest::mouseClick` 点击真实 Accept/Reject 按钮，没有直接调用
  service 或 model；
- Accept 后 sender 为 `Completed`，接收文件 SHA-256 与源文件一致，receiver transfer row 为
  `Completed`，offer panel 隐藏且无 `.part`；
- Reject 后 sender 为 typed `Rejected`，目标文件和 `.part` 均不存在，receiver 不新增 rejected row；
  既有 accepted row 的 snapshot、row count 和当前选择均保持不变；
- owner C 盘 build 为 `C:\Users\52323\AppData\Local\Temp\relaydesk-a7-ui005`。新增槽、完整
  RelayDeskTransferRuntimeCompositionTests、完整 RelayDeskDevicesDockTests 均退出 0；完整
  RelayDeskFileTransferRuntimeTests 为 56 passed、0 failed、4 skipped、退出 0，日志为
  `C:\Users\52323\AppData\Local\Temp\relaydesk-a7-ui005-file-runtime.txt`（2026-08-31 23:27:28）；
- transfer reviewer 首轮 GO。UI reviewer 首轮因 reject 后 receiver row/selection 缺少断言给出 NO-GO；
  补齐后复核 GO，未发现新的 P0/P1 或范围扩张；
- A0 在 fresh C 盘 build `C:\Users\52323\AppData\Local\Temp\relaydesk-a0-ui005` 使用 VS 2022、
  Ninja、Qt 6.10.1 与现有 x64-windows Debug vcpkg 安装树，执行：

  ```powershell
  cmake --build 'C:\Users\52323\AppData\Local\Temp\relaydesk-a0-ui005' --target RelayDeskTransferRuntimeCompositionTests --parallel 8
  ```

  106/106 完成、退出 0，日志为
  `C:\Users\52323\AppData\Local\Temp\relaydesk-a0-ui005-build.log`（2026-08-31 23:46:55）。随后设置
  Qt/vcpkg Debug `PATH` 与 `QT_QPA_PLATFORM=offscreen`，新增槽为 3 passed、0 failed、0 skipped、
  退出 0，日志为 `C:\Users\52323\AppData\Local\Temp\relaydesk-a0-ui005-new-slot.log`；完整 Composition
  为 11 passed、0 failed、0 skipped、退出 0，日志为
  `C:\Users\52323\AppData\Local\Temp\relaydesk-a0-ui005-composition-full.log`（2026-08-31 23:47:32）。

`R4-UI-005` 的 `PASS` 只覆盖 Windows 同机 localhost TLS socket、真实临时文件系统、production
composition/model 和 offscreen Qt widget typed intent。它不证明 native Windows/macOS window system、
Explorer/Finder、macOS TCC、物理 Win↔Mac 双机、Ask 冲突或正式发布行为；这些仍为 `NOT_RUN`。

## 7. 第四个纵向修复切片

选择 `R4-UI-006A`，范围只包含 Transfer Center 的 pause/resume/cancel；retry 不在本切片。

根因与修复：

- 初始动态红测使用真实 FileTransferRuntime/TLS/trust/discovery 与非零 `.part`，receiver runtime 和
  TransferCenterModel 均已处于 `Transferring`，但 UI 无法点击 Pause；4 KiB chunk 最终红证据为 runtime
  `22,548,480 / 33,554,469`、model `22,470,656 / 33,554,469`、`.part=22,556,672`，日志
  `C:\Users\52323\AppData\Local\Temp\relaydesk-a7-ui006a\ui006a-chunk4k-final.log`；
- production 根因是 `IncomingTransferRuntime::publishProgress()` 将 receiver snapshot 状态改为
  `Transferring`，却继承 WaitingForAcceptance 的 `canPause=false`；底层直接调用 service 的旧测试绕过
  TransferCenterModel capability role，未暴露该缺口；
- 修复在既有状态发布点同步 action flags：Transferring 可 pause/cancel，Completed/Failed 清理控制，
  Resuming 禁止 pause/resume 但可 cancel，Interrupted 可 resume/cancel；没有新增接口、协议或 retry 行为。

实现与验收结果：

- owner：`agent/a7/r4-transfer-center-controls@361b3ba2e5aa9e675e24dc0ecdd40d4808278434`；
- A0 内容等价集成：`agent/a0/redevelop-p0@9c38f79e9394ed283ad5ef24a2c2710f2563a281`。两提交
  parent 均为 `79f3469aa9e48657d426672271afaa1faa4f1b77`、tree 均为
  `710fd6f1c280c0a7acba53d7f72c99428a842c7b`，由 cherry-pick 产生不同 commit id；
- fixture 使用 32 MiB + 37 B 确定性文件、4 KiB negotiated chunk、16 KiB payload 和 64 KiB TLS
  write queue；全部是 production 支持的有界配置；
- Pause/Resume 通过当前选中 active row 的真实 QPushButton mouse click；Cancel 通过真实 More
  InstantPopup 和 QMenu action geometry mouse click。model signal 只负责选中 row，并用 `singleShot(0)`
  把手势排到下一事件循环，没有在 signal 栈内直接控制 service；
- Pause 后 sender/receiver 均为 `Paused`，400 ms 内双方 completedBytes 与 `.part` size 不前进；
  Resume 后双方 `Completed`，最终文件 SHA-256 与源文件一致，incoming/outgoing recovery state 和
  `.part` 均清理；
- 独立第二次传输 Cancel 后双方 typed `Cancelled`，receiver 顺序包含 `Cancelling -> Cancelled`，无最终
  文件；默认 Keep 策略保留 `.part` 与 incoming recovery state，outgoing recovery descriptor 清除，
  无 runtime error；
- owner 最终 portable-temp 新增槽日志
  `C:\Users\52323\AppData\Local\Temp\relaydesk-a7-ui006a\ui006a-portable-slot.log`（2026-09-01
  01:31:37，64.984s）为 3/0/0、退出 0；同一控制逻辑在 portable-path 修正前的稳定性日志
  `ui006a-final-chain.log`、`ui006a-stability-2.log`、`ui006a-stability-3.log` 均为 3/0/0，形成
  3/3。修正后完整 Composition `composition-portable-full.log`（01:33:01，63.909s）为 12/0/0，
  Dock `transfer-center-dock-full.log`（01:21:40）为 4/0/0，FileTransferRuntime
  `file-transfer-runtime-full.log`（01:22:48）为 56 passed、0 failed、4 skipped，均退出 0；
- transfer reviewer 最终按 UI-006A 原始状态/文件契约给出 GO。UI reviewer 首轮因硬编码 Windows
  临时路径给出 NO-GO；改用 `QDir::tempPath()` 后复核 GO，未发现新的 P0/P1；
- A0 fresh build `C:\Users\52323\AppData\Local\Temp\relaydesk-a0-ui006a` 使用 VS 2022、Ninja、
  Qt 6.10.1 与现有 x64-windows Debug vcpkg 安装树，Composition 106/106 构建退出 0，日志
  `C:\Users\52323\AppData\Local\Temp\relaydesk-a0-ui006a-build.log`（2026-09-01 01:43:16）；新增槽
  `relaydesk-a0-ui006a-slot.log`（01:44:46，62.552s）为 3/0/0、退出 0，完整 Composition
  `relaydesk-a0-ui006a-composition-full.log`（01:45:51，64.585s）为 12/0/0、退出 0。

`R4-UI-006A` 的 `PASS` 只覆盖 Windows 同机 localhost TLS、真实临时文件系统和 offscreen Qt
Transfer Center widget intent。native Windows/macOS window system、TCC、物理 Win↔Mac 和正式发布
不由本证据证明。

## 8. 第五个纵向证据切片

选择 `R4-UI-006B`，范围只包含真实 Failed outgoing history row 的 retry。

实现与验收结果：

- owner：`agent/a7/r4-history-retry-ui@52fd8341aaa3ec3770703c32c7367f721657c715`；
- A0 内容等价集成：`agent/a0/redevelop-p0@2483319805ba1c3b7002e8bfc346f53ead8f9b0e`。两提交
  parent 均为 `9a03ad52625ee6c25d0ec29927bfc6ecad3cd8a9`、tree 均为
  `67ec839bebfc56f170596dcec11ba423b4498e90`，由 cherry-pick 产生不同 commit id；
- sender/receiver 均由真实 `TransferRuntimeComposition` 持有 FileTransferRuntime；sender 绑定真实
  TransferCenterDock/history runtime，receiver 绑定真实 DevicesDock/IncomingOfferModel，TLS identity、
  双向 trust、discovery、localhost socket 和临时文件系统均为 production 实现；
- sender 生成首个 manifest/offer 后、receiver 点击真实 Accept 前，以同长度不同内容替换源文件；
  TransferSender 在首块前检测 SourceChanged，使旧 outgoing 成为 typed `Failed + canRetry` 并异步进入历史；
- 选中旧 Failed history row 后只点击可见 Retry QPushButton。model 立即清除旧 retry availability，
  service 发布绑定旧 ID 的 Retry Applied，并通过正常 `send()` 产生不同的新 transfer ID 与第二个真实 offer；
  旧 row 不能重复点击；
- receiver 再次点击真实 Accept 后，新 transfer 完成，目标 SHA-256 与当前源一致；旧 Failed history
  不被改写，新 row/history 为 Completed；
- SourceChanged 发生在首块前，因此旧 transfer 从未创建 `.part`。测试在 Retry 前等待旧 incoming
  recovery sidecar 持久化，并在 Applied、新 transfer 和最终完成后逐次比较其原始字节与解码状态保持
  不变，同时断言 old no-part 状态保持；没有伪造 partial，也没有把无 `.part` 误写成 retry 清理；
- owner C 盘日志根为 `C:\Users\52323\AppData\Local\Temp\relaydesk-a7-ui006b\logs`。最终新增槽
  `ui006b-final-slot-1.txt`、`ui006b-final-slot-2.txt`、`ui006b-final-slot-3.txt` 均为 3/0/0、退出 0，
  bounded 3/3；完整 Composition `composition-full-final.txt` 为 13/0/0，Dock
  `transfer-center-dock-full-final.txt` 为 4/0/0，FileTransferRuntime
  `file-transfer-runtime-full-final.txt` 为 56 passed、0 failed、4 skipped，HistoryStore
  `history-store-full-final.txt` 为 11/0/0，均退出 0；
- UI 与 transfer reviewer 均复核 GO，未发现 P0/P1；
- A0 fresh build `C:\Users\52323\AppData\Local\Temp\relaydesk-a0-ui006b` 使用 VS 2022、Ninja、
  Qt 6.10.1 与现有 x64-windows Debug vcpkg 安装树，Composition 106/106 构建退出 0，日志
  `C:\Users\52323\AppData\Local\Temp\relaydesk-a0-ui006b-build.log`（2026-09-01 02:23:19）；新增槽
  `relaydesk-a0-ui006b-slot.log`（02:23:50，3.933s）为 3/0/0、退出 0，完整 Composition
  `relaydesk-a0-ui006b-composition-full.log`（02:25:00，69.112s）为 13/0/0、退出 0。

`R4-UI-006B` 关闭了 history retry 缺口，因此 `R4-UI-006` 在 Windows 同机 localhost/offscreen
production composition 范围内为 `PASS`。该状态不证明 native Windows/macOS window system、TCC、
物理 Win↔Mac 或正式发布行为。

## 9. 第六个纵向证据切片

选择 `R4-UI-002`，范围只包含 production pairing UI 的 Pair、SAS Confirm 与独立 Cancel。

实现与验收结果：

- owner：`agent/a2/r4-pairing-ui@b6a37091f7d6e775f65bde1304490256c4aa7e90`；
- A0 内容等价集成：`agent/a0/redevelop-p0@a2cb8a2af92cca580ca5c09cea19cdbe1ce854a3`。两提交
  parent 均为 `1f6df3e6c72999bb9b35f18853d8850c5ab481ee`、tree 均为
  `cffa3e0fec7de840e494f7baf11812aa14ce2116`，由 cherry-pick 产生不同 commit id；
- 测试复用现有 RuntimePair：两个真实 DeviceDiscoveryRuntime/PairingTrustRuntime 各自绑定独立
  localhost UDP port，使用不同 DeviceId、不同 advertised certificate fingerprint、不同 device model 和
  独立 trust store 路径；endpoint resolver 指向对端实际 boundPort，没有新增协议宿主；
- 本端真实 DevicesDock 从已发现候选 card 选中设备，只通过 Pair QPushButton mouse click 发出 typed
  DeviceId；测试连接与 production MainWindow 相同的 intent boundary，由 PairingTrustRuntime 使用当前
  discovery identity/endpoint 发起配对，本端没有直接调用 service/model；
- 发起端 PairingWizardModel 显示实际六位 SAS，widget label 使用三位分组。协议角色非对称：远端不展示
  SAS，而是进入 canSubmitCode 状态；远端 harness 仅提交发起端模型的实际 SAS 值，未硬编码验证码；
- 本端只点击真实 Confirm QPushButton。完成后双方 session 为 Completed，双方 device card 为
  Online + trusted，pinned fingerprint 分别准确绑定对端 fingerprint；重新加载两份 trust store 仍为
  Trusted；
- 完成后本端 Pair 按钮隐藏；对隐藏按钮的重复手势不产生新 session，两份 trust JSON 在手势前后字节
  完全一致，证明没有重复写 trust；
- 独立 Cancel 场景使用新的 runtime pair、身份和 trust 目录。本端只点击真实 Pair 和 Cancel 按钮，双方
  session 为 Rejected，device card 回到 Discovered/untrusted，Cancel 隐藏、Pair 恢复可用；重新加载
  两份 store 均无对端 trust；
- 本切片只新增测试，没有修改 production；
- owner C 盘日志根为 `C:\Users\52323\AppData\Local\Temp\relaydesk-a2-ui002`。Confirm 三轮为
  504/516/459ms，Cancel 三轮为 456/522/547ms，均 3/0/0、退出 0。完整目标均退出 0：
  PairingTrustRuntime 11/0、DevicesDock 30/0、PairingWizardModel 10/0、PairingStateMachine 11/0、
  PairingMessageCodec 13/0、PairingTrustCommitter 6/0、PairingManager 9/0、PairingService 5/0、
  TrustedDeviceStore 10/0、DeviceDiscoveryRuntime 10/0；对应 receipts 为 `full-*.txt`；
- UI reviewer 首轮 GO。pairing reviewer 首轮把协议误读为双方都展示 SAS；定向动态复跑证明 responder
  模型按设计不展示 SAS、只提交 initiator 码，reviewer 按非对称 production 角色复核后 GO；修正版 Confirm
  `pairing-ui-confirm-sas-asymmetric.txt` 为 3/0/0，完整 PairingTrustRuntime
  `full-PairingTrustRuntime-sas-asymmetric.txt` 为 11/0/0，均退出 0；
- A0 fresh build `C:\Users\52323\AppData\Local\Temp\relaydesk-a0-ui002` 使用 VS 2022、Ninja、
  Qt 6.10.1 与现有 x64-windows Debug vcpkg 安装树，serial 106/106 构建退出 0，日志
  `C:\Users\52323\AppData\Local\Temp\relaydesk-a0-ui002-build.log`（2026-09-01 03:26:55）；Confirm
  `relaydesk-a0-ui002-confirm.log`（03:27:20，187ms）与 Cancel `relaydesk-a0-ui002-cancel.log`
  （03:27:21，171ms）均为 3/0/0、退出 0，完整 PairingTrustRuntime
  `relaydesk-a0-ui002-full.log`（03:27:22，714ms）为 11/0/0、退出 0。

`R4-UI-002` 的 `PASS` 只覆盖 Windows 同进程 localhost UDP、offscreen Qt widget、真实 pairing runtime
和 trust store。它不证明 native Windows/macOS window system、真实 LAN 广播、多网卡选路、TCC、物理
Win↔Mac 设备或正式发布行为。

## 10. 第七个纵向修复切片

选择 `R4-UI-001A`，范围只包含 trusted device card 的 auto-accept、revoke 与 revoke persistence failure 反馈；
manual address 不在本切片。

根因与修复：

- 成功链使用真实 MainWindow/DevicesDock More 菜单：AutoAccept action 经 production typed boundary 调用
  PairingTrustRuntime，持久化 JSON 与 card 同步；Revoke action 经真实确认对话框写 revoked tombstone、清除
  autoAccept 并把 card 置为不再信任；隐藏/禁用后的重复手势不改写 store；
- 初始负向红测以真实 primary path 写失败注入触发 PersistenceFailed。MainWindow 原先只写 diagnostic 日志，
  用户界面没有反馈；修复复用 DevicesDock 现有非模态 feedback 面，显示固定七语言
  `devices.trust.update_failed` 文案，不接收或显示 diagnostic/path；
- trust feedback 带来源 guard。成功 revoke 或成功 auto-accept 只清理 trust error，不会误清除文件发送反馈；
- 负向测试连续两次走真实 Revoke 菜单与确认按钮，runtime/card 保持 trusted + autoAccept，primary 不可写时
  load 从旧 backup 恢复相同状态；恢复 primary 可写后，真实 AutoAccept 成功持久化并清除旧 feedback；
- 监督审阅发现 backup-only 语义不能只在 PairingManager::revoke 特判。共享契约下沉到
  TrustedDeviceStore::save：authoritative primary 原子提交即 `ok=true`；backup 写失败只通过
  `source=Primary + diagnostic` 表示降级。primary 首次写失败仍为 `ok=false`，所有 PairingManager、
  PairingTrustRuntime、PairingTrustCommitter、reconnect 调用方行为一致；
- Store、PairingManager 与 PairingTrustRuntime 分别补充 backup-only 回归，证明 auto-accept/revoke 在 primary
  已提交时同步更新内存，避免磁盘已提交而调用方回滚。

实现与验收结果：

- owner：`agent/a2/r4-trust-card-actions@fb4e75f9214cb4f37afea896095aac5cf8a527da`；
- A0 内容等价集成：`agent/a0/redevelop-p0@7095330241f1e3a093e96359370a8cbc5c1f98d3`。两提交 parent 均为
  `dbefd7dd94c642d5cddc90f1d962b94e2cf09425`、tree 均为
  `543df89f246831ce899ccfa30f66b3ad72b189f3`，由 cherry-pick 产生不同 commit id；
- owner 使用稳定 clean C 盘 build `C:\Users\52323\AppData\Local\Temp\relaydesk-a2-ui001a-clean`。
  成功链与 primary-write failure 负向各 bounded 3/3、均退出 0；完整 MainWindowLayout 15/15、
  DevicesDock 30/30、TrustedDeviceStore 11/11、PairingManager 10/10、PairingTrustRuntime 11/11、
  i18n 7/7，均退出 0；
- UI 与 pairing reviewer 最终均 GO，未发现 P0/P1；监督要求的完整 DevicesDockTests 已在 guard 修改后
  独立重跑；
- A0 fresh build `C:\Users\52323\AppData\Local\Temp\relaydesk-a0-ui001a` 使用 VS 2022、Ninja、
  Qt 6.10.1 与独立 x64-windows Debug vcpkg 安装树，serial 278/278 构建退出 0，日志
  `C:\Users\52323\AppData\Local\Temp\relaydesk-a0-ui001a-build.log`（2026-09-01 05:37:49）；成功链
  `ui001a-success.log`（05:38:17，361ms）与失败链 `ui001a-failure.log`（05:38:17，522ms）均为
  3/0/0、退出 0，完整 MainWindowLayout `main-window-layout-full.log`（05:38:21，3.245s）为
  15/0/0、退出 0。

`R4-UI-001A` 的 `PASS` 只覆盖 Windows localhost advertisement、真实临时 store 与 offscreen MainWindow
widget intent。native Windows/macOS window system、真实 LAN、多网卡、TCC、物理 Win↔Mac 和发布不由
本证据证明。

## 11. 第八个纵向证据切片

选择 `R4-UI-001B`，范围只包含 manual address 的 Add/Save/重开/Remove/Save production UI 链。

实现与验收结果：

- owner：`agent/a2/r4-manual-address-ui@d81c13e549ab491b86a11f4ee4ab082785a9582f`；
- A0 内容等价集成：`agent/a0/redevelop-p0@34f2481700dbf2119d93dbca047e2f4545011997`。两提交
  parent 均为 `cff80aacf0d6d5df9f4fca1471d5ebd5ed16cc61`、tree 均为
  `c1d09dce21dde2fc9e6947f678a6e4addce59c65`，由 cherry-pick 产生不同 commit id；
- 初始 DiscoverySettings 为 `enabled=false` 且 manualAddresses 为空，真实 MainWindow 构造后
  DeviceDiscoveryRuntime 未运行；
- 测试只通过 DevicesDock 的 Manage QPushButton 打开真实 modal dialog，填写 `127.0.0.1`、input port
  `24910`、file port `24911`，并用 Add/Save mouse click 触发 production receipt；没有直接调用 signal、
  model、store 或 runtime；
- 保存成功后 dialog 关闭，DiscoverySettingsStore 复读地址和 ports 一致；DeviceDiscoveryRuntime 从 stopped
  进入 running，DiscoveryService `started` 只发一次，证明 listener 已启动且 manual-address refresh 入口可达；
- 再次真实打开 dialog 可见同一条目；选中后 Remove/Save，store 复读为空。runtime 合理保持 running，
  `started` 仍为一次，没有重复启动；
- 本切片只新增测试，production 现有接线直接通过；
- owner 稳定证据位于 `C:\Users\52323\AppData\Local\Temp\relaydesk-a0-ui001b-ownercheck`：新增槽
  `ui001b-first.log`、`ui001b-stability-2.log`、`ui001b-stability-3.log` 均为 3/0/0、退出 0；完整
  MainWindowLayout 16/0、DevicesDock 30/0、DiscoverySettings 22/0、DeviceDiscoveryRuntime 10/0、
  AddressCandidateProvider 10/0，均退出 0；
- UI 与 discovery reviewer 均 GO，未发现 P0/P1。reviewer 明确 `started/isRunning` 只证明 UDP listener
  启动和刷新入口可达，不能证明 AddressCandidateProvider 已完成解析或 probe datagram 抵达；
- A0 fresh build `C:\Users\52323\AppData\Local\Temp\relaydesk-a0-ui001b` 使用 VS 2022、Ninja、
  Qt 6.10.1 与独立 x64-windows Debug vcpkg 安装树，serial 278/278 构建退出 0，日志
  `C:\Users\52323\AppData\Local\Temp\relaydesk-a0-ui001b-build.log`（2026-09-01 06:39:33）；新增槽
  `ui001b-slot.log`（06:39:52，653ms）为 3/0/0、退出 0，完整 MainWindowLayout
  `main-window-layout-full.log`（06:39:56，3.916s）为 16/0/0、退出 0。

`R4-UI-001B` 关闭了 manual-address UI 缺口。该状态不证明实际 probe 数据包、native Windows/macOS window system、
真实 LAN、DNS、多网卡选路、TCC、物理 Win↔Mac 或正式发布行为。

## 12. 第九个纵向修复切片

选择 `R4-UI-001C`，范围只包含 trusted device card 的 auto-accept primary 写失败反馈；不重复
revoke、manual address 或视觉设计。

根因与修复：

- 根因位于 production MainWindow 组合层：revoke 失败已调用 DevicesDock 的非模态 trust feedback，
  但 auto-accept 失败仍调用 `QMessageBox::warning`，并把 `PairingOperationResult::diagnostic` 拼入用户文案；
- 最小修复保留 diagnostic 日志，只把用户反馈改为既有 `showTrustActionFailure()`；成功路径继续调用
  `clearTrustActionFeedback()`，其来源 guard 不会清除文件发送反馈；
- owner：`agent/a2/r4-auto-accept-failure-feedback@55388091f36c28f602c7fb7db6d2bf6609e7df75`；
- A0 内容等价集成：`agent/a0/redevelop-p0@38e955bd62fcf6dc78078b004cdccbfbd8b17be5`。两提交
  parent 均为 `6b65e951fe8f64b0ef77d052951ca400c52682fa`、tree 均为
  `3a467e04971119da42cc4871857549329b5facd7`。

RED/GREEN 与复审：

- 同一新增测试仅应用到精确 parent 的临时基线，不含 production 修复；
  `ui001c-red-final.log` 退出 1，2 passed / 1 failed，日志包含旧路径 internal diagnostic，断言记录
  `modalSeen=1 feedbackVisible=0 feedbackText=`；
- 修复版通过真实 DevicesDock More 菜单与 auto-accept action 触发失败：没有模态框，现有非模态面显示
  `devices.trust.update_failed` 的固定本地化文案，不含路径；PairingTrustRuntime、device card 与 store 均保持
  原 auto-accept 状态。恢复 primary 写入后，同一真实手势成功持久化新状态并清除旧 trust feedback；
- owner 新增槽三轮均 3/0/0；完整 MainWindowLayout 17/0、DevicesDock 30/0、TrustedDeviceStore 11/0、
  PairingManager 10/0、PairingTrustRuntime 11/0、i18n 7/0，均退出 0；格式化后新增槽 3/0/0、退出 0；
- UI 与 pairing reviewer 均 GO，无 P0/P1；
- A0 fresh 目录 `C:\Users\52323\AppData\Local\Temp\relaydesk-a0-ui001b` 增量构建 7/7、退出 0，
  `ui001c-a0-slot.log` 为 3/0/0、461ms、退出 0。

因此 `R4-UI-001C` 为 `PASS`，`R4-UI-001` 在 Windows localhost/offscreen production UI 与临时 store
范围内恢复为 `PASS`。该证据不证明 native Windows/macOS window system、真实 LAN、多网卡、TCC、
物理 Win↔Mac 或正式发布行为。

## 13. 第十个纵向证据切片

选择 `R4-UI-003A`，范围只包含当前 SHA 的 production 权限卡与 capability gating；不包含系统设置页、
迷你条、tray 或视觉改版。

实现与动态证据：

- owner：`agent/a3/r4-permission-card-gating@16f0dbe39fcc4ce44f72ac4629176149f097719c`；
- A0 内容等价集成：`agent/a0/redevelop-p0@bfe7a5d868324fe1ba2953ff8884eb2456b5c1f0`。两提交
  parent 均为 `9714a169edc4d826f55dbaa486f66ebf1671c36d`、tree 均为
  `5c71c8616e07575db7e47f0a73c856c9a1ab68fa`；
- 只新增 MainWindowLayout 动态测试，production 现有接线直接通过；
- 测试构造真实 MainWindow，通过 production discovery registry 显示候选设备并用 DevicesDock 列表手势选中；
  Granted 时 Pair 可用，Windows Firewall Denied 时 Pair 禁用并显示固定防火墙文案；
- Windows Firewall 恢复 Granted 后，ListeningPort 按真实 probe 契约发布
  `NeedsAction + WindowsPortUnavailable`，Pair 再次禁用并显示固定端口文案；两项恢复 Granted 后，
  同一 MainWindow 无需重启即可重新启用，真实 Pair 按钮点击发出 typed pairing intent；
- macOS 条件分支只翻转 LocalNetwork，Accessibility/InputMonitoring 保持 Granted，证明 source contract
  不把网络权限扩展为输入权限总开关；当前未在真实 macOS/TCC 环境运行；
- owner fresh 目录 `C:\Users\52323\AppData\Local\Temp\relaydesk-a3-r4-permission-card-gating-fresh`
  的有效 build 为 294/294；最终新增槽三轮均 3/0/0。完整 MainWindowLayout 18/0、DevicesDock 30/0、
  PermissionStatusModel 9/0、PermissionSnapshot 4/0、WindowsFirewallProbe 22/0，均退出 0；
- UI reviewer 与 platform reviewer 最终均 GO。platform reviewer 确认 ListeningPort 状态与 production
  `WindowsFirewallProbe::listeningPortEntry(NotListening)` 映射一致；
- A0 fresh 目录 `C:\Users\52323\AppData\Local\Temp\relaydesk-a0-ui001b` 单次增量构建 4/4、退出 0，
  `ui003a-a0-slot.txt` 为 3/0/0、532ms、退出 0。

因此 `R4-UI-003A` 在 Windows localhost/offscreen 与注入 PermissionSnapshot contract 范围内为 `PASS`，
`R4-UI-003` 改为 `IN_PROGRESS`。真实 Windows Firewall probe、macOS TCC、系统设置往返、native
Windows/macOS window system、物理 Win↔Mac 与正式发布仍为 `NOT_RUN` 或最终验收项。

## 14. 第十一个纵向证据切片

`R4-UI-003B` 只验证当前 Windows 主机的 production `WindowsFirewallProbe` 与权限卡，不修改
Firewall 规则，也不展开 macOS、tray、设置页或视觉改版。

- A0 fresh2 构建为 282/282、退出 0；临时 `controlled-listener` 与 `current-host-mainwindow` 槽均为
  3/0/0；
- 受控、本进程 loopback 临时端口从未监听到监听时，真实 probe 得到 `NotListening` 后得到
  `Listening`；
- 真实 MainWindow 读取 production probe current snapshot，PermissionStatusModel 的两项 kind/state/error
  code 与其逐项一致，DevicesDock 权限卡标题和文案一致；没有注入 fake snapshot；
- Firewall 规则 canonical 投影前后 SHA-256 均为
  `30EFF3C905815B4A06D4042340C5776BAC5E19DC4B398BE098C0D09DD5DC5626`，规则数均为 899；
- `SystemSettings` 在验收前已存在，且 production launcher 没有可观察的返回契约，所以 Windows 系统设置
  打开/返回仍为 `NOT_RUN`。

详见 `product/docs/reports/R4_WINDOWS_PERMISSION_RUNTIME.md`。本切片只令 `R4-UI-003B` 为 `PASS`，总项
`R4-UI-003` 继续为 `IN_PROGRESS`；macOS TCC、native 窗口、真实 LAN/多网卡、物理设备和发布不在范围内。

后续 R4 默认采用单构建执行者：每个 owner/A0 工作树只由明确执行者启动并持有构建进程到退出；
状态轮询只读取 PID、进程状态和日志，不用重复 `cmake --build` 轮询。

## 15. 第十二个纵向证据切片

选择 `R4-UI-007A`，范围只包含真实后台传输驱动 production `TransferMiniBar` 刷新和主操作；不修改
production 或视觉，也不把 details intent 外推为 MainWindow 已打开 TransferCenterDock。

- owner：`agent/a7/r4-mini-bar-runtime@da3497e69e2932f40497f034c3095015322cbfcd`；
- A0 内容等价集成：`agent/a0/redevelop-p0@6a06645752d4dcc1265be55bf304f04b29080bc8`。两提交
  parent 均为 `21e6f5c88f0564c8eade9f88eeea3f8d13b94b9a`、tree 均为
  `c737eeeb200a31c5485e3c9ccfbaf6bc89bd1f87`；
- 只新增 TransferRuntimeComposition 动态测试。真实 sender/receiver FileTransferRuntime、TLS identity、
  双向 trust、discovery 与 production composition/model 驱动迷你条，不注入手工 transfer snapshot；
- 迷你条初始隐藏；真实接收按钮 Accept 后从 production snapshot 自动显示，进度大于 0，标题、
  ProgressPercentRole、ProgressText 与 SpeedText/StateText 均和同一 TransferCenterModel 行精确一致；
- 真实 primary QPushButton 发出 Pause，sender/receiver 均进入 Paused，400ms 稳定窗口内两端 completedBytes
  不前进；同一按钮 Resume 后双方 Completed，目标 SHA-256 与源一致，运行期 error 列表为空；
- 鼠标点击 bar body 与 Enter 键均发出 details intent；本切片未证明 MainWindow 把该 intent 打开真实
  TransferCenterDock，因此该子点仍 `NOT_RUN`；
- 局部 connection context 在捕获变量后声明，结束时显式 `composition.stop()` 再 `sender.stop()`，未引入
  悬空捕获或依赖析构顺序回退；
- transfer reviewer 与 UI reviewer 最终均 GO；UI reviewer 的 metrics/progress 精确性 P1 已关闭；
- owner worktree 的有效 A0 receipt `ui007-a0-p1-slot.txt` 为 3/0/0、48.034s、退出 0；A7 早期 full
  receipt 丢失，不作为证据；
- A0 集成 fresh 目录 `C:\Users\52323\AppData\Local\Temp\relaydesk-a0-ui001b` 单次增量构建 4/4、
  `ui007-integration-slot.txt` 为 3/0/0、49.504s、退出 0。

因此 `R4-UI-007A` 在 Windows localhost/offscreen production widget/runtime 范围内为 `PASS`，总项
`R4-UI-007` 改为 `IN_PROGRESS`。MainWindow details→TransferCenterDock、native Windows/macOS、TCC、
物理 Win↔Mac 与发布仍未证明。

## 16. 第十三个纵向证据切片

选择 `R4-UI-007B`，只验证 production MainWindow 的 `TransferMiniBar::detailsRequested` 到自身
TransferCenterDock 的路由；不重复真实 TLS、Pause/Resume 或 SHA 传输。

- owner：`agent/a3/r4-mini-bar-details@1a61ab239bf1d834533f3f05673746cfd4bb5fd2`；
- A0 内容等价集成：`agent/a0/redevelop-p0@678695f1ce04f4572e76d62bd90526a4bad36203`。两提交
  parent 均为 `0e1526671a43f16910079cc3ddf41768a8231763`、tree 均为
  `5ad4bddb7d724d219f43db264a6f9096da2a31ea`；
- 只新增 MainWindowLayout 动态测试，production 现有接线直接通过；
- 使用 MainWindow 自身的 TransferCenterModel、TransferMiniBar 与 TransferCenterDock。无 active row 时
  mini bar 和 dock 均隐藏，不制造可见、可操作的 details；
- 注入一个仅用于 UI 路由的 active TransferSnapshot 后，mini bar 由自身 model 自动显示，dock 仍隐藏；
  测试未把该 snapshot 表述为真实传输证据，007A 单独证明真实 runtime 链；
- 真实 mini bar body 鼠标点击经 MainWindow 既有 production connect 使同一个 dock 可见，且
  `visibleRegion()` 非空；dock 的真实 `relaydeskTransfersView` 可按 TransferIdRole 访问对应 row。测试未直接
  show dock、复制 lambda 或打开 history details dialog；
- owner 新增槽三轮均 3/0/0；完整 MainWindowLayout 19/0、TransferMiniBar 4/0、TransferCenterDock 4/0，
  均退出 0；UI 与 transfer reviewer 均 GO；
- A0 fresh 目录 `C:\Users\52323\AppData\Local\Temp\relaydesk-a0-ui001b` 单次增量构建 4/4、退出 0，
  `ui007b-integration-slot.txt` 为 3/0/0、641ms、退出 0。

因此 `R4-UI-007B` 为 `PASS`，`R4-UI-007` 在 localhost/offscreen production UI 范围内恢复为 `PASS`。
offscreen 的 visible region 不证明 native Windows/macOS 窗口管理层级；TCC、物理 Win↔Mac 与发布仍不由本证据证明。

## 17. 第十四个纵向证据切片

选择 `R4-UI-009`，只验证当前 SHA 的 production 文件传输设置保存、重开与运行时应用；不展开 tray、权限、自动启动或视觉设计。

- owner：`agent/a3/r4-settings-runtime-evidence@8aa690359c38096f155c3883647612ac5a1eb7ee`；
- A0 内容等价集成：`agent/a0/redevelop-p0@d015027e9470ff26d532ddbef80b03912bc18a52`。两提交 parent 均为 `675146944e5be92730e044907f451f25f624cca2`、tree 均为 `4790e3a7067a1c8dc9fc3e436df2187836c26f35`；
- production 零改动；只把既有 runtime 槽从直接发射 signal 改为先显示 pending offer，再点击真实 DevicesDock 设置按钮；
- `MainWindowLayoutTests::fileTransferSettingsPersistAndReopen` 只通过真实 SettingsDialog Save 按钮写入 receive root、incoming policy 与 conflict policy；随后重新构造 dialog，逐项精确回显已保存值；
- `MainWindowLayoutTests::fileTransferSettingsEntryAppliesToRuntime` 从真实 DevicesDock 的 incoming-offer settings 入口打开专用设置对话框，通过 Save 按钮应用同一 MainWindow 的 `TransferRuntimeComposition::incomingOffers()`；destination root、trusted auto-accept 和 default conflict policy 均无需重启即时更新，并从持久化 store 复读；
- A0 作为 owner build 唯一执行者在 `C:\Users\52323\AppData\Local\Temp\relaydesk-a3-r4-settings-runtime-evidence-a0` fresh 构建 282/282、退出 0；`ui009-logs` 中两个槽各三份日志均 3/0/0，完整 MainWindowLayout 19/0、TransferSettings 10/0，均退出 0；
- A7 最终只读复核 GO；A0 集成 fresh 构建 4/4，`ui009-integration-logs` 中两个槽分别 3/0/0、退出 0；
- `R4-UI-009` 因此只在 Qt localhost/offscreen production UI 范围为 `PASS`。该验证不证明 native Windows/macOS 窗口、系统文件选择器、TCC、物理 Win↔Mac 或发布行为。

详见 `product/docs/reports/R4_SETTINGS_RUNTIME.md`。

## 18. 证据边界

- `PASS` 只可来自当前 SHA 的定向测试或明确的运行证据；
- `NOT_RUN` 不阻断继续修复已确认的 `FAIL`；
- Windows/macOS hosted 构建不证明原生 UI 或物理交互；
- 物理 Win↔Mac、macOS TCC、Finder/Explorer 原生拖放和 tray/menu bar 继续独立验收。
