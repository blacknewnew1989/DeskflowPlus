# 18 共享接口契约

本文是 RelayDesk v1 跨模块、跨平台 C++ 接口的参考索引。公共头文件是唯一可编译契约；
本文解释类型所有权、组合方向和线程边界，不重复维护一套“等价”声明。

当前状态为 `PROTO-FREEZE-001` freeze candidate。只有
`product/docs/19_PROTOCOL_V1_FREEZE.md` 记录的 commit、tag 和双平台 Actions 证据全部填充后，
Windows 与 macOS 才能把这些接口视为已发布的 v1 基线。

## 1. 命名空间与所有权

| 命名空间 | 责任 | 代表头文件 |
|---|---|---|
| `deskflow::relaydesk` | 设备、发现、配对、信任、重连、平台 adapter、应用组合 service | `device/*.h`、`discovery/*.h`、`pairing/*.h`、`platform/*.h`、`app/*.h` |
| `relaydesk::transfer` | RDFT wire、manifest、sender/receiver、恢复、冲突、进度与历史值类型 | `transfer/*.h` |

不得在 GUI、Windows 或 macOS 目录复制这些声明。平台代码必须 include 共享头文件，不能通过
同名 struct、整数错误码、`QString` policy 或 wire message 近似替代。

共享接口变更规则：

1. 由本表对应 owner 创建独立任务提交；
2. 同一提交更新调用者、定向测试和本文件；
3. A0 合入 `product/relaydesk-v1` 后，A4/A5 从同一 commit 消费；
4. 已发布 v1 wire 不兼容变更必须升 major version，不能移动既有 tag；
5. 公共 QObject signal 参数必须是已注册、可复制的不可变值。

## 2. 强类型标识

| 类型 | Authority | Owner |
|---|---|---|
| `deskflow::relaydesk::DeviceId` | `src/lib/relaydesk/device/DeviceId.h` | A2 |
| `relaydesk::transfer::TransferId` | `src/lib/relaydesk/transfer/TransferId.h` | A6 |
| `relaydesk::transfer::FileId` | `src/lib/relaydesk/transfer/FileId.h` | A6 |

三个类型具有相同的表示规则，但不能相互转换：

- 只能通过 `generate()`、`fromBytes()` 或 `fromString()` 获得；
- 没有默认构造，也不接受隐式 `QUuid`；
- null UUID、非 16-byte wire 值和非 canonical UUID 文本会被拒绝；
- wire 使用 16 bytes，配置/JSON 使用 36 字符无花括号 UUID；
- 提供 `toBytes()`、`toString()`、`value()`、`qHash` 和 `QMetaType`；
- IP 地址、display name、Deskflow screen name 和 pairing session ID 都不是设备/传输/文件 ID。

`QUuid` 仍可用于短生命周期的 pairing session、冲突 reservation 等局部标识，但不得替代上述
三种业务 ID。失败由 typed result 表示，不使用 null ID sentinel。

## 3. Device、discovery 与 endpoint

### 3.1 DeviceInfo 与 DeviceSnapshot

Authority：

- `src/lib/relaydesk/device/DeviceInfo.h`
- `src/lib/relaydesk/device/DeviceSnapshot.h`

`DeviceInfo` 是 discovery wire 中的设备自报信息，包含 `deviceId`、名称、平台、架构、版本、
input/file port、`DeviceCapabilities` 和自报证书摘要。`DeviceSnapshot` 是 registry 发布给 UI 的
不可变视图，增加 alias、presence、trust、latency、地址、pinned fingerprint 和 last-seen。

边界规则：

- registry 按 `DeviceId` 去重；同名设备不合并，IP 不作为 identity；
- display name 和远端 diagnostic 不可信，GUI 只能以纯文本显示；
- discovery 自报 fingerprint 不是 pinned trust，配对完成前不得写入
  `DeviceSnapshot::pinnedFingerprint`；
- UI 不持有或修改 service 内部对象，只接收 snapshot copy；
- capability bool 是已观察/已发布事实，不是用户授权或隐式信任。

### 3.2 Discovery 分层

| 层 | Authority | 责任 |
|---|---|---|
| 用户设置 | `DiscoverySettings.h` | `relaydesk/discovery/*` QSettings、手动 host/port、迁移 |
| UDP transport | `DiscoveryService.h` | bind、定向广播、bounded receive、datagram diagnostics |
| presence registry | `DiscoveryRegistry.h` | DeviceId 去重、地址排序、TTL/offline |
| 地址候选 | `AddressCandidateProvider.h` | recent → discovered → manual、异步 DNS、去重 |
| 应用组合 | `DeviceDiscoveryRuntime.h` | owning-thread start/stop、service → registry → model |

`DiscoverySettings` 与 `DiscoveryServiceSettings` 不是重复类型：前者是用户持久配置，后者只控制
UDP transport。文件 endpoint 使用 `FileEndpointAnnouncement` 一次性传递 port/file/folder/resume
事实；禁止重新引入 `(port, bool, bool)` 位置参数。

`FileEndpointAnnouncement::disabled()` 撤销发布。非 disabled 值必须有非零 port 且 `fileV1=true`；
folder/resume 只能在相应 handler 已组合并由测试证明可执行后发布。

## 4. Pairing、trust 与 reconnect

### 4.1 唯一 pairing facade

Authority：

- `src/lib/relaydesk/pairing/IPairingService.h`
- `src/lib/relaydesk/pairing/PairingOperation.h`
- `src/lib/relaydesk/pairing/PairingStateMachine.h`
- `src/lib/relaydesk/app/PairingTrustRuntime.h`

`IPairingService` 是 GUI 唯一生产入口。它接收 `DeviceId`、session `QUuid` 和本地用户输入，返回
`PairingOperationResult`，并发布 `PairingSnapshot`。`PairingTrustRuntime` 是唯一生产实现：它从
fresh discovery snapshot 解析 endpoint 与 advertised identity evidence，并在 manager 完成 endpoint、
session、device 和实际 peer fingerprint 绑定后提交 trust。

内部 UDP `PairingService` 不是 GUI API；`PairingWizardModel` 只能 bind `IPairingService`，不得在
生产路径直接驱动 `PairingStateMachine`。六位 SAS 只供两端用户比较，不是网络认证密钥。

### 4.2 Reconnect 的认证结果

Authority：`src/lib/relaydesk/reconnect/AutoReconnectCoordinator.h`。

`AutoReconnectRequest` 只包含目标 `DeviceId`、地址来源和端口，不接收 caller 自报 fingerprint。
`Connector` 必须在 TLS/HELLO 完成后返回 `AutoReconnectConnectResult`，其中
`AuthenticatedReconnectPeer` 来自实际握手的 DeviceId 与证书 SHA-256。coordinator 再用
`TlsPeerPinningPolicy` 核对 trusted store；discovery 数据不能替代握手身份。

候选顺序固定为 recent-successful → discovered → manual，retry delay 使用
`std::chrono::milliseconds`。该模块在产品组合根接线前仍为 `NOT_WIRED`，接口冻结不等于运行时已启用。

## 5. RDFT wire 与 transfer 值类型

Wire authority：

- `src/lib/relaydesk/transfer/ProtocolMessageRegistry.def`
- `src/lib/relaydesk/transfer/Protocol.h`
- `product/spec/protocol/messages.cddl`
- `product/spec/protocol/test-vectors.json`
- `product/docs/05_FILE_TRANSFER_PROTOCOL.md`
- `product/docs/19_PROTOCOL_V1_FREEZE.md`

所有平台使用同一 24-message registry、32-byte big-endian frame 和 60 个正负向量。GUI、平台
adapter 和应用组合层不得构造 CBOR、解释 raw `Frame` 或复制 codec。

Manifest authority 为 `TransferTypes.h`、`ManifestBuilder.h` 和 `ManifestPageCodec.h`：

- `ManifestEntry` 持有强 `FileId`、规范相对路径、类型、大小、UTC mtime、SHA-256 与 flags；
- `TransferManifestSummary` 只持有总量和 canonical digest；
- sender-only `PreparedManifestEntry::canonicalSourcePath` 不进入 wire digest；
- canonical digest 排除本地绝对路径、display name 和 transfer ID；
- UI snapshot 不复制完整 manifest，分页详情由 service 提供。

`IncomingOffer` 保留经过共享 codec 验证的 `TransferOffer`，以及 peer DeviceId/display/trust facts。
它不是让 GUI 重建 wire 的许可。普通 accept/reject intent 只能携带强 TransferId 与 service options。
`AcceptanceOrigin` 区分用户决定与可信设备策略；`TransferAccept::autoAccepted` 只由 service/state-machine
在 wire 边界从该 enum 映射，不能作为跨层业务 bool。

## 6. IFileTransferService 与应用边界

Authority：

- `src/lib/relaydesk/transfer/IFileTransferService.h`
- `src/lib/relaydesk/transfer/TransferTypes.h`
- `src/lib/relaydesk/app/FileTransferRuntime.h`
- `src/lib/relaydesk/app/TransferUiRuntime.h`

`IFileTransferService` 是 GUI 与产品组合层使用的唯一文件传输业务接口。其规则为：

- `send(DeviceId, QList<QUrl>, SendOptions)` 返回 `TransferStartResult`；失败不返回 null ID；
- `accept` 使用 `TransferId + ReceiveOptions`；
- `reject` 使用 `TransferId + RejectReason`；
- pause/resume/retry 只使用 `TransferId`；
- cancel 使用 `TransferId + TransferCancelOptions`；其中 `TransferCancelReason` 与
  `PartialDisposition` 都是 typed enum，wire 的 reason/keep-partial 只在 service codec 边界映射；
- active transfer 与 incoming/added/changed/removed signals 只传已注册不可变值；
- GUI signal 不得携带 `Frame`、`TransferAccept`、`TransferReject`、socket、路径解析器或 TLS 对象。

`TransferUiRuntime` 是 typed UI intent adapter，不是第二个 transfer service。产品组合根负责把每个
UI intent 恰好一次调用到 `IFileTransferService`，并把 service offer/snapshot 恰好一次送回 model。
完成文件/目录的打开动作可留在 UI adapter，但必须通过注入的 resolver/opener，并重新验证接收根、
存在性与 canonical containment；它不写 transfer history。

`FileTransferRuntime` 实现 `IFileTransferService`，拥有独立于 Deskflow input channel 的 RDFT
listener/client、连接认证、capability negotiation 和应用线程生命周期。raw protocol frame 是内部
路由细节，不能作为 GUI 或平台代理业务入口。

## 7. Sender、receiver、resume、conflict 与 backpressure

### 7.1 唯一发送边界

Authority：

- `src/lib/relaydesk/transfer/TransferSender.h`
- `src/lib/relaydesk/filetransport/FileTlsFrameSink.h`

`TransferSender::nextFrame()` 是 worker-side bounded pull：一次最多读取一个 chunk、产生一个 frame。
`TransferSenderPump` 是唯一 high/low-water 协调器；`TransferFrameSink` 是唯一 submit/queued-bytes
接口；`FileTlsFrameSink` 只把已经生成的 frame 交给 `FileTlsConnection`。

高水位期间不得继续读取/哈希源文件；backpressured frame 必须原样保留并先重试。QSslSocket callback
不得调用 sender、扫描目录或哈希。应用运行时不得另建私有 sink、水位或轮询算法。

### 7.2 接收、恢复与冲突

| Boundary | Authority | Freeze status |
|---|---|---|
| `.part` receive/SHA/commit core | `FileReceiver.h` | shared core implemented; runtime `NOT_WIRED` |
| resume state | `ResumeStore.h` | shared core implemented; runtime `NOT_WIRED` |
| resume wire plan | `ResumeMessageCodec.h` | wire frozen; runtime `NOT_WIRED` |
| conflict decision/reservation | `ConflictResolver.h` | shared core implemented; receiver composition `NOT_WIRED` |
| platform root/link/atomic commit | `IPlatformFileSafety.h` | interface frozen; platform adapters `NOT_IMPLEMENTED` |

`PathPolicy` 负责 protocol path、NFC/case-fold collision key 和 lexical containment；
`IPlatformFileSafety` 在 disk worker 上负责真实 root、link/reparse traversal 与最终 atomic move/replace。
`CommitStagedFileRequest` 使用具名 receiveRoot/stagingPath/destinationPath 和
`CommitDisposition`，不接受含义不明的 bool。

在实际组合前，`FileReceiver` 不得宣告已消费 `ConflictResolver` 或 `IPlatformFileSafety`。缺失或损坏
resume state、part size/hash 不匹配时不能盲续；成功必须在 SHA-256、flush/close、冲突决策和原子
commit 全部完成后才发送 OK。

## 8. TransferSnapshot、progress 与 history

Authority：

- `TransferControlStateMachine.h`
- `TransferProgressPublisher.h`
- `TransferHistoryStore.h`

`TransferSnapshot` 是 live immutable view；`TransferHistoryRecord` 是持久 schema，两者不能互换。
共同规则：

- `Interrupted` 与 `Failed` 不同，terminal state 不回退；
- total 不得回归或溢出，0-byte transfer 仍有确定性完成态；
- speed/ETA 由 service 的 `TransferProgressPublisher` 计算，UI 不重复实现；
-普通更新最多每 200 ms 发布一次，状态跃迁和终态立即发布；paused/interrupted 时间不计入速度；
- current path 只是安全显示值，不参与落盘；diagnostic 不直接显示；
- history retry intent 只携带 TransferId，durable retry recipe 属于 service/store；
- history store 的同步文件操作必须由 disk worker 调用。

## 9. Permission 与平台文件安全

### 9.1 Permission

Authority：

- `src/lib/relaydesk/platform/PermissionSnapshot.h`
- `src/lib/relaydesk/platform/IPlatformPermissions.h`

`PermissionProbeEntry::errorCode` 是稳定 `PermissionErrorCode`，原生诊断只用于日志。
`IPlatformPermissions` 冻结共同的 `current()` 与 typed `openSystemSettings(PermissionKind)`；Windows
firewall/listening probe 和 macOS privacy probe 的 refresh 输入不同，保留在 concrete adapter，不能为了
表面一致伪造无信息的通用 refresh API。

UI 仅在 `canOpenSettings=true` 时显示跳转动作。`Unavailable`/`Unknown` 不能映射为 Granted。

### 9.2 File safety

`IPlatformFileSafety` 是 worker-side 非 QObject 接口：`verifyReceiveRoot`、
`verifyNoLinkTraversal` 和 `commitStagedFile` 均接收具名 request 并返回 `FileSafetyResult`。
当前冻结只证明接口可编译；Windows reparse/atomic replace 与 macOS symlink/rename adapter 尚未实现，
因此是 `NOT_IMPLEMENTED`，不是 `PASS`。

Windows/macOS 只实现 adapter，不各自实现 transfer 状态机、PathPolicy、ConflictResolver 或 codec。

## 10. Error contract

RelayDesk v1 不定义一个包办所有层的 `ProductError`。稳定错误由实际 boundary 的 enum 管理，例如：

- codec/frame：各 `*CodecError`、`FrameError`；
- pairing：`PairingOperationError` 及嵌套 message/state/trust error；
- reconnect/pinning：`AutoReconnectConnectError`、`PeerPinningError`；
- service start/control：`TransferStartError`、`RejectReason`、typed cancel options；
- platform：`PermissionErrorCode`、`PermissionOpenError`、`FileSafetyError`；
- receiver/resume/conflict/history：对应共享 header 中的稳定 enum。

错误规则：

- wire enum 数值与持久 schema 值由测试冻结；
- service result 同时提供 typed error 和仅供日志的 diagnostic；
- UI 根据稳定 code 映射 ProductStrings，不能显示远端或平台 diagnostic 原文；
- retryability 由具体 boundary/state 决定，不用一个全局 bool 覆盖所有层；
- 不用裸 `int reasonCode`、空 ID、空 catch 或单一 `QString error` 表示领域失败。

## 11. Settings

产品设置使用 `relaydesk/...` namespace。每个 owner 负责默认值、范围、schema version 和一次迁移；
service 接收不可变 settings snapshot。已存在的 discovery user settings 与 UDP service settings 按第 3 节
分层。稳定 DeviceIdentity、trust、resume 和 history 不得被普通 GUI “reset settings” 误删。

私钥和真实签名凭据不存入普通 QSettings 或仓库。

## 12. Qt queued values 与线程所有权

以下跨 QObject/线程边界使用的值必须保持 copyable 且有 `Q_DECLARE_METATYPE`：DeviceId、
TransferId、FileId、DeviceSnapshot、IncomingOffer、TransferSnapshot、TransferHistoryRecord、
SendOptions、ReceiveOptions、PermissionSnapshot 及 service options/results。

冻结测试必须包含真实 `Qt::QueuedConnection` 跨 `QThread` smoke，而不只检查
`QMetaType::isValid()`。

线程边界：

- GUI/model、QObject service、QUdpSocket/QSslSocket 留在 owning event-loop thread；
- socket callback 只做 bounded parse、状态推进和 immutable value dispatch；
- scan、QFile read/write、SHA-256、resume/history/trust persistence 在有界 worker；
- worker 不直接访问 QSslSocket；网络线程不等待长磁盘操作；
- completion 通过 queued immutable result 回到 owning thread。

## 13. Freeze candidate 组合状态

| Product path | Status | 说明 |
|---|---|---|
| discovery → registry → DeviceHomeModel | `WIRED` | `DeviceDiscoveryRuntime` 已在 MainWindow 组合 |
| pairing GUI → IPairingService → trust | `WIRED` | `PairingTrustRuntime` 是唯一 facade |
| AutoReconnectCoordinator | `NOT_WIRED` | 接口/测试存在，MainWindow 尚未拥有 |
| GUI transfer intents → IFileTransferService | `NOT_WIRED` | typed adapter/service 存在，组合根尚未连接 |
| outgoing manifest/offer/sender core | `IMPLEMENTED` | runtime slice 已有定向 E2E；产品入口尚未组合 |
| incoming receiver/resume/conflict/history/progress | `NOT_WIRED` | 共享 core 存在，FileTransferRuntime 尚未组合 |
| Windows permission → UI model | `NOT_WIRED` | adapter 已实现，MainWindow 尚未创建 |
| IPlatformFileSafety Windows/macOS implementations | `NOT_IMPLEMENTED` | 只冻结共同接口 |

`IMPLEMENTED`/`WIRED` 不等于 Win↔Mac 真机 `PASS`；不能执行的双机、系统授权和签名项必须在阶段
报告中单独标记 `NOT_RUN`。接口冻结标签的双平台编译/向量测试也不能代替最终网络、磁盘和 UI 验收。
