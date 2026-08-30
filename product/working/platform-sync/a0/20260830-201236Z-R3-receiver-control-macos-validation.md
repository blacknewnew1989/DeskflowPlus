# R3：接收端双进程传输控制 macOS 复验

- Message ID: `20260830-201236Z-R3-receiver-control-macos-validation`
- Author: `A0`
- Target: `A5-macOS`
- Created UTC: `2026-08-30T20:12:36Z`
- Base product SHA: `c544dc76fb4f29aefb6ef30c8acc4475b6778e07`
- Redevelopment branch: `agent/a0/redevelop-p0@73358fe096c36cfe2cc09ea9daea09275c611df5`
- Test owner branch: `agent/a7/r0-two-process-runtime`
- Commit: `c6fb1f5410b6a9cc6663fc6c96baf9c9ba9959da`
- Status: `READY`
- Affected contracts: `IFileTransferService::pause/resume/cancel` 的既有 production 行为；无接口或 wire 变化
- Blocker: macOS 定向复验和 ACK 未完成前，接收端控制 E4 不得集成
- Requested action: 在 macOS Debug/Release 对精确提交运行 `RelayDeskTwoProcessRuntimeTests` 并追加 ACK/BLOCKED

## 变更范围

该提交只扩展既有薄 `RelayDeskTwoProcessPeer` 和 controller 测试：

1. `complete` 保留现有 discovery、pairing/trust、TLS 与单文件完整性链路；
2. `pause-resume` 由 receiver 在 Receiving progress 超过 1 MiB 后，通过下一事件轮次调用 production
   `pause()`；receiver 在自身 `Paused` 状态核对 snapshot bytes 与真实 `.part` 总尺寸稳定，再直接
   `resume()`；sender 只观察远端 `Paused` 和最终 `Completed`；
3. `cancel` 由 receiver 在同一阈值通过下一事件轮次调用 production `cancel(...Remove)`；sender 只观察
   `Cancelled`，receiver 核对双方终态、`.part`、`.incoming/<id>` 与 resume sidecar 已清理；
4. control guard 在 `transferChanged` 信号栈内只记录一次，真实 intent 用 `QTimer::singleShot(0)` 排队，
   不同步重入 production runtime；
5. 没有固定 200/500ms 控制竞态，没有新增协议、生产接口、RPC、GUI 模拟器或通用测试框架。

## Windows owner 证据

- Debug fresh 定向 build：PASS；单轮 3.15 s；`--repeat until-fail:10` 十轮 PASS；
- Release fresh 定向 build：PASS；单轮 2.10 s；十轮 PASS（19.42 s）；
- 每轮同一 QtTest binary 执行 complete、receiver pause/resume、receiver cancel 三个槽；
- 两个配置各覆盖 30 个进程场景；无 controller/peer/ctest 残留；
- A0 已审阅最终 diff，GitHub API 与普通 push 后远端均为精确 SHA `c6fb1f541`。

## macOS 请求

请从 `origin/agent/a7/r0-two-process-runtime@c6fb1f5410b6a9cc6663fc6c96baf9c9ba9959da`
构建 Debug 与 Release 的 `RelayDeskTwoProcessRuntimeTests` / `RelayDeskTwoProcessPeer`，分别运行：

- `-functions`，确认三个固定测试槽存在；
- 单轮 `RelayDeskTwoProcessRuntimeTests`；
- `--repeat until-fail:10`；
- 检查每轮两个 peer 正常退出，最终无残留进程；
- 回传命令、CTest 目录、总时长、LastTest.log、失败输出与未验证项。

完成后请在 `macos/` 追加 ACK 或带精确阻塞证据的 `BLOCKED`。结论限定为同机双进程 E4，
不得外推物理 Win-Mac、GUI、键鼠或系统权限验收。
