# 06 文件传输实现设计

## 1. 代码布局建议

真实目录由 A1 核查后适配：

```text
src/lib/relaydesk/transfer/
├── Protocol.h
├── FrameCodec.*
├── TransferTypes.*
├── FileTransferService.*
├── FileTransferManager.*
├── PeerSession.*
├── ManifestBuilder.*
├── TransferSender.*
├── TransferReceiver.*
├── ResumeStore.*
├── HistoryStore.*
├── PathPolicy.*
├── ConflictResolver.*
├── IntegrityVerifier.*
├── RateController.*
└── TransferErrors.*

src/lib/relaydesk/device/
src/lib/relaydesk/discovery/
src/lib/relaydesk/pairing/
src/lib/gui/relaydesk/
```

`starter/` 提供可独立编译的 FrameCodec 与 PathPolicy 起始实现。代理应先运行其测试，再按上游风格迁移，不得机械复制后不测试。

## 2. 类职责

### FileTransferService

应用层 facade：

- 启动/停止；
- 提供 send/accept/reject/pause/resume/cancel；
- 暴露 immutable snapshot；
- 汇总 discovery/trust/session；
- 不执行大 I/O。

### PeerSession

- 一个信任 peer 最多一个主 file connection；
- TLS/pinning；
- frame parser；
- capability；
- heartbeat；
- transfer stream routing；
- reconnect；
- 有界写队列。

### ManifestBuilder

输入 `QList<QUrl>`，输出：

```cpp
struct TransferManifest {
    TransferId transferId;
    QList<ManifestEntry> entries;
    quint64 totalBytes;
    quint64 fileCount;
    quint64 directoryCount;
    QByteArray canonicalSha256;
};
```

扫描时记录源快照：

- canonical local path；
- relative protocol path；
- size；
- mtime；
- platform file identity（可用时）；
- type；
- hash 策略。

P0 可以边扫描边计算哈希，但超大任务可能延迟 offer。建议：

- 小文件预哈希；
- 大文件可在发送过程中流式哈希；
- manifest hash 覆盖结构和元数据；
- FILE_END 提供最终文件摘要；
- 接收端以最终摘要为准。

### TransferSender

- 每次读取 chunk 前检查 pause/cancel；
- 使用 `QFile` 或平台等价；
- 检查起始 size/mtime；
- 发送结束再次检查 size/mtime；
- 变化时标记 SOURCE_CHANGED；
- 不自动发送变化后的混合内容。

### TransferReceiver

- 接收路径只由 PathPolicy + ConflictResolver 生成；
- P0 使用应用自建的专用接收根目录，只做相对路径和词法根目录约束；
- 发送端符号链接默认跳过，P0 不实现逐级 handle 校验或复杂 anti-TOCTOU；
- `.part` 使用独占/安全打开；
- 写成功后更新 in-memory offset；
- fsync 策略平衡性能，checkpoint 前确保可恢复语义；
- hash/size 通过后再 commit。

## 3. 流式与内存

错误实现：

```cpp
QByteArray all = file.readAll();
socket.write(all);
```

正确模式：

```text
socket low-water event
  -> worker read up to chunk size
  -> move chunk to network thread
  -> encode header/metadata
  -> socket write
  -> release buffer
```

上限：

- 每 stream 在途 chunk 数默认 2；
- 每 peer 总写队列默认 16 MiB；
- 全局待发送数据默认 64 MiB；
- 超限停止生产，不丢数据；
- UI snapshot 不保存 chunk。

## 4. 目录扫描

规则：

- 使用显式 stack，避免深递归栈；
- 跟踪 entry count、depth、metadata bytes；
- 默认跳过隐藏文件？**不跳过**，保持用户选择的内容；
- symlink：P0 reject 或 skip with warning，由产品设置统一；
- hardlink：按普通独立文件发送；
- 文件在扫描后消失：任务准备失败或标记；
- 权限不可读：列出具体失败项，用户决定取消/跳过；
- 不静默漏文件。

## 5. 协议路径

发送方：

1. 取共同根；
2. 生成 `/` 分隔相对路径；
3. Unicode NFC；
4. 验证组件；
5. 计算 canonical manifest hash。

接收方：

1. 不信任 manifest；
2. 重新标准化；
3. PathPolicy 校验；
4. 映射平台合法名称；
5. 解析冲突；
6. 检查最终目标仍位于 root；
7. 创建 staging。

不能仅用 `QDir::cleanPath()` 判断安全。

## 6. ResumeStore

每任务：

```cbor
{
  1: 1,
  2: h'transfer-id',
  3: h'peer-device-id',
  4: h'manifest-hash',
  5: "receiving",
  6: [
    {
      1: h'file-id',
      2: "root/a.bin",
      3: 12884901888,
      4: 21474836480,
      5: "a.bin.part"
    }
  ],
  7: 1730000000000
}
```

要求：

- `QSaveFile`/temp+rename 原子更新；
- schema version；
- 不写私钥；
- `.part` 与 state 一致性检查；
- state 缺失但 `.part` 存在时默认不盲目续传；
- 过期任务清理前通知/设置；
- cancel 可选择删除 partial。

## 7. HistoryStore

P0 采用本地有界 JSONL 或紧凑 JSON：

```json
{"id":"...","direction":"send","peer":"...","files":42,
 "bytes":1234,"startedAt":"...","finishedAt":"...",
 "status":"completed","errorCode":null}
```

- 最多 1,000 条或 90 天；
- 定期压缩；
- 损坏行跳过并记录；
- UI 分页读取；
- 不保存所有绝对本地路径；可保存显示名和本地 open location token/path（根据隐私设置）。

## 8. 冲突处理

`ConflictResolver` 输入：

- target root；
- relative path；
- policy；
- existing file info。

输出唯一决策：

```text
Use(path)
Skip
Ask(conflictId)
Fail(error)
```

自动重命名：

```text
name.ext
name (1).ext
name (2).ext
```

并发任务通过简单的目标名预留与冲突重试避免互相覆盖；P0 不建设复杂文件系统竞态防御框架。

覆盖策略：

- 先写 staging；
- 完成校验后替换；
- 尽量使用平台原子替换；
- 失败不破坏原文件。

## 9. 完整性

- TLS：传输保密与链路完整性；
- SHA-256：端到端文件完整性和 resume 验证；
- 不使用 MD5；
- 不把 hash 当恶意文件安全扫描；
- hash mismatch：
  - 不 commit；
  - 保留/删除 partial 按策略；
  - 记录错误；
  - 重试默认从 0 或经过分块校验后恢复，P0 可从 0。

## 10. 速度和 ETA

- 采样最近 5～10 秒；
- 排除暂停时间；
- 使用 EWMA；
- ETA 在样本不足时显示“计算中”；
- 进度信号节流至 5 Hz；
- 完成多个小文件时避免 UI 抖动。

## 11. QoS

优先级：

1. 输入通道；
2. pairing/control；
3. file control/checkpoint；
4. file payload；
5. history/diagnostic。

可行措施：

- 独立 socket/thread；
- 有界并发；
- 每块后让出事件循环；
- 网络/CPU 压力高时降并发；
- hash worker 低于 GUI/input 优先级（平台允许时）；
- 不在 GUI 高频日志每个 chunk。

## 12. 失败语义

每个错误包含：

```cpp
struct TransferError {
    ErrorCode code;
    ErrorCategory category;
    bool retryable;
    QString userMessageKey;
    QString diagnostic;
};
```

UI 使用 `userMessageKey` 本地化；diagnostic 进入日志。不要把远端任意字符串直接显示。

## 13. 诊断

每任务日志上下文：

- transferId 短值；
- peer deviceId 短值；
- direction；
- state；
- file index；
- offset；
- error code；
- connection generation。

不记录：

- pairing code；
- private key；
- 文件内容；
- 默认完整绝对路径；
- 证书私钥；
- 敏感剪贴板。

## 14. 纵向开发切片

1. FrameCodec + tests。
2. PathPolicy + tests。
3. TLS loopback peer + pinning fixture。
4. 单文件 offer/accept。
5. stream `.part` + SHA-256。
6. Qt test loopback 中断恢复。
7. Win→Mac 真机。
8. 多文件/文件夹。
9. pause/resume/cancel。
10. UI 和历史。
11. 压测和故障注入。
