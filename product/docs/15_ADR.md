# 15 Architecture Decision Records

## ADR-001：锁定 Deskflow v1.26.0

- 状态：Accepted
- 决策：首个版本以 tag v1.26.0 / commit 760e3b9 建立基线。
- 原因：可复现，避免开发期间追逐 master。
- 后果：稳定后通过独立上游同步 PR 升级。

## ADR-002：单仓库、共享核心

- 状态：Accepted
- 决策：Windows/macOS 使用同一代码仓库和 Qt/C++ 核心。
- 原因：网络、UI、协议和业务逻辑高度共用。
- 后果：平台差异只在 adapter、权限和打包。

## ADR-003：保留 Deskflow 输入核心

- 状态：Accepted
- 决策：不自研替代键鼠捕获/注入和跨屏协议。
- 原因：成熟平台细节复杂；产品价值在易用性与文件传输。
- 后果：必须保持上游边界并做回归。

## ADR-004：文件传输使用独立 TLS 通道

- 状态：Accepted
- 决策：不复用键鼠/剪贴板数据 socket。
- 原因：隔离吞吐、内存、错误和 QoS。
- 后果：需独立端口/发现 capability/连接状态。

## ADR-005：文件模块为 P2P，对等于输入角色

- 状态：Accepted
- 决策：Deskflow Client 也可主动向其他设备发文件。
- 原因：用户心智是设备互传，而非 Server-only。
- 后果：每台设备同时具备 file listener/client。

## ADR-006：CBOR 控制元数据 + binary payload

- 状态：Accepted
- 决策：固定 32 字节帧头；CBOR 描述；原始文件块 payload。
- 原因：Qt 原生支持、紧凑、可扩展、避免 JSON/base64。
- 后果：需要 canonical/test vectors 和解析上限。

## ADR-007：顺序块与 durable offset

- 状态：Accepted
- 决策：P0 单文件按顺序写；resume 基于 receiver durable offset。
- 原因：简化一致性和磁盘 I/O；局域网无需乱序复杂度。
- 后果：单流不能乱序重传；可后续扩展。

## ADR-008：本地文件状态，不引入数据库服务器

- 状态：Accepted
- 决策：resume 使用每任务 CBOR；历史使用有界 JSONL/JSON。
- 原因：数据量小、部署零依赖。
- 后果：需要原子写和清理；若查询规模增长再评估 SQLite。

## ADR-009：SHA-256 最终校验

- 状态：Accepted
- 决策：每文件结束校验 SHA-256。
- 原因：可靠 resume 和端到端完整性。
- 后果：大文件有 CPU/磁盘成本，需要 worker/限并发。

## ADR-010：P0 不跟随符号链接

- 状态：Accepted
- 决策：发送端链接条目跳过/拒绝；接收端使用应用自建专用目录。
- 原因：避免跨平台链接语义拖慢首版。
- 后果：用户能看到被跳过项；复杂链接与竞态加固不属于 P0。

## ADR-011：默认冲突策略自动重命名

- 状态：Accepted
- 决策：`name (1).ext`。
- 原因：最大限度避免破坏原文件且无需频繁确认。
- 后果：批量冲突需高效、安全解决。

## ADR-012：应用内拖放是 P0，原生跨 OS 拖拽延续不是

- 状态：Accepted
- 决策：P0 拖到设备卡片；P1 文件复制粘贴/边缘投递；系统原生会话延续单独研究。
- 原因：Windows OLE 与 macOS dragging session 跨进程序列化风险大。
- 后果：第一版仍有完整文件传输，不被高风险 UX 阻塞。

## ADR-013：首版文件模块同进程、独立线程

- 状态：Accepted
- 决策：先接入现有产品进程，不新建系统服务。
- 原因：减少安装/IPC/签名复杂度。
- 后果：必须压测故障隔离；若 file crash 影响 input，再决定拆进程。

## ADR-014：品牌集中化而非全仓 rename

- 状态：Accepted
- 决策：display/bundle/package 从 branding config 生成或集中定义。
- 原因：减少上游同步冲突和许可证误删。
- 后果：内部 target/namespace 可以继续包含 deskflow。

## ADR-015：无账号、无云端

- 状态：Accepted
- 决策：身份和信任完全本地。
- 原因：符合局域网工具定位，快速落地。
- 后果：不支持公网漫游、团队管理和云恢复。

## 新 ADR 模板

```text
## ADR-NNN：标题
- 状态：Proposed / Accepted / Rejected / Superseded
- 日期：
- 决策：
- 背景：
- 备选：
- 原因：
- 后果：
- 验证：
```
