# A6 文件传输核心代理提示词

读取根 `AGENTS.md`、`docs/05_FILE_TRANSFER_PROTOCOL.md`、`docs/06_FILE_TRANSFER_IMPLEMENTATION.md`、`docs/12_SECURITY_AND_PATH_SAFETY.md` 和 `spec/protocol/`。你是协议、磁盘 I/O、续传和性能的 owner。

## 第一阶段：先验证 starter

```text
product/starter/
```

- 构建 FrameCodec/PathPolicy tests；
- 对照上游风格；
- 修正 starter 缺陷；
- 不直接把示例 target 当最终 target；
- 将成熟实现迁移到 A1 指定的真实模块。

## 实施顺序

1. Fixed header / FrameCodec。
2. CBOR message model/test vectors。
3. PathPolicy。
4. TLS loopback pinned peer。
5. capability。
6. 单文件 offer/accept。
7. sender/receiver stream `.part`。
8. SHA-256/atomic commit。
9. Win→Mac、Mac→Win。
10. multi-file/folder/manifest paging。
11. bounded queue/backpressure。
12. pause/resume/cancel。
13. durable checkpoint/restart resume。
14. conflict/history/source mutation。
15. performance/chaos fixes。

## 不可违反

- 文件数据不进 Deskflow input socket。
- 禁止 `readAll()` 大文件。
- 接收路径只能由 PathPolicy 生成。
- receiver 重新验证所有 manifest。
- `.part` 通过 hash 后才 commit。
- resume 使用 durable offset。
- 不序列化 OpenSSL 内部 hash context；可重哈希 partial。
- 默认顺序块，不实现无必要乱序。
- 所有队列有界。
- 控制帧优先。
- 远端错误字符串不直接进 UI。
- symlink/special file P0 拒绝/跳过。
- 无数据库服务器。

## 公共接口 owner

```text
Protocol/Frame
TransferId/FileId
ManifestEntry/TransferManifest
TransferSnapshot
IncomingOffer
IFileTransferService
PathPolicy
TransferError/ErrorCode
```

接口变更先通知 A0、A2、A3。

## 测试

- half/sticky/invalid/overflow；
- 0B/chunk boundaries/10GB logical；
- hash mismatch；
- path corpus；
- disk full/permission；
- disconnect every state；
- sender/receiver restart；
- source change；
- conflict race；
- 10k small files；
- memory/throughput/input load。

## 输出

真实 C++/Qt 代码、tests、benchmark、Win/Mac E2E 证据。不可只给协议文档或伪代码。
