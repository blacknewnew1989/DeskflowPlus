# 19 RDFT v1 Protocol Freeze Index

本文是 RelayDesk RDFT v1 的冻结索引。它把 wire、共享 codec、验证入口和当前共享
接口边界绑定到同一个 Git commit，但不复制各文件中的 schema 或 C++ 声明。

当前状态为 **FROZEN**。下表绑定唯一 tag commit、同一 commit 的双平台 Actions run
以及 GitHub artifact 原始 ZIP 的 SHA-256；既有冻结 tag 不得移动。

## 1. 冻结身份与发布占位

| Field | Value |
|---|---|
| `authoritativeCommit` | `0d091d301aea2140387fdd615150984dfed5bc08` |
| `freezeTagPattern` | `relaydesk-protocol-v1-*` |
| `freezeTag` | `relaydesk-protocol-v1-20260813-01` |
| `canonicalActionsRun` | `31672497950` |
| `windowsArtifact` | `relaydesk-windows-x64-0d091d301aea2140387fdd615150984dfed5bc08` (ID `9170492840`, unsigned) |
| `windowsArtifactSha256` | `bf435935c748bc57ea1e7f5913a01dc47467bcaec61040068e630c6d7b54b5d0` |
| `macosArtifact` | `relaydesk-macos-arm64-0d091d301aea2140387fdd615150984dfed5bc08` (ID `9170386546`, ad-hoc) |
| `macosArtifactSha256` | `04ba64d9ebd49c4655871fc29005fbbc37d641b19bc2029439baa447a0567887` |

`authoritativeCommit` 最终必须是完整 40 位 Git SHA。`freezeTag` 必须匹配
`relaydesk-protocol-v1-*`，并直接指向该 commit；不得移动既有冻结 tag。Actions run
必须由该 tag 或同一 commit 触发，artifact 记录必须包含平台、签名状态和 SHA-256。

## 2. Wire 冻结面

| Machine field | Frozen value | Authority |
|---|---:|---|
| `registryMessageTypes` | `24` | [`ProtocolMessageRegistry.def`](../../src/lib/relaydesk/transfer/ProtocolMessageRegistry.def) |
| `sharedJsonVectors` | `60` | [`test-vectors.json`](../spec/protocol/test-vectors.json) |
| `fixedHeaderBytes` | `32` | [`Protocol.h`](../../src/lib/relaydesk/transfer/Protocol.h) 与 `test-vectors.json` |

完整 wire 语义见 [`05_FILE_TRANSFER_PROTOCOL.md`](05_FILE_TRANSFER_PROTOCOL.md)。冻结
材料还包括：

- [`messages.cddl`](../spec/protocol/messages.cddl)：metadata CBOR map schema；
- [`ProtocolMessageRegistry.h`](../../src/lib/relaydesk/transfer/ProtocolMessageRegistry.h)
  与 [`ProtocolMessageRegistry.cpp`](../../src/lib/relaydesk/transfer/ProtocolMessageRegistry.cpp)：
  单一 registry 查询和 envelope 规则；
- [`FrameCodec.h`](../../src/lib/relaydesk/transfer/FrameCodec.h) 与
  [`FrameCodec.cpp`](../../src/lib/relaydesk/transfer/FrameCodec.cpp)：32-byte frame
  canonical encode 和增量 decode；
- [`file-transfer-v1.md`](../spec/protocol/file-transfer-v1.md)：共享状态语义。

24 个 message type、60 个 JSON vector 和 32-byte envelope 是一个整体。仅修改文档、
仅增加平台 adapter，或仅让一端接受新 bytes，都不构成新的冻结契约。

## 3. Codec 与测试目标

| Wire family | Shared codec | Direct Qt test target |
|---|---|---|
| Envelope / registry / full vectors | `FrameCodec`, `ProtocolMessageRegistry` | `RelayDeskFrameCodecTests`, `RelayDeskProtocolMessageRegistryTests`, `RelayDeskProtocolVectorTests` |
| Session | `SessionMessageCodec` | `RelayDeskSessionMessageCodecTests` |
| Capability | `CapabilityCodec` | `RelayDeskCapabilityCodecTests` |
| Control | `ControlMessageCodec` | `RelayDeskControlMessageCodecTests` |
| Manifest | `ManifestPageCodec` | `RelayDeskManifestPageCodecTests` |
| File | `FileMessageCodec` | `RelayDeskFileMessageCodecTests` |
| Transfer command | `TransferCommandCodec` | `RelayDeskTransferCommandCodecTests` |
| Transfer completion | `TransferCompletionCodec` | `RelayDeskTransferCompletionCodecTests` |
| Resume | `ResumeMessageCodec` | `RelayDeskResumeMessageCodecTests` |

`RelayDeskProtocolVectorTests` 验证 typed metadata canonical re-encode 和完整 frame bytes；
本索引的 Python 校验只核对材料数量、引用和 workflow trigger，不重复 C++ codec 测试。

## 4. 当前共享 interface headers

下表是最终 freeze candidate 中需要由 Windows 与 macOS 共同消费的 header 索引。
下表只索引真实、可编译的 authoritative headers；不以文档示例替代声明。UI、service、
运行时、queued values、sender/receiver、恢复、冲突和平台 boundary 均已在冻结测试中核对。

| Boundary | Current shared headers |
|---|---|
| Device | [`DeviceId.h`](../../src/lib/relaydesk/device/DeviceId.h), [`DeviceInfo.h`](../../src/lib/relaydesk/device/DeviceInfo.h), [`DeviceSnapshot.h`](../../src/lib/relaydesk/device/DeviceSnapshot.h) |
| Discovery | [`DiscoveryService.h`](../../src/lib/relaydesk/discovery/DiscoveryService.h), [`DiscoveryRegistry.h`](../../src/lib/relaydesk/discovery/DiscoveryRegistry.h), [`DiscoverySettings.h`](../../src/lib/relaydesk/discovery/DiscoverySettings.h), [`AddressCandidateProvider.h`](../../src/lib/relaydesk/discovery/AddressCandidateProvider.h), [`FileEndpointAnnouncement.h`](../../src/lib/relaydesk/discovery/FileEndpointAnnouncement.h) |
| Pairing | [`PairingOperation.h`](../../src/lib/relaydesk/pairing/PairingOperation.h), [`PairingMessageCodec.h`](../../src/lib/relaydesk/pairing/PairingMessageCodec.h), [`PairingStateMachine.h`](../../src/lib/relaydesk/pairing/PairingStateMachine.h), [`IPairingService.h`](../../src/lib/relaydesk/pairing/IPairingService.h), [`PairingTrustRuntime.h`](../../src/lib/relaydesk/app/PairingTrustRuntime.h) |
| Reconnect | [`AutoReconnectCoordinator.h`](../../src/lib/relaydesk/reconnect/AutoReconnectCoordinator.h) |
| Permission | [`PermissionSnapshot.h`](../../src/lib/relaydesk/platform/PermissionSnapshot.h), [`IPlatformPermissions.h`](../../src/lib/relaydesk/platform/IPlatformPermissions.h) |
| File identity | [`TransferId.h`](../../src/lib/relaydesk/transfer/TransferId.h), [`FileId.h`](../../src/lib/relaydesk/transfer/FileId.h) |
| File wire/capability | [`Protocol.h`](../../src/lib/relaydesk/transfer/Protocol.h), [`CapabilityCodec.h`](../../src/lib/relaydesk/transfer/CapabilityCodec.h), [`SessionMessageCodec.h`](../../src/lib/relaydesk/transfer/SessionMessageCodec.h) 及第 3 节列出的 codec headers |
| File service/runtime | [`TransferTypes.h`](../../src/lib/relaydesk/transfer/TransferTypes.h), [`TransferError.h`](../../src/lib/relaydesk/transfer/TransferError.h), [`IFileTransferService.h`](../../src/lib/relaydesk/transfer/IFileTransferService.h), [`FileTransferRuntime.h`](../../src/lib/relaydesk/app/FileTransferRuntime.h), [`TransferUiRuntime.h`](../../src/lib/relaydesk/app/TransferUiRuntime.h) |
| File live/history | [`TransferControlStateMachine.h`](../../src/lib/relaydesk/transfer/TransferControlStateMachine.h), [`TransferProgressPublisher.h`](../../src/lib/relaydesk/transfer/TransferProgressPublisher.h), [`TransferHistoryStore.h`](../../src/lib/relaydesk/transfer/TransferHistoryStore.h) |
| File sender/transport | [`TransferSender.h`](../../src/lib/relaydesk/transfer/TransferSender.h), [`FileTlsFrameSink.h`](../../src/lib/relaydesk/filetransport/FileTlsFrameSink.h), [`FileTlsTransport.h`](../../src/lib/relaydesk/filetransport/FileTlsTransport.h) |
| File receiver/recovery | [`FileReceiver.h`](../../src/lib/relaydesk/transfer/FileReceiver.h), [`ResumeStore.h`](../../src/lib/relaydesk/transfer/ResumeStore.h), [`ResumeMessageCodec.h`](../../src/lib/relaydesk/transfer/ResumeMessageCodec.h), [`ConflictResolver.h`](../../src/lib/relaydesk/transfer/ConflictResolver.h) |
| File safety | [`IPlatformFileSafety.h`](../../src/lib/relaydesk/platform/IPlatformFileSafety.h) |

后续接口冻结提交应更新本索引，而不是在 Windows/macOS 各建一份镜像声明。

## 5. Windows 与 macOS 同一 commit 规则

- 两个平台必须 checkout 同一个 `authoritativeCommit`，并消费同一份
  `ProtocolMessageRegistry.def`、`messages.cddl` 和 `test-vectors.json`。
- 两个平台必须通过唯一 workflow `.github/workflows/relaydesk-build.yml` 构建；
  `product/templates/github/workflows/relaydesk-build.yml` 必须与其逐 byte 相同。
- Windows 与 macOS 的 `RelayDeskProtocolVectorTests` 必须读取相同 60 个 frozen
  vectors。平台代码不得重建 CBOR map、复制 registry 或维护“等价”向量。
- artifact manifest 中的 commit 必须等于 `authoritativeCommit`。Windows unsigned
  与 macOS ad-hoc/unsigned 内部包可以作为证据，但必须准确标注签名状态。
- 单平台 PASS、不同 commit 的两个 PASS，或未记录 artifact digest，都不能把本索引
  从 freeze candidate 提升为已冻结。

## 6. 当前 `NOT_WIRED` 清单

`NOT_WIRED` 表示实现或接口存在，但产品启动路径尚未完整拥有并连接它；它与真机尚未
执行的 `NOT_RUN` 不同。下表只描述当前 commit 可从生产 composition 代码核实的边界。

| Product path | Status | Current boundary evidence |
|---|---|---|
| Trust/discovery -> automatic reconnect | `NOT_WIRED` | `AutoReconnectCoordinator` 有共享实现和测试，但 `MainWindow` 未创建或驱动它 |
| Windows firewall permission probe -> permission model | `NOT_WIRED` | `WindowsFirewallProbe` 实现 `IPlatformPermissions`，但当前 `MainWindow` 只在 macOS 创建 `MacPermissionProbe` |
| GUI send/control intents -> `IFileTransferService` | `NOT_WIRED` | `TransferUiRuntime` 与 `FileTransferRuntime` 存在，但 `MainWindow` 未拥有或连接二者 |
| Trusted discovery -> dedicated file TLS runtime | `NOT_WIRED` | `FileTransferRuntime` 可拥有 listener/client，但产品启动路径未创建它；在 receiver 组合前 discovery endpoint disabled |
| Incoming offer -> receive/accept/reject/resume/commit | `NOT_WIRED` | 当前 runtime 不声明 `file.receive.v1`，收到未协商 offer会断开；`accept()`/`reject()` 只发布 typed unknown-transfer result |
| `FileReceiver` staged commit -> `IPlatformFileSafety` | `NOT_WIRED` | 共享 file-safety contract 与 contract test 已存在；其 header 明确标注 receiver 接线仍未完成 |
| Transfer progress/history -> Transfer Center | `NOT_WIRED` | model/dock 和共享 publisher/history store 存在，但产品启动路径没有 file service 向其发布状态 |

当前发现与配对/trust composition 已由 `MainWindow` 创建，不应继续沿用早期审计中针对旧
baseline 的 `NOT_WIRED` 结论。真实 Windows↔macOS 发现、配对、重连和传输仍需在最终
双机验收中以 `PASS` 或 `NOT_RUN` 记录，组件测试不能代替该证据。

## 7. 变更规则

1. Freeze candidate 期间的 wire 变更必须在同一共享提交中更新 registry、`Protocol.h`、
   codec、CDDL、JSON vectors、C++ tests、`05_FILE_TRANSFER_PROTOCOL.md` 和本索引。
2. 已发布 tag 下的 message value、field 语义、flags、32-byte envelope 或 canonical bytes
   不得原地改变；不兼容变更必须升 major version。
3. Control family 的 unknown non-negative integer extension-key 行为以共享 codec 为准；
   不得推广到其他 codec family，也不得把未覆盖行为误报为 JSON vector contract。
4. 共享 interface header 变更必须先由 owner 提交，再由 Windows/macOS 从同一 commit
   消费；strong ID、typed service/UI intents、stable errors、queued values 和 file safety
   以表中真实 header 与 `RelayDeskSharedInterfaceFreezeTests` 为准。
5. 每次新的 freeze candidate 使用新的 `relaydesk-protocol-v1-*` tag，不移动、覆盖或
   force-update 既有 tag。

## 8. A0 最终证据核对

A0 已在 tag 和双平台 run 完成后填写第 1 节，并核对：

- tag 指向完整 `authoritativeCommit`；
- canonical/template workflows 一致且 tag trigger 生效；
- Windows 与 macOS jobs 均对应同一 commit；
- 每个平台的测试结果、artifact ID/name、签名状态和 SHA-256 可追溯；
- 所有未执行真机项明确为 `NOT_RUN`，所有未完成 composition 明确为 `NOT_WIRED`。

以上证据均来自 tag 触发的 canonical run；Windows 84/84、macOS 85/85 CTest 通过，
Windows MSI 生命周期与 macOS 安装生命周期任务均通过。真实 Windows↔macOS 双机传输
仍为 `NOT_RUN`，不属于协议字节与共享接口冻结证据。
