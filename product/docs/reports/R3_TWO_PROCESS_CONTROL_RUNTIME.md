# R3 接收端双进程传输控制验证

## 1. 结论与范围

`R3-CTRL-001` 在限定自动化范围内为 `PASS`：receiver 在独立 OS 进程中通过 production
`IFileTransferService` 对正在接收的传输直接执行暂停、继续和取消；sender 进程只观察远端状态。
该链路继续使用现有 discovery、pairing/trust、TLS、RDFT command codec 和
`FileTransferRuntime`，没有新增 production API 或 wire protocol。

结论只覆盖 E4 同机双进程文件链路，不证明 GUI、键鼠、物理 Win-Mac、macOS 权限或真正进程
退出后的断点恢复。macOS hosted Release 已通过；A5 会话不是 macOS，本机 Debug/Release 复验
准确标记为 `BLOCKED`，macOS Debug 保持 `NOT_RUN`。

## 2. 精确 Git 证据

| 项目 | SHA / ref |
|---|---|
| 未合入产品分支 | `product/relaydesk-v1@c544dc76fb4f29aefb6ef30c8acc4475b6778e07` |
| 测试 owner 分支 | `agent/a7/r0-two-process-runtime@c6fb1f5410b6a9cc6663fc6c96baf9c9ba9959da` |
| A0 集成分支 | `agent/a0/redevelop-p0@346025db6142ac34d3dccce0d3194d7d87e811ab` |
| 本轮 workflow | `33333471632@346025db6142ac34d3dccce0d3194d7d87e811ab` |

GitHub API 与普通 push 后远端 ref 均已复读。产品分支仍为 `c544dc76f`，不得把本报告写成
产品分支或发布候选验收。

## 3. 监督纠偏与最终实现

| 提交 | 审阅结论 | 处理 |
|---|---|---|
| `8442fb45b` | NO-GO：只有 sender 发起控制，不能证明接收方直接控制 | control actor 改为 receiver |
| `7dd95e22c` | NO-GO：receiver 在 `transferChanged` 信号栈同步调用控制命令 | typed intent 排到下一事件轮次 |
| `c6fb1f541` | GO：receiver actor、queued intent、sender observation | 独立验收后集成 |

最终薄 peer 的行为：

1. receiver 的 Receiving progress 达到 1 MiB 后先设置一次性 guard；
2. `QTimer::singleShot(0)` 在后续事件轮次调用 production `pause()` 或
   `cancel(...PartialDisposition::Remove)`，不在 signal 栈重入 runtime；
3. pause 场景由 receiver 在自身 `Paused` 状态同时核对 snapshot completed bytes 与真实
   `.part` 总尺寸稳定，再由 receiver 直接 `resume()`；
4. sender 只记录远端 `Paused`、`Cancelled` 和最终 `Completed`；
5. 完成时核对 SHA-256；取消时核对双方 `Cancelled`，且 receiver 对当前 transfer 的 `.part`、
   `.incoming/<transferId>` 和 resume sidecar 均已清理；
6. CLI 只增加固定 `--scenario complete|pause-resume|cancel`，输出仍为结构化 JSON 和 exit code。

没有固定 200/500 ms 控制延迟，没有新 RPC、通用测试框架或 GUI 模拟器。

## 4. Windows 独立验收

独立验收 owner 对精确远端 `c6fb1f541` 给出 `GO`：

- Debug `-functions`、单轮 CTest 3.04 s、`--repeat until-fail:10` 10/10 PASS；
- Release `-functions`、单轮 CTest 2.75 s、10/10 PASS（总计 20.78 s）；
- 每轮执行 complete、receiver pause/resume、receiver cancel 三个 QtTest 槽；
- Release peer JSON：`receiverControlled=true`、`pauseBytesStable=true`、
  `senderObservedPause=true`，双方 `passed=true`；
- cancel JSON：双方 `cancelled=true`、receiver `cancelCleanupValid=true`，双方 exit code 0；
- pause/resume 完成文件 SHA-256 与源文件一致；cancel 后 `.part` 数为 0；
- 最终无 controller、peer 或 ctest 残留。

命令形式：

```powershell
ctest --test-dir <debug-or-release>/src/unittests `
  --output-on-failure `
  -R '^RelayDeskTwoProcessRuntimeTests$' `
  --repeat until-fail:10
```

## 5. 双平台集成 run

唯一集成 run：[`33333471632`](https://github.com/blacknewnew1989/DeskflowPlus/actions/runs/33333471632)

| 平台/任务 | 结果 | 定向证据 |
|---|---|---|
| materials `99316109372` | PASS | 资料校验成功 |
| Windows `99316109298` | PASS | CTest 100/100；TwoProcess #98 PASS 1.78 s；TEST-005 PASS |
| macOS `99316109379` | PASS | CTest 101/101；TwoProcess #99 PASS 1.84 s |
| macOS lifecycle `99317291523` | PASS | hosted isolated lifecycle report `status=PASS` |
| publish `99317291990` | SKIPPED_BY_SCOPE | 分支 run 按规则不发布；不能替代精确标签 Release |

Win/Mac artifact 中的 `ctest.log` 还确认以下 supporting targets 实际执行：

- Windows：Protocol Registry #38、Protocol Vector #39、Transfer Command #49、
  FileTransferRuntime #93；
- macOS：Protocol Registry #39、Protocol Vector #40、Transfer Command #50、
  FileTransferRuntime #94；
- 上述目标及两平台完整 CTest 均为 PASS。

## 6. Artifacts

| Artifact | ID | API digest |
|---|---:|---|
| Windows x64 | `9738437182` | `sha256:0b86ec0db98c3426253b5aa6065b9240fd2e1b91abe5724b958c1f66c7c75340` |
| macOS arm64 | `9738387567` | `sha256:931490e7567dbb6b5cb3df20061ea12aaa210856d0df6f4e4ca123da50029c8c` |
| macOS lifecycle | `9738441639` | `sha256:d70f38ad2359f41fdcd7c0cac89a2bdf04a40a39fb1279cd7d37d706fa68ff01` |

## 7. 跨平台协作边界

- A0 请求：`product/working/platform-sync/a0/20260830-201236Z-R3-receiver-control-macos-validation.md`，
  coordination commit `e013b23877637a5e1b11e37c04443e6867a650c2`；
- A5 回执：`product/working/platform-sync/macos/20260830-201428Z-R3-receiver-control-macos-validation-blocked.md`，
  coordination commit `8f71ed675938729e1b6fb2e81e7f629f538f3e26`；
- BLOCKED 原因：A5 会话实际为 Windows 11 x64、无 `xcodebuild`；未伪造 macOS 本机结果；
- hosted macOS job 是平台构建回退，不覆盖 macOS Debug、真实 App 交互或物理跨平台验收。

## 8. 后续缺口

1. `R3-FILETREE-001`：两个独立进程传输多 source、嵌套文件夹和空目录；
2. `R3-LISTENER-RESUME-001`：同一进程内 listener 中断后从非零 durable offset 恢复；
3. `R3-PROCESS-RECOVERY-001`：真正 sender/receiver 进程退出后恢复目前没有 production bootstrap，
   必须先持久化 outgoing manifest/source bindings 与 receiver offer/options/session context；
4. 物理 Win-Mac 双向控制和文件传输继续 `FINAL_ACCEPTANCE_REQUIRED`。
