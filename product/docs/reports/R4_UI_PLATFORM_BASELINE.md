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
| R4-UI-001 | 设备卡、信任、手动地址 | DevicesDock pairing/revoke/autoAccept/manual save | MainWindow -> PairingTrustRuntime / DiscoverySettingsStore / DeviceDiscoveryRuntime | auto-accept 可见；pair/revoke 即时失败仅日志 | `NOT_RUN` |
| R4-UI-002 | 配对 | pairingRequested、SAS confirm/cancel | PairingTrustRuntime -> DiscoveryService datagram -> trust store | 状态模型存在；组合根原生手势未运行 | `NOT_RUN` |
| R4-UI-003 | 权限 | PermissionStatusModel openSettingsRequested | Windows/Mac permission probe 与原生 settings opener | banner/detail model 存在；TCC/系统往返未运行 | `NOT_RUN` |
| R4-UI-004 | 拖放/选择发送 | DevicesDock sendItemsRequested | TransferUiRuntime -> IFileTransferService::send -> FileTransferRuntime | typed start failure 写回现有本地化反馈 | `PASS` |
| R4-UI-005 | Incoming Offer / Ask | accept/reject/conflict decision | IncomingOfferModel / TransferUiRuntime -> FileTransferRuntime | 真实 TLS offer 经 production widget accept/reject 完成或拒绝 | `PASS` |
| R4-UI-006 | 传输中心 | pause/resume/cancel/retry | TransferCenterModel -> TransferUiRuntime -> FileTransferRuntime | snapshot 可显示；一般 operation rejection 未运行 | `NOT_RUN` |
| R4-UI-007 | 迷你条 | primary action、details | TransferCenterModel typed intents；details 打开 TransferCenterDock | 真实后台传输刷新未运行 | `NOT_RUN` |
| R4-UI-008 | 历史/打开位置 | openFile/openFolder/history retry | TransferHistoryRuntime / TransferUiRuntime validated resolver / QDesktopServices | opener 拒绝与 history load/persist error 写入本地化非模态反馈 | `PASS` |
| R4-UI-009 | 设置 | transferSettingsSaved | TransferSettingsStore -> MainWindow composition snapshot | 保存失败有 QMessageBox；原生交互未运行 | `NOT_RUN` |
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
| 设备卡/配对 | DeviceHomeModel、DevicesDock、PairingWizardModel、PairingTrustRuntime | model、typed intent、UDP/trust 组件 | MainWindow 原生手势、物理配对 |
| 权限 | PermissionStatusModel、DevicesDock、MainWindowLayout | 门控、文案、控件绑定 | TCC、Windows/macOS 系统设置往返 |
| 拖放发送 | DevicesDock、TransferUiRuntime | local URL 过滤、immutable intent、fake service adapter | Explorer/Finder DnD、真实 service 失败反馈 |
| Incoming Offer | IncomingOfferModel、DevicesDock、TransferRuntimeComposition、FileTransferRuntime | localhost TLS offer、widget accept/reject、文件 SHA/row/清理 | Ask、原生窗口系统、物理双机 |
| 传输中心/迷你条 | TransferCenterModel/Dock、TransferMiniBar | 控制 intent、状态/ETA 映射 | 真实传输期间原生交互 |
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

## 7. 证据边界

- `PASS` 只可来自当前 SHA 的定向测试或明确的运行证据；
- `NOT_RUN` 不阻断继续修复已确认的 `FAIL`；
- Windows/macOS hosted 构建不证明原生 UI 或物理交互；
- 物理 Win↔Mac、macOS TCC、Finder/Explorer 原生拖放和 tray/menu bar 继续独立验收。
