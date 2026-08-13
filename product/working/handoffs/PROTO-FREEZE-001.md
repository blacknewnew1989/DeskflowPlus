# PROTO-FREEZE-001 A0 优先执行通知

- 优先级：当前最高；先于继续扩展 COMP-004 业务功能。
- 权威来源：`product/docs/01_PRD.md` §0.1、§5.3、FR-PROTOCOL、AC-008。
- 负责人：A6（wire protocol/interface owner）、A2（网络/发现接口复核）、A0（唯一集成）。
- 平台边界：A4/A5 冻结前只继续不依赖未定义协议的构建、权限、打包和安装验证。

## A0 立即动作

1. fetch 最新 `origin/product/relaydesk-v1`，暂停继续叠加依赖未冻结消息的业务功能；已完成且
   可独立验证的安全小切片先提交/推送，不丢弃工作。
2. 将 `PROTO-FREEZE-001` 置于 `product/TASK_BOARD.md` In Progress 首位。
3. 指定 A6 依次提交：
   - session heartbeat/goodbye contract + codec + vectors；
   - transfer pause/resume/cancel/complete/result contract + codec + vectors；
   - MessageType registry 全量 implemented/reserved 自动审计；
   - `IFileTransferService` / `FileTransferRuntime` 组合边界收敛；
   - 协议文档、共享接口文档和 v1 freeze reference。
4. 每个提交执行真实 Qt 编译和定向测试并推送 owner 分支，A0 小步 cherry-pick。
5. 运行 Windows/macOS 相同冻结向量，修复所有字节差异。
6. 更新 `PROJECT_STATE.md`、`TASK_BOARD.md` 和阶段报告。
7. 创建并推送 `relaydesk-protocol-v1-<date>-01`，监控唯一
   `.github/workflows/relaydesk-build.yml`。
8. 双平台 PASS 后通知 A4/A5 从标签提交或其后继创建 service 平台分支。

## 当前已确认的缺口

下列消息在 `Protocol.h` 已注册，但截至本通知基线没有独立 wire codec/handler contract：

- `Heartbeat` / `HeartbeatAck`；
- `TransferPause` / `TransferResume` / `TransferCancel`；
- `TransferComplete` / `TransferResult`；
- `Goodbye`。

本地 pause/resume/cancel 状态机和 `IFileTransferService` 方法不能替代 wire schema。
这些消息必须补齐，或明确标为 v1 reserved 并从运行时发送路径排除。

## 完成证据

- `product/docs/19_PROTOCOL_V1_FREEZE.md`；
- 全 MessageType registry 审计测试；
- Windows/macOS 共用正负冻结向量；
- owner 小提交和 A0 集成提交；
- protocol freeze tag 与双平台 Actions run；
- `PASS/FAIL/NOT_RUN` 清单。

该顺序是跨平台兼容依赖，不是 PR、人工审批、required check 或覆盖率门禁。
