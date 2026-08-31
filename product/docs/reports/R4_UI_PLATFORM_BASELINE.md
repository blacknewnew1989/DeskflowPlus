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
| R4-UI-005 | Incoming Offer / Ask | accept/reject/conflict decision | IncomingOfferModel / TransferUiRuntime -> FileTransferRuntime | safety/prompt 有反馈；真实 TLS 组合未运行 | `NOT_RUN` |
| R4-UI-006 | 传输中心 | pause/resume/cancel/retry | TransferCenterModel -> TransferUiRuntime -> FileTransferRuntime | snapshot 可显示；一般 operation rejection 未运行 | `NOT_RUN` |
| R4-UI-007 | 迷你条 | primary action、details | TransferCenterModel typed intents；details 打开 TransferCenterDock | 真实后台传输刷新未运行 | `NOT_RUN` |
| R4-UI-008 | 历史/打开位置 | openFile/openFolder/history retry | TransferHistoryRuntime / TransferUiRuntime validated resolver / QDesktopServices | completionOpenRejected、historyError 无 production UI receiver | `FAIL` |
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
| Incoming Offer | IncomingOfferModel、DevicesDock、IncomingTransferRuntime | accept/reject/Ask typed flow | 真实 TLS offer 到可视面板 |
| 传输中心/迷你条 | TransferCenterModel/Dock、TransferMiniBar | 控制 intent、状态/ETA 映射 | 真实传输期间原生交互 |
| 历史/打开 | TransferUiRuntime、TransferRuntimeComposition | receive-root 路径校验、可注入 opener | OS shell 实际打开与用户失败提示 |
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

未新增视觉组件、翻译键、协议或 service API。`R4-UI-008` 保留为下一独立 FAIL；托盘、权限、
七语言和打包没有在本切片展开。

## 5. 证据边界

- `PASS` 只可来自当前 SHA 的定向测试或明确的运行证据；
- `NOT_RUN` 不阻断继续修复已确认的 `FAIL`；
- Windows/macOS hosted 构建不证明原生 UI 或物理交互；
- 物理 Win↔Mac、macOS TCC、Finder/Explorer 原生拖放和 tray/menu bar 继续独立验收。
