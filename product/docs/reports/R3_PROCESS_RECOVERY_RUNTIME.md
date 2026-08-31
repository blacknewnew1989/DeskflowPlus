# R3 进程恢复运行时证据

## 1. 最终状态与边界

`R3-PROCESS-RECOVERY-001` 在
`agent/a0/redevelop-p0@043d6b3fb25fbd618804a36ab0ba3f263938e448` 标记为 `PASS`。

PASS 覆盖：

- outgoing/incoming 恢复状态持久化与 bootstrap；
- receiver 正常退出后的单文件同 transferId 非零续传；
- sender 正常退出后的单文件同 transferId 非零续传；
- receiver 正常退出后的文件树恢复，包括已提交文件、下一文件非零 `.part` 和空目录；
- Store 目录 entry 兼容与完整恢复状态清理；
- production shutdown 在 transfer runtime 引用依赖存活时完成 stop 与析构；
- hosted Windows/macOS 对当前 SHA 的完整构建、测试、打包和 macOS 自动生命周期。

PASS 不覆盖 crash、强杀或断电恢复。这些进入 R5，当前为 `NOT_RUN`。sender 文件树 relaunch 的
笛卡尔组合不作为 R3 门槛，也不能据此宣称已经运行。物理 Win↔Mac、TCC、人工安装交互、签名、
阶段标签和正式发布均不由本报告证明。

## 2. Git 基线

| 项目 | SHA / ref |
|---|---|
| A0 远端集成分支 | `agent/a0/redevelop-p0@043d6b3fb25fbd618804a36ab0ba3f263938e448` |
| 产品分支（未合入） | `product/relaydesk-v1@c544dc76fb4f29aefb6ef30c8acc4475b6778e07` |
| 恢复状态 Store 集成 | `8eb60779ff85d1458466a43e6919466f00e6e64e` |
| outgoing owner / A0 集成 | `23fcaff0c3b90372a8df976a0c72f4573f335037` / `b7ce5c30eb0cbc25728212cd522c22f7a009fe1c` |
| incoming owner / A0 集成 | `7c2ee1d3b652097aa1927cf6145da53e46c0b8c6` / `2d16bc96598bccf4ce0cefc52a09c8eba436a0da` |
| receiver 单文件 relaunch owner / A0 | `b3ea5336727ed73c1d5e6dcb7184553512303d3c` / `f2267e310ec744d15673557aa1a3e72e8f62387a` |
| Store 目录 entry owner / A0 | `af14337134db2b17f2a95ffadf2676be33be5b54` / `f176a1f1202ac45462b70359482458048f588bad` |
| sender 单文件 relaunch owner / A0 | `8ec33d168690239da1a43ab5c506cbbb2191208e` / `44611d2badae6f934bb77c257de1de41302182c5` |
| receiver 文件树 relaunch owner / A0 | `6d597fcd3814f21cbe6cddbf15d978b39a925d3a` / `d3ae4ac3300eb228c70e09ed5c1bfbac40881d45` |
| shutdown 测试证据 / production owner | `49ff6d1fd80ff7993a611ada64364542bef53877` / `40fe546966b0f0394c4ee77609a6eec76891a67e` |
| shutdown A0 集成 | `6c87340f3`、`99c330f88` |
| 停机状态 follow-up owner / A0 | `3487da5f57dbf60fa24965bea04887b622f899a4` / `043d6b3fb25fbd618804a36ab0ba3f263938e448` |

产品分支仍为 `c544dc76f`。本报告不构成产品分支、阶段标签或发布候选验收。

## 3. 已集成实现事实

- `TransferRecoveryStore` 有界保存 outgoing/incoming 恢复描述；
- outgoing bootstrap 校验本地身份、trust、source manifest、page plan 和 transferId 后重建发送会话；
- incoming bootstrap 校验恢复描述、ResumeStore v2、resolved target、实际目标文件和 `.part` 后重建接收会话；
- Store 按 entry 类型校验摘要：目录摘要必须为空，文件摘要必须为 32 字节 SHA-256；
- transfer 完成后清理 outgoing/incoming descriptor、ResumeStore sidecar 和 `.part`；
- shutdown 先停止 AutoReconnect，再 stop 并销毁 TransferRuntimeComposition/service，最后才停止
  discovery。FileTransferRuntime 引用的 trust/discovery 仍存活；其内部 incoming、file-safety 和 worker
  由自身所有，析构体先 stop 并等待 worker 完成。

没有新增 wire protocol、service 或 UI 功能接口。

## 4. 动态恢复场景

### 4.1 Receiver 单文件 relaunch

receiver-1 在非零 durable checkpoint、`.part` 和恢复描述存在后正常退出。receiver-2 复用同 root、
device ID、TLS identity、trust 和接收目录，以同一 transferId 恢复。sender/receiver 首条恢复进度均
不低于 checkpoint，最终 SHA-256 一致，恢复状态全部清理。

Owner 单次、20/20 和完整 TwoProcess 均通过；A0 最小验收退出 0。

### 4.2 Sender 单文件 relaunch

receiver 保持存活并写出实际 ResumeStore 非零 checkpoint。sender-1 正常退出；sender-2 复用同一
sender root、device ID、fingerprint、TLS、trust、source 和 recovery root，通过 outgoing bootstrap
恢复同一 transferId。最终 SHA-256 一致且全部状态清理。

Owner 新场景 20/20、完整 TwoProcess 和 A0 最小验收均退出 0。

### 4.3 Receiver 文件树 relaunch

固定 manifest 包含两个文件和空目录。receiver-1 只在以下条件同时成立时正常退出：

- ResumeStore v2 中正好一个 `resolvedTargets`，AutoRename 目标实际存在且 size/SHA 匹配；
- 下一文件正好一个，`0 < durableOffset < totalBytes`，`.part >= durableOffset`；
- incoming descriptor 的同一 manifest 包含两个 FileId 和指定空目录；
- outgoing descriptor 已存在。

receiver-2 复用同一身份/root/trust/TLS 与 transferId。最终精确目录树、两个文件 SHA 和空目录一致；
`first*.bin` 只有一个，证明已完成文件没有被 AutoRename 为重复副本；首恢复进度不低于 checkpoint，
outgoing/incoming/ResumeStore/`.part` 全部清理。

Owner 新场景 10/10、完整 TwoProcess 和 A0 最小验收均退出 0。

## 5. Store 与停机生命周期

### 5.1 Store 目录 entry

outgoing prepared manifest 与 incoming wire manifest 均覆盖目录空摘要 `save -> load -> scan`；文件空摘要
在重算 manifest digest/page binding 后仍被拒绝。启用 recovery root 的 file-tree 双进程回归退出 0。

### 5.2 Shutdown 生命周期

macOS 首次 hosted 失败暴露 sender 正常退出时的 stop/destruct 时序。修复拆为两个可审阅切片：

- A7 测试证据只修正 receiver durable checkpoint 等待、outgoing descriptor 重写证据和 two-process
  helper 显式析构；
- A3 production owner 增加组合根红测，并在 MainWindow shutdown hook 内于 pairing/discovery 仍存活时
  stop+delete transfer composition，再关闭 network services。

生命周期红测在旧 hook 退出 1，修复后新槽与完整 MainWindowLayoutTests 退出 0。独立 reviewer 核对
MainWindow 外部引用、TransferRuntimeComposition 成员逆序析构、FileTransferRuntime 的
incoming/file-safety/worker 生命周期及重复 shutdown 幂等性后给出 GO。

停机状态测试不再比较 sender snapshot、receiver durable 与 outgoing descriptor 的跨端精确数值；真实
契约为 stop 后 descriptor 相对 stop 前发生可观察重写，且 `0 < completedBytes < totalBytes`，transfer、
local/peer、fingerprint、source、summary 和 page plan 绑定保持一致。实际非零恢复由 4.1-4.3 场景证明。

## 6. Hosted 当前 SHA

最终 run：[`33385968319`](https://github.com/blacknewnew1989/DeskflowPlus/actions/runs/33385968319)，
head SHA `043d6b3fb25fbd618804a36ab0ba3f263938e448`，结论 `SUCCESS`。

| Job / 目标 | 结果 | 证据 |
|---|---|---|
| materials `99468545811` | PASS | development package validation |
| Windows `99468545603` | PASS | 101/101，54.13 s |
| Windows FileTransferRuntime #94 | PASS | 26.22 s |
| Windows TwoProcess #99 | PASS | 9.23 s |
| macOS `99468545756` | PASS | 102/102，69.94 s |
| macOS FileTransferRuntime #95 | PASS | 40.72 s |
| macOS TwoProcess #100 | PASS | 8.42 s |
| macOS lifecycle `99471900727` | PASS | hosted isolated lifecycle |
| publish `99471902029` | SKIPPED_BY_SCOPE | 分支 run 不发布 |

Artifacts：

| Artifact | ID | API digest |
|---|---:|---|
| Windows x64 | `9755964279` | `sha256:ef9f6f50158c93a25d0a4c4be2bd981f29b3d7d24f3b09fda147ec4467fc3436` |
| macOS arm64 | `9755748309` | `sha256:db31a2f46b91ca9d018c6066c883c0a97e3e714d9d71199c4d4f3e7f747b1606` |
| macOS lifecycle | `9755978890` | `sha256:470cfebe33d60f1c8261f67344c914a1e3a49254d61d51cb0cb03cffe96bcc92` |

这些结果只证明 hosted runner 对当前 SHA 的构建、测试、打包与自动生命周期，不证明物理设备或人工
系统交互。

## 7. 保留的失败诊断

| Run | SHA | 结果与根因 |
|---|---|---|
| `33375461387` | `d3ae4ac33` | FAIL；macOS FileTransferRuntime #95 在 receiver durable 仍为 0 时断言，TwoProcess #100 的 sender-1 已写 checkpoint JSON 后 `SIGABRT/CrashExit` |
| `33381378989` | `99c330f88` | FAIL；macOS 102/102 与 TwoProcess 通过，Windows FileTransferRuntime #94 暴露异步 descriptor checkpoint 测试的 Release 时序假设 |

失败 runs 不被最终成功 run 覆盖或删除，继续作为根因和回归证据。

## 8. 最终范围矩阵

| 范围 | 状态 | 边界 |
|---|---|---|
| 正常退出后的 localhost receiver/sender 单文件恢复 | `PASS` | 独立 OS 子进程、同 transferId、非零恢复 |
| 正常退出后的 localhost receiver 文件树恢复 | `PASS` | 已提交文件 + 下一文件 `.part` + 空目录 |
| Store 目录 entry 与 shutdown 生命周期 | `PASS` | 定向红绿测试、独立复审、hosted 当前 SHA |
| crash / 强杀 / 断电恢复 | `NOT_RUN` | 转入 R5，不属于 R3 PASS |
| sender 文件树 relaunch 笛卡尔组合 | 非 R3 门槛 | 不宣称已运行，也不阻断 R3 |
| 物理 Win↔Mac、TCC、人工安装交互 | `FINAL_ACCEPTANCE_REQUIRED` | 必须在最终包和真实设备上执行 |
| 阶段标签、正式发布 | `NOT_RUN` | 当前仅分支 run，publish skipped |

## 9. 独立复核

- receiver/sender relaunch、receiver 文件树 relaunch 和 Store 修复均经独立只读复核 GO；
- shutdown production owner 与测试证据分支独立提交，生命周期 reviewer 给出 GO；
- descriptor checkpoint follow-up 由同一 transfer reviewer 复核，确认是可观察持久化契约而非 CI 绿化；
- 文档最终复核只检查证据映射和范围，不把 hosted 结果外推为物理验收或正式发布。
