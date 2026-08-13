# 05 RelayDesk File Transfer Protocol v1

| 项目 | 值 |
|---|---|
| 状态 | **v1 freeze candidate** |
| 协议标识 | `RDFT/1` |
| 传输 | TLS over TCP |
| 固定头整数 | unsigned, network byte order（big-endian） |
| metadata | CBOR，由共享 C++ codec 编解码 |
| file data | binary payload |
| 适用平台 | Windows x64 与 macOS Apple Silicon 使用同一组冻结 bytes |

本文是 RDFT v1 wire reference，不是独立于代码的第二份协议定义。凡本文、源码、
CDDL 或冻结向量没有明确规定的内容，都不得由实现者自行推断。

## 1. 冻结契约与权威来源

以下文件共同构成当前 freeze candidate：

| 责任 | 权威文件或 API |
|---|---|
| 24 个消息的值、分类、codec family、CDDL 名、测试目标、stream/content/flags 规则 | [`ProtocolMessageRegistry.def`](../../src/lib/relaydesk/transfer/ProtocolMessageRegistry.def) |
| magic、major version、32-byte 大小、flags、共享 wire 类型 | [`Protocol.h`](../../src/lib/relaydesk/transfer/Protocol.h) |
| registry 查询与 envelope 验证 | [`ProtocolMessageRegistry.h`](../../src/lib/relaydesk/transfer/ProtocolMessageRegistry.h)、`protocolMessageDescriptors()`、`protocolMessageDescriptor()`、`validateProtocolEnvelope()` |
| frame 编解码与长度检查 | [`FrameCodec.h`](../../src/lib/relaydesk/transfer/FrameCodec.h)、[`FrameCodec.cpp`](../../src/lib/relaydesk/transfer/FrameCodec.cpp) |
| CBOR map 结构 | [`messages.cddl`](../spec/protocol/messages.cddl) |
| codec 语义约束与 canonical encode | `src/lib/relaydesk/transfer/*Codec.{h,cpp}` |
| 状态语义 | [`file-transfer-v1.md`](../spec/protocol/file-transfer-v1.md) 及共享状态机实现 |
| 完整 bytes 与稳定负向结果 | [`test-vectors.json`](../spec/protocol/test-vectors.json) |
| registry/CDDL/vector 覆盖 | [`ProtocolMessageRegistryTests.cpp`](../../src/unittests/relaydesk/transfer/ProtocolMessageRegistryTests.cpp) |
| typed metadata 与 frame byte-for-byte round-trip | [`ProtocolVectorTests.cpp`](../../src/unittests/relaydesk/transfer/ProtocolVectorTests.cpp) |

`MessageType` enum 与 `kProtocolMessageTypeCount` 都由
`ProtocolMessageRegistry.def` 展开生成；`ProtocolMessageRegistry.cpp` 从同一文件生成
descriptor 数组。不得新增第二份手工消息 enum、平台专属编号表或平台专属 codec。

当前 registry 恰好有 24 个唯一条目，全部分类为 `Implemented`。数值空洞不是已分配
的 reserved range，未知值在 v1 中是协议错误。

## 2. 32-byte frame envelope

Wire 顺序固定为：

```text
[32-byte fixed header][metadataLength bytes CBOR][payloadLength bytes binary]
```

| Offset | Size | Field | Wire type | v1 约束 |
|---:|---:|---|---|---|
| 0 | 4 | `magic` | bytes | ASCII `RDFT` |
| 4 | 2 | `version` | `uint16` | `1` |
| 6 | 2 | `messageType` | `uint16` | 必须存在于单一 registry |
| 8 | 4 | `flags` | `uint32` | 必须精确匹配该 descriptor 的一个 flag set |
| 12 | 4 | `metadataLength` | `uint32` | 默认上限 1 MiB |
| 16 | 8 | `payloadLength` | `uint64` | 默认上限 4 MiB |
| 24 | 8 | `streamId` | `uint64` | 必须符合该 descriptor 的 `Zero`/`NonZero` 规则 |

所有 header 整数均为 unsigned big-endian。UUID 与 SHA-256 位于 CBOR metadata 内：
UUID 是 16-byte byte string，SHA-256 是 32-byte byte string；不得改用文本 UUID 或
平台原生结构体布局。

`ProtocolLimits` 的默认值为：

- `maxControlMetadataBytes = 1 MiB`；
- `maxDataPayloadBytes = 4 MiB`；
- `maxFrameBytes = 32 + 1 MiB + 4 MiB`。

`FrameCodec::tryDecode()` 是增量 parser：

- 少于完整 header 或完整 frame 时返回 `NeedMoreData`；
- `NeedMoreData` 与 `ProtocolError` 都不修改输入 buffer；
- `FrameReady` 只消费一个 frame，保留后续粘连 bytes；
- 在按远端长度分配内存前检查 metadata、payload、总 frame 上限、加法溢出和本地
  `qsizetype` 可表示性；
- envelope 合法性在 metadata codec 之前检查。

`FrameCodec::encode()` 也调用同一 `validateProtocolEnvelope()` 与长度检查，不能编码
一个 decoder 会因 envelope 规则拒绝的 frame。

## 3. Flags、stream 与 content rules

`Protocol.h` 只声明以下五个位：

| C++ symbol | Value | 含义 |
|---|---:|---|
| `AckRequired` | `0x00000001` | 请求对端产生协议规定的 acknowledgement |
| `Response` | `0x00000002` | response 方向 |
| `Final` | `0x00000004` | 对该消息所属范围的终结标记，具体范围见第 6 节 |
| `Retryable` | `0x00000008` | `Error` 的 retry hint；本地策略仍决定是否重试 |
| `CompressedMetadata` | `0x00000010` | 仅有常量；当前 24 个 descriptor 均不允许 |

Flags 是**精确集合**而不是可自由组合的 bitmask。任何额外位、遗漏位或未列出的组合
都返回 `InvalidFlags`。当前没有 `0x00000020` flag，也没有允许 metadata compression
的消息。

Registry content rules 的含义固定如下：

- `Zero`：`streamId` 必须为 0；
- `NonZero`：`streamId` 必须非 0；
- `Required`：对应区域长度必须大于 0；
- `Forbidden`：对应区域长度必须为 0。

所有 24 个消息都要求 metadata。只有 `FileChunk` 要求 binary payload；其他 23 个
消息都禁止 payload。

## 4. 单一 24-message registry

下表逐行镜像 `ProtocolMessageRegistry.def`。`Codec / CDDL` 中斜线前为
`ProtocolCodecFamily`，斜线后为 `messages.cddl` 的 schema 名。

| `MessageType` | Value | Codec / CDDL | Stream | Metadata | Payload | 允许的精确 flags |
|---|---:|---|---|---|---|---|
| `Hello` | `0x0001` | `Session` / `hello` | `Zero` | `Required` | `Forbidden` | `AckRequired` |
| `AuthResult` | `0x0002` | `Session` / `auth-result` | `Zero` | `Required` | `Forbidden` | `Response` |
| `Capabilities` | `0x0003` | `Capability` / `capabilities` | `Zero` | `Required` | `Forbidden` | `0` |
| `Heartbeat` | `0x0004` | `Session` / `heartbeat` | `Zero` | `Required` | `Forbidden` | `AckRequired` |
| `HeartbeatAck` | `0x0005` | `Session` / `heartbeat` | `Zero` | `Required` | `Forbidden` | `Response` |
| `TransferOffer` | `0x0100` | `Control` / `transfer-offer` | `Zero` | `Required` | `Forbidden` | `AckRequired` |
| `TransferAccept` | `0x0101` | `Control` / `transfer-accept` | `Zero` | `Required` | `Forbidden` | `Response` |
| `TransferReject` | `0x0102` | `Control` / `transfer-reject` | `Zero` | `Required` | `Forbidden` | `Response` |
| `ManifestPage` | `0x0103` | `Manifest` / `manifest-page` | `Zero` | `Required` | `Forbidden` | `0` |
| `ManifestComplete` | `0x0104` | `Manifest` / `manifest-complete` | `Zero` | `Required` | `Forbidden` | `Final` |
| `FileBegin` | `0x0200` | `File` / `file-begin` | `NonZero` | `Required` | `Forbidden` | `0` |
| `FileChunk` | `0x0201` | `File` / `file-chunk` | `NonZero` | `Required` | `Required` | `0`<br>`AckRequired` |
| `FileCheckpoint` | `0x0202` | `File` / `file-checkpoint` | `NonZero` | `Required` | `Forbidden` | `Response` |
| `FileEnd` | `0x0203` | `File` / `file-end` | `NonZero` | `Required` | `Forbidden` | `Final` |
| `FileResult` | `0x0204` | `File` / `file-result` | `NonZero` | `Required` | `Forbidden` | <code>Response &#124; Final</code> |
| `TransferPause` | `0x0300` | `TransferCommand` / `transfer-pause` | `Zero` | `Required` | `Forbidden` | `AckRequired`<br>`Response` |
| `TransferResume` | `0x0301` | `TransferCommand` / `transfer-resume` | `Zero` | `Required` | `Forbidden` | `AckRequired`<br>`Response` |
| `TransferCancel` | `0x0302` | `TransferCommand` / `transfer-cancel` | `Zero` | `Required` | `Forbidden` | `AckRequired` |
| `TransferComplete` | `0x0303` | `TransferCompletion` / `transfer-complete` | `Zero` | `Required` | `Forbidden` | <code>AckRequired &#124; Final</code> |
| `TransferResult` | `0x0304` | `TransferCompletion` / `transfer-result` | `Zero` | `Required` | `Forbidden` | <code>Response &#124; Final</code> |
| `ResumeQuery` | `0x0400` | `Resume` / `resume-query` | `Zero` | `Required` | `Forbidden` | `AckRequired` |
| `ResumeResponse` | `0x0401` | `Resume` / `resume-response` | `Zero` | `Required` | `Forbidden` | `Response` |
| `Error` | `0x7ffe` | `Control` / `error` | `Zero` | `Required` | `Forbidden` | `0`<br>`Response`<br>`Retryable`<br><code>Response &#124; Retryable</code> |
| `Goodbye` | `0x7fff` | `Session` / `goodbye` | `Zero` | `Required` | `Forbidden` | `Final` |

## 5. Codec families 与 CDDL

CDDL 规定 map 的 wire key 和结构；codec 还实施版本、类型、字段集合、条件字段、
长度、数值、enum、顺序和交叉字段约束。实现必须同时满足二者，不能只按 CDDL 的
表面类型接受更宽输入。除下述 `Control` extension-key 例外外，strict codec 依各自的
exact-field 检查拒绝未知字段。

每段 metadata 必须完整解析为一个 CBOR map，不能有 trailing CBOR item。字段是否可
省略、重复或扩展必须以对应 codec 为准，不能从其他 family 推断。Encoder 的输出是
项目 canonical bytes，不能由平台侧重建 map。

| Family | 共享 codec API | `MessageType` | CDDL schema | Qt test target |
|---|---|---|---|---|
| `Session` | `SessionMessageCodec::encodeHello()` / `decodeHello()`; `encodeAuthResult()` / `decodeAuthResult()`; `encodeHeartbeat()` / `decodeHeartbeat()`; `encodeGoodbye()` / `decodeGoodbye()` | `Hello`, `AuthResult`, `Heartbeat`, `HeartbeatAck`, `Goodbye` | `hello`, `auth-result`, `heartbeat`, `goodbye` | `RelayDeskSessionMessageCodecTests` |
| `Capability` | `CapabilityCodec::encode()` / `decode()` | `Capabilities` | `capabilities` | `RelayDeskCapabilityCodecTests` |
| `Control` | `ControlMessageCodec::encode()` / `decode()` | `TransferOffer`, `TransferAccept`, `TransferReject`, `Error` | `transfer-offer`, `transfer-accept`, `transfer-reject`, `error` | `RelayDeskControlMessageCodecTests` |
| `Manifest` | `ManifestPageCodec::encode()` / `decode()` / `encodeComplete()` / `decodeComplete()` | `ManifestPage`, `ManifestComplete` | `manifest-page`, `manifest-complete`, `manifest-entry` | `RelayDeskManifestPageCodecTests` |
| `File` | `FileMessageCodec::encode()` / `decode()` | `FileBegin`, `FileChunk`, `FileCheckpoint`, `FileEnd`, `FileResult` | `file-begin`, `file-chunk`, `file-checkpoint`, `file-end`, `file-result` | `RelayDeskFileMessageCodecTests` |
| `TransferCommand` | `TransferCommandCodec::encode()` / `decode()` | `TransferPause`, `TransferResume`, `TransferCancel` | `transfer-pause`, `transfer-resume`, `transfer-cancel` | `RelayDeskTransferCommandCodecTests` |
| `TransferCompletion` | `TransferCompletionCodec::encode()` / `decode()` | `TransferComplete`, `TransferResult` | `transfer-complete`, `transfer-result` | `RelayDeskTransferCompletionCodecTests` |
| `Resume` | `ResumeMessageCodec::encode()` / `decode()` | `ResumeQuery`, `ResumeResponse` | `resume-query`, `resume-response`, `resume-entry` | `RelayDeskResumeMessageCodecTests` |

### 5.1 关键 codec 约束

- `Hello` 是精确六字段 map；device/session UUID 不能为 null，fingerprint 必须为
  32 bytes，app version 最多 64 UTF-8 bytes，supported versions 为 1..16 个有效值。
- Accepted `AuthResult` 只能包含 `{1: true}`；rejected 形态必须包含 false、1..512
  UTF-8-byte diagnostic 和已冻结 `AuthResultErrorCode`：`InvalidHello=1`、
  `UnsupportedVersion=2`、`UnknownPeer=3`、`RevokedPeer=4`、
  `FingerprintMismatch=5`、`InternalError=6`。`None=0` 只用于 accepted typed value，
  不得出现在 rejected wire；其他值在 v1 返回 `InvalidAuthResult`。
- `Heartbeat` 与 `HeartbeatAck` 共用精确 map `{1: sequence, 2: timestampMs}`：sequence
  为 `0..2^63-1`，timestamp 为 `1..2^63-1`。
- `Capabilities` 是精确七字段 map。feature/policy list 要非空、无重复并满足 codec 的
  token/enum 约束；negotiated 数值不能超过共享 hard maxima。v1 冻结三个基础 token：
  `file.v1` 表示支持 RDFT/1 文件协议，`sha256` 表示支持本版摘要，
  `file.receive.v1` **单向**表示发送该 CAPABILITIES 的 endpoint 已组合并可执行
  `TransferOffer` 接收处理器。`file.v1` 本身不表示可接收 offer。发送方只能向明确
  声明 `file.receive.v1` 的对端发 offer；未组合 receiver 的 runtime 不得声明它，并
  必须拒绝无该协商事实的 incoming `TransferOffer`。其他 feature token 的含义不冻结。
- `ControlMessageCodec` 使用 `ControlMessage` variant 与 `Protocol.h` 中的
  `ConflictPolicy`、`RejectReason`。`RejectReason` 的 wire 值为 `UserDeclined=1`、
  `NotTrusted=2`、`PolicyDenied=3`、`InsufficientSpace=4`、`TooManyFiles=5`、
  `PathInvalid=6`、`UnsupportedCapability=7`、`Busy=8`、`InternalError=9`。
- `ErrorMessage` 使用 `ProtocolErrorCode`：`UnsupportedVersion=1001`、
  `InvalidFrame=1002`、`UnsupportedMessage=1003`、`InvalidState=1004`、
  `TemporarilyUnavailable=1005`、`InternalError=1006`。只有
  `TemporarilyUnavailable` 的 metadata retryable 为 true；该 boolean 由 catalog 派生，
  wire 值与 catalog 不一致或未知 code 都返回 `InvalidFieldValue`。`None=0` 不上 wire。
- Manifest entry 类型只有 `File=0` 与 `Directory=1`。协议路径必须已是安全、相对、
  `/` 分隔、NFC 的共享格式；默认最多 4,096 UTF-8 bytes、单 component 255 bytes、
  depth 128。目录必须 size 0 且没有 SHA-256；文件 SHA-256 只能缺省或为 32 bytes。
- Manifest 必须按 UTF-8 path bytes、entry type、fileId bytes 严格排序。Canonical
  digest 是对 deterministic CBOR manifest-entry array 的 SHA-256，不包含本地路径、
  display name 或 transferId。
- `FileBegin.chunkBytes` 为 `1..4 MiB`。`FileChunk` 的 payload 是唯一 wire file bytes；
  metadata 只携带 transferId、fileId、offset、sequence。`FileResultCode` 为 `Ok=0`、
  `HashMismatch=1`、`SizeMismatch=2`、`SourceChanged=3`、`TargetExists=4`、
  `DiskFull=5`、`PermissionDenied=6`、`PathInvalid=7`、`IoError=8`、`Cancelled=9`。
- `TransferPause` 与 `TransferResume` 都只含 transferId；`TransferResume` **不携带
  offset**。`TransferCancel` 另含 `UserRequested=1` 或 `ApplicationShutdown=2` 以及
  `keepPartial` boolean。
- `TransferComplete` 的 completed/skipped 各不超过 100,000，二者之和也不超过
  100,000；total bytes 不超过 `2^63-1`。`TransferResultCode` 为 `Ok=0`、`Partial=1`、
  `Cancelled=2`、`Failed=3`；`Ok` 禁止 diagnostic，其他结果要求 1..512 UTF-8 bytes。
- `ResumeQuery` 只含 transferId 与 manifest SHA-256。`ResumeResponse` 的 fileId 必须按
  RFC 4122 bytes 严格升序、不得重复，最多 100,000 项，metadata 最多 1 MiB。
- `Goodbye` 的 plain 形态只有 `Normal=0`、`ApplicationShutdown=1`、`IdleTimeout=3`；
  `ProtocolError=2` 必须且只能带 1..512 UTF-8-byte diagnostic。

### 5.2 `Control` family 的 integer extension keys

`ControlMessageCodec::decode()` 是当前唯一允许 unknown field extension 的 metadata
family：

- `validateMapKeys()` 要求每个 key 都是非负整数；非整数或负整数 key 返回
  `ControlMessageError::NonIntegerKey`；
- `TransferOffer`、`TransferAccept`、`TransferReject` 与 `Error` decoder 读取并验证
  自己的 required/optional known keys，同时忽略其他非负整数 key；
- unknown key/value 不进入 typed `ControlMessage` variant，因此随后调用
  `ControlMessageCodec::encode()` 会输出只含已知字段的 canonical map，不会保留或
  回显 extension bytes；
- 接受 unknown integer key 只提供 optional forward-compatible decode 行为，不为该
  key 分配语义，也不允许平台 adapter 自行解释它。

此行为由 `ControlMessageCodecTests::ignoresUnknownIntegerKey()` 覆盖。当前 60 个共享
JSON vectors 没有冻结 unknown Control integer extension key；三个 transfer Control
`metadata-negative` vector 覆盖缺 required fields 并期望 `MissingField`，`Error` 的
negative vector 覆盖未知 catalog code 并期望 `InvalidFieldValue`。因此 integer-key
extension 是当前共享 codec contract，但 unknown error code 明确不是 extension。

CDDL 中写作 `uint` 不代表 codec 接受 Qt CBOR signed integer range 以外的值；具体边界
以对应 codec 为准。

## 6. Duplicate、顺序与 `Final` 语义

`Final` 不是“收到后自动关闭一切”的通用操作。它只在 registry 允许的精确组合中
合法，状态机按消息范围解释：

| 消息 | `Final` 范围与当前语义 |
|---|---|
| `ManifestComplete` | sender 声明 manifest pages 发送完毕；reassembler 仍必须检查 page/entry 数量与 canonical digest |
| `FileEnd` | sender 结束一个 nonzero file stream；尚未表示 receiver 已提交文件 |
| `FileResult` | receiver 对该 file stream 的 terminal result |
| `TransferComplete` | sender 已耗尽任务且所有文件已有 terminal `FileResult`；使用 <code>AckRequired &#124; Final</code> |
| `TransferResult` | receiver 的 authoritative transfer commit result；使用 <code>Response &#124; Final</code> |
| `Goodbye` | session 进入 draining/close；没有 response frame |

Duplicate 与顺序规则按层区分，不能把一种消息的幂等规则推广到另一种消息：

- TLS session handshake 只接受一次 peer `Hello` 和一次 accepted `AuthResult`；重复或
  authenticated 后再次出现由 `FileTlsConnection` 拒绝。
- 当前 heartbeat sequence 的精确重复可重新 acknowledgement；较小 stale sequence
  忽略，跳号或不匹配的 ack 是 session protocol error。Heartbeat 状态不跨 reconnect。
- Manifest pages 必须从 0 开始顺序到达。旧 page index 是 `DuplicatePage`，超前 index
  是 `OutOfOrderPage`；重复 fileId、portable path collision、缺页、数量或 digest 不符
  都失败。Reassembler 完成后再追加或再次 finish 返回 `AlreadyComplete`，不是通用
  idempotent success。
- `ResumeResponse` 的重复 fileId 返回 `DuplicateFileId`，非严格升序返回
  `InvalidFileOrder`。
- 重复当前已应用的 pause/resume command 是幂等操作；与当前/terminal 状态矛盾的
  command 是稳定 transfer-state error。Pause/resume acknowledgement 使用同 type 的
  `Response` variant；cancel 不使用同 type response，而由 terminal
  `TransferResult(Cancelled)` acknowledgement。
- 精确重复的 `TransferComplete`/`TransferResult` 幂等并重放 cached response；内容
  矛盾的重复、所有文件结束前的 terminal message、terminal result 后的新 command
  都是稳定 transfer-state error。`TransferComplete` 仅表示 sender exhaustion，只有
  `TransferResult(Ok)` 表示 receiver commit 成功。
- 第一个合法 `Goodbye` 后不再排入新业务 frame，已排入 bytes 可 drain 后关闭 TLS；
  draining 时观察到相同 `Goodbye` bytes 是幂等的，其他 post-goodbye frame 是 session
  protocol error。没有 `Goodbye` 的 TLS close 是可续传 interruption，不是隐式 cancel。

## 7. Stable frame 与 codec errors

稳定契约是 C++ error enum 与 JSON vector 中的 symbolic error 名。`diagnostic` 文本用于
日志和调试，不是 wire code，也不冻结逐字内容。Encoder 以空 `QByteArray` 加
`QString` diagnostic 报错；decoder 使用下列 enum。

### 7.1 Envelope 与 frame

```text
ProtocolEnvelopeError:
None, UnknownMessageType, ReservedMessageType, InvalidFlags,
InvalidStreamId, MissingMetadata, UnexpectedMetadata, MissingPayload,
UnexpectedPayload

FrameDecodeStatus:
FrameReady, NeedMoreData, ProtocolError

FrameDecodeError:
None, InvalidMagic, UnsupportedMajorVersion, UnknownMessageType,
ControlMetadataTooLarge, DataPayloadTooLarge, UnexpectedPayload,
FrameTooLarge, LengthOverflow, ReservedMessageType, InvalidFlags,
InvalidStreamId, MissingMetadata, UnexpectedMetadata, MissingPayload
```

冻结 JSON 的 `expectedError` 使用下列稳定映射；表中未出现的
`FrameDecodeError` 仍是公开 enum，但当前没有对应 frame-negative vector：

| `FrameDecodeError` | JSON `expectedError` |
|---|---|
| `InvalidMagic` | `InvalidMagic` |
| `UnsupportedMajorVersion` | `UnsupportedVersion` |
| `ControlMetadataTooLarge` | `MetadataTooLarge` |
| `DataPayloadTooLarge` | `PayloadTooLarge` |
| `InvalidFlags` | `InvalidFlags` |
| `InvalidStreamId` | `InvalidStreamId` |
| `MissingMetadata` | `MissingMetadata` |
| `MissingPayload` | `MissingPayload` |
| `UnexpectedPayload` | `UnexpectedPayload` |

### 7.2 Metadata codec enums

```text
SessionMessageError:
None, UnsupportedVersion, UnsupportedMessageType, TooLarge, MalformedCbor,
MetadataNotMap, InvalidFields, InvalidDeviceId, InvalidSessionId,
InvalidAppVersion, InvalidVersions, InvalidFingerprint, InvalidTimestamp,
InvalidAuthResult, InvalidSequence, InvalidGoodbyeReason, InvalidDiagnostic

CapabilityCodecError:
None, UnsupportedMessageType, MalformedCbor, InvalidFields, InvalidFeatures,
InvalidLimits, InvalidConflictPolicies

ControlMessageError:
None, UnsupportedVersion, UnsupportedMessageType, MalformedCbor,
MetadataNotMap, NonIntegerKey, MissingField, InvalidFieldType,
InvalidFieldValue

ManifestPageError:
None, UnsupportedVersion, InvalidLimits, EmptyManifest, TooManyEntries,
TooManyPages, EntryTooLarge, PageMetadataTooLarge,
ManifestMetadataTooLarge, InvalidManifestOrder, InvalidManifestEntry,
MalformedCbor, MetadataNotMap, NonIntegerKey, MissingField,
InvalidFieldType, InvalidFieldValue, TransferMismatch, PageCountMismatch,
DuplicatePage, OutOfOrderPage, MissingPage, EntryCountMismatch,
DigestMismatch, ProtocolPathCollision, DuplicateFileId, AlreadyComplete

FileMessageCodecError:
None, UnsupportedMessageType, MalformedCbor, InvalidFields,
InvalidTransferId, InvalidFileId, InvalidInteger, InvalidChunkSize,
InvalidHash, InvalidResult

TransferCommandCodecError:
None, UnsupportedVersion, UnsupportedMessageType, TooLarge, MalformedCbor,
InvalidFields, InvalidTransferId, InvalidReason, InvalidKeepPartial

TransferCompletionCodecError:
None, UnsupportedVersion, UnsupportedMessageType, TooLarge, MalformedCbor,
InvalidFields, InvalidTransferId, InvalidFileCount, InvalidTotalBytes,
InvalidResultCode, InvalidDiagnostic

ResumeMessageCodecError:
None, UnsupportedVersion, UnsupportedMessageType, TooLarge, MalformedCbor,
InvalidFields, InvalidTransferId, InvalidManifestHash, TooManyFiles,
InvalidFileId, InvalidOffset, DuplicateFileId, InvalidFileOrder
```

`metadata-negative` vectors 的 `expectedCodecError` 与以上 enum symbol 逐字一致。
Negotiation、store、path、sender/receiver 与 UI error enum 属于更高层，不得当成
metadata codec error 发到 wire。

## 8. 60 个共享 JSON vectors

`product/spec/protocol/test-vectors.json` 固定 `schemaVersion=1` 与
`fixedHeaderBytes=32`，当前共有 60 个唯一命名向量：

| Kind | Count | 契约 |
|---|---:|---|
| `frame-positive` | 24 | 每个 registry `MessageType` 恰有覆盖；header、metadata、payload 与 expected fields 全部冻结 |
| `frame-negative` | 11 | 非法 envelope 必须返回指定 stable `expectedError` |
| `metadata-negative` | 25 | 每个 registry `MessageType` 至少有覆盖；对应 codec 必须返回指定 `expectedCodecError` |

`RelayDeskProtocolMessageRegistryTests` 验证：

- registry count、type/name 唯一性与全部 `Implemented`；
- 每个 descriptor 的所有合法 flag set 都被 envelope validator 接受；
- 错误 flags、stream、metadata、payload 被稳定拒绝；
- 每个 descriptor 的 CDDL schema、正向 vector type 和 metadata-negative type 都存在。

`RelayDeskProtocolVectorTests` 对全部 24 个正向 frame 执行：

1. 从 JSON 的 `headerHex + metadataHex + payloadHex` 解码完整 frame；
2. 按 registry `codecFamily` 解为具体 typed message；
3. 用同一 codec canonical encode，结果必须逐 byte 等于 JSON `metadataHex`；
4. 把 canonical metadata 放回 frame，经 `FrameCodec::encode()` 后必须逐 byte 等于完整
   冻结 frame；
5. 对 11 个 frame-negative 与 25 个 metadata-negative 比较精确 symbolic error。

Windows 与 macOS 都必须消费这同一个 JSON 文件、同一个 `relaydesk_transfer` 实现和
同一组 frozen bytes。平台 adapter 不得重写 CBOR、改变 UUID byte order、复制 registry
或维护“等价”平台向量。

## 9. 不可推断项与变更规则

- 未知 `messageType`、未知 flag 位和未列出的 flag 组合都不是可忽略 extension；v1
  按对应 stable error 拒绝。
- 未知 CBOR key 是 codec-family-specific：`ControlMessageCodec` 按第 5.2 节接受但不
  保留未知非负整数 key；非整数/负整数 key 被拒绝。其他 strict codec 依自身
  exact-field 检查拒绝未知 key。不得把 Control 的 forward-compatible 行为推广到
  其他 family。
- Registry 数值空洞不是可自由使用的 extension range。
- `CompressedMetadata` 常量不表示 v1 peer 可以发送压缩 metadata。
- CDDL 中 optional 只表示该字段在 schema 中可省略；仍须满足 codec 的条件约束。
- 示例 token、diagnostic 文本、数值空洞或本地 enum 不构成新 wire schema。
- `streamId` 只由 registry 冻结为 zero/nonzero；本文不额外定义未实现的 multiplexing
  语义。
- Device/transfer/file strong identity 的进一步约束不在本 wire 文档中；待独立接口
  文档冻结后引用，不能从 UUID 字节形状推断业务身份语义。

Freeze candidate 期间任何 wire 变更必须在同一共享提交中更新 registry、对应 codec、
CDDL、冻结 vectors、registry/vector tests 与本文，并在 Windows/macOS 上得到相同
bytes。Freeze 完成后，改变既有值、field 语义、envelope 或 canonical bytes 必须升
major version，或先定义并冻结明确的向后兼容机制；不得靠“接收端应该能猜到”维持
兼容。
