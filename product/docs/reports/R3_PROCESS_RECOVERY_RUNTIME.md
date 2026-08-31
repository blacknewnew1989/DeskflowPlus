# R3 进程恢复运行时证据

## 1. 状态与结论边界

`R3-PROCESS-RECOVERY-001` 在 `agent/a0/redevelop-p0@f176a1f1202ac45462b70359482458048f588bad`
更新为 `IN_PROGRESS`。

当前已集成 outgoing/incoming 恢复状态持久化与进程启动 bootstrap，并已在 Windows 同机环境验证
receiver 子进程正常退出后，由新的 receiver 子进程沿用同一 transferId 从非零 durable offset 完成
单文件续传。该证据不是整个 R3 的 `PASS`，也不证明 sender relaunch、文件夹跨进程 relaunch、
macOS 当前 SHA 或物理 Win↔Mac。

## 2. Git 基线

| 项目 | SHA / ref |
|---|---|
| A0 远端集成分支 | `agent/a0/redevelop-p0@f176a1f1202ac45462b70359482458048f588bad` |
| 产品分支（未合入） | `product/relaydesk-v1@c544dc76fb4f29aefb6ef30c8acc4475b6778e07` |
| 恢复状态 Store 集成 | `8eb60779ff85d1458466a43e6919466f00e6e64e` |
| outgoing owner / A0 集成 | `23fcaff0c3b90372a8df976a0c72f4573f335037` / `b7ce5c30eb0cbc25728212cd522c22f7a009fe1c` |
| incoming owner / A0 集成 | `7c2ee1d3b652097aa1927cf6145da53e46c0b8c6` / `2d16bc96598bccf4ce0cefc52a09c8eba436a0da` |
| receiver relaunch owner / A0 集成 | `b3ea5336727ed73c1d5e6dcb7184553512303d3c` / `f2267e310ec744d15673557aa1a3e72e8f62387a` |
| Store 目录 entry owner / A0 集成 | `af14337134db2b17f2a95ffadf2676be33be5b54` / `f176a1f1202ac45462b70359482458048f588bad` |

上述 ref 均经远端复读。产品分支仍停留在 `c544dc76f`，因此本报告只描述尚未合入产品分支的
重开发集成证据，不构成阶段标签或发布候选证据。

## 3. 已集成实现事实

- `TransferRecoveryStore` 持久化有界 outgoing/incoming 恢复描述；终态删除失败不会静默发布成功；
- outgoing bootstrap 可从持久化的 prepared manifest、source bindings 和 transferId 重建发送会话；
- incoming bootstrap 可从恢复描述、ResumeStore 和已解析目标状态重建接收会话；
- Store 对目录 entry 接受协议要求的空摘要，文件 entry 仍必须包含 32 字节 SHA-256；
- 两端 bootstrap 均复用现有 production discovery、trust、TLS、文件传输与恢复状态路径，未新增
  wire protocol、service 或 UI 接口。

这些事实说明持久化与 bootstrap 已进入 A0 集成分支，不等同于所有进程重启组合均已运行。

## 4. Windows 同机 receiver relaunch 证据

固定 `receiver-process-recovery` 场景使用三个 OS 子进程生命周期：sender、receiver-1 和
receiver-2。controller 仅在 receiver-1 正常退出且已确认非零 durable checkpoint、`.part` 大小和
恢复状态存在后启动 receiver-2。

结构化断言覆盖：

- receiver-1、receiver-2 与 sender 使用同一 transferId；
- receiver-2 复用 receiver root、设备身份、TLS identity、trust、接收目录和恢复状态；
- sender 状态顺序包含 `Interrupted -> Resuming -> Completed`；
- sender 第一条 Resuming 与 receiver-2 第一条恢复进度均不小于退出前 durable offset；
- 最终文件 SHA-256 与源一致，`.part`、ResumeStore 和 incoming recovery descriptor 均已清理；
- receiver-1、receiver-2 和 sender 均为正常子进程退出。

| 验证层级 | 基线 | 结果 |
|---|---|---|
| A7 owner 新场景单次 | `b3ea53367` | 退出 0 |
| A7 owner 新场景重复 | `b3ea53367` | 20/20，均退出 0 |
| A7 owner 完整 TwoProcess 可执行目标 | `b3ea53367` | 7 个场景全部完成，退出 0 |
| A0 完整 `RelayDeskTwoProcessRuntimeTests` | `f2267e310` | 退出 0 |

以上均为 Windows localhost 同机子进程证据。receiver-1 是按场景正常退出，不覆盖 crash、断电或
进程强杀恢复。

## 5. Store 与 file-tree 回归

Store 目录 entry 缺陷最初由给 `file-tree` 场景启用 recovery root 暴露：协议目录 entry 的摘要
必须为空，旧 Store 却对全部 entry 无条件要求 32 字节摘要。修复后按 entry 类型统一校验，并覆盖
outgoing prepared manifest 与 incoming wire manifest 的 `save -> load -> scan` round-trip；文件空摘要
仍在重算 manifest digest 与 page binding 后被拒绝。

| 验证 | 基线 | 结果 |
|---|---|---|
| A6 完整 `RelayDeskTransferRecoveryStoreTests` | `af1433713` | 退出 0 |
| A6 recovery-root `fileTreeUsesTwoIndependentProcesses` | `af1433713` | 退出 0 |
| A0 两目标构建 | `f176a1f12` | 退出 0 |
| A0 完整 `RelayDeskTransferRecoveryStoreTests` | `f176a1f12` | 退出 0 |
| A0 recovery-root `fileTreeUsesTwoIndependentProcesses` | `f176a1f12` | 退出 0 |

该 file-tree 回归证明同机双进程文件树传输在启用 Store 时不再被目录 entry 校验阻断；它没有执行
文件夹传输过程中的 sender 或 receiver 子进程 relaunch。

## 6. 当前状态矩阵

| 范围 | 状态 | 证据边界 / 下一步 |
|---|---|---|
| outgoing/incoming 持久化与 bootstrap 集成 | `IN_PROGRESS` | 已进入 A0 重开发分支；继续由未运行组合完成动态覆盖 |
| Windows 同机 receiver 正常退出、新进程同 transferId 非零续传 | `IN_PROGRESS` | 自动化证据已取得，但只是 R3 子范围 |
| sender 子进程 relaunch | `NOT_RUN` | 尚无 sender 新进程恢复的动态场景 |
| 文件夹跨进程 relaunch | `NOT_RUN` | recovery-root file-tree 只覆盖不中途重启的传输 |
| macOS 当前 SHA | `NOT_RUN` | `f176a1f12` 尚无 macOS 构建或动态运行证据 |
| 物理 Win↔Mac | `FINAL_ACCEPTANCE_REQUIRED` | 最终包完成后执行真实双机验收 |

## 7. 独立复核与后续顺序

- receiver relaunch 测试经独立复核为 GO，结论严格限定 localhost 同机 OS 子进程；
- Store 首轮复核发现 decode 仍无条件要求 32 字节摘要，修复并增加 load/scan 后复核为 GO；
- 当前报告不得用 owner 20/20、A0 退出 0 或 Store file-tree 回归替代未运行项。

后续顺序保持：sender 子进程 relaunch、文件夹跨进程 relaunch、macOS 当前 SHA，再进入最终物理
Win↔Mac 验收。未执行前保持上述状态，不提前写 `PASS`。
