# R3 双进程文件树与监听器续传验证

## 1. 结论边界

以下两个自动化纵向切片在限定范围内为 `PASS`：

- `R3-FILETREE-001`：两个独立 OS 进程通过 production discovery、pairing/trust、TLS 和
  `IFileTransferService::send()` 一次发送独立文件与嵌套文件夹，保留空目录；
- `R3-LISTENER-RESUME-001`：两个独立 OS 进程保持存活，receiver 的 production
  `FileTransferRuntime` listener 在 durable checkpoint 后 `stop()/start()`，sender 重新发现非零端口并
  从非零 offset 继续。

第二项严格是**同一 peer 对象内 listener 重启**，不是 sender/receiver 进程退出后的恢复。
真正进程重建缺少 outgoing manifest/source bindings 与 incoming offer/options/session 的 production
bootstrap，继续为 `NOT_RUN`。

## 2. Git 与分支

| 项目 | SHA / ref |
|---|---|
| 产品分支（未合入） | `product/relaydesk-v1@c544dc76fb4f29aefb6ef30c8acc4475b6778e07` |
| 文件树 owner 最终提交 | `agent/a7/r0-two-process-runtime@ea3c6688852389c1ead0973cd49cb95985fc9ad8` |
| sidecar production 修复 | `agent/a6/resume-sidecar-cleanup@aada14580a1b8935cb5692410cc7189a1ddee674` |
| listener-resume owner 最终提交 | `agent/a7/r0-two-process-runtime@813f7fc9460ad95816377aaf27231bcdc465db0a` |
| A0 已验证实现 | `agent/a0/redevelop-p0@200303da19cb8e10e613449bb3421e5bb0ca6c36` |

所有 owner 提交均经普通 Git push 与 GitHub API 复读；产品分支仍为 `c544dc76f`，本报告不构成
产品分支或发布候选验收。

## 3. 文件树 E4

固定 `file-tree` 场景由 driver 创建：

```text
alpha.txt                         16 bytes
bundle/
└── nested/
    ├── leaf.bin                  4097 bytes
    └── empty/
```

sender 在一次 production 调用中传入 `{alphaPath, bundlePath}`。receiver 校验：

- sender/receiver 非空且相同的 transferId；
- `completedFiles=2`、`completedBytes=4113`；
- 两文件 `isFile`、size 和 SHA-256 与源一致；
- 三个目录均为 `isDir`，包括空目录；
- 排除管理容器 `.incoming` 后，actual/expected 业务树集合精确相等；
- 无 `.part`。

独立 fresh Windows Debug/Release 均通过 `-functions`、单轮和 repeat10；Release 直接探针复读了
JSON 和磁盘树，无 peer/controller 残留。

## 4. 完成态 sidecar production 修复

listener E4 首次跑通传输后发现 Completed 仍遗留
`.incoming/resume-active/<transferId>.resume.cbor`。根因是文件完成路径只把 durable offset 保存为
total 后直接发布 Completed；只有 directory-only 路径删除 sidecar。

`aada14580` 将 `ResumeStore.remove()` 统一移动到 `ReceivePipeline::publishCompleted()` 最前：

- directory-only 与普通文件完成都只从一个入口删除；
- remove 失败转 `InternalError` 并在 Completed signal 前返回；
- Interrupted、cancel Keep 和其他非完成路径不误删可恢复状态。

fresh Debug 红测在旧代码上稳定得到：

```text
!QFileInfo::exists(resumeStatePath) returned FALSE
Totals: 2 passed, 1 failed
```

修复后恢复完成、文件夹、仅目录与 cancel Keep partial 四个定向用例通过。独立验收给出 GO；
`ResumeStore.remove()` 失败的动态注入缺少测试缝，静态控制流证明失败不发布 Completed，该项保留为
测试增强候选。

## 5. Listener Resume E4

固定场景使用 20 MiB+ 单文件：

1. receiver 在至少一个真实 durable checkpoint 后，从 production `ResumeStore` 复读 sidecar 与
   `.part`；
2. control 通过下一事件轮次执行 listener `stop()/start()`，不在 `transferChanged` signal 栈重入；
3. sender 复用 100 ms discovery probe，持续到观察到新非零 filePort，再只调用一次 production
   `connectPeer()`；
4. expected `TransportFailed` 只在受限断链窗口内允许，首个 Resuming 后关闭，其他错误仍失败；
5. sender 第一条 Resuming 和 receiver 中断后第一条 Transferring 分别用独立 captured bool 记录，
   即使值为 0 也不允许后续覆盖；两值必须不小于 durable offset；
6. 状态以整数数组验证 `Interrupted -> Resuming -> Completed` 顺序；
7. 最终 SHA-256 一致，sidecar/`.part` 清理，双方 `passed=true` 且 `error=""`。

独立 Release 探针实际值：

| 证据 | 结果 |
|---|---:|
| durable offset | `1,048,576` |
| restart 前 `.part` | `2,097,152` |
| receiver 首条恢复进度 | `2,097,152` |
| sender 首条 Resuming 进度 | `4,194,304` |
| expected TransportFailed | `0` |
| sidecar / `.part` | 均不存在 |

历史风险：`5258a96c4` 的首次 fresh Debug 完整 repeat 曾在首轮 5.57 s 无上下文失败；listener 单槽
10/10 通过。随后 `813f7fc94` 仅增加 failure-only `QScopeGuard`，任何断言失败才保留 temp 并输出双方
JSON/exit evidence，成功路径零 I/O、业务断言不变。系统化 A/B 中最终低扰动代码连续两个 CTest
repeat20 共 40/40 PASS；新 fresh Debug/Release 各 repeat10 通过。该历史时序风险不删除，后续失败
已有可诊断证据。

## 6. 双平台集成 run

唯一集成 run：[`33341572421`](https://github.com/blacknewnew1989/DeskflowPlus/actions/runs/33341572421)
指向 `200303da19cb8e10e613449bb3421e5bb0ca6c36`。

| 平台/任务 | 结果 | 关键证据 |
|---|---|---|
| materials `99338043234` | PASS | 资料校验成功 |
| Windows `99338043371` | PASS | CTest 100/100；FileTransferRuntime #93 9.83 s；TwoProcess #98 3.85 s；TEST-005 PASS |
| macOS `99338043406` | PASS | CTest 101/101；FileTransferRuntime #94 14.65 s；TwoProcess #99 4.49 s |
| macOS lifecycle `99339775460` | PASS | hosted isolated lifecycle `status=PASS` |
| publish `99339776408` | SKIPPED_BY_SCOPE | 分支 run 按规则不发布 |

Artifacts：

| Artifact | ID | API digest |
|---|---:|---|
| Windows x64 | `9740883755` | `sha256:3bd4fc370f465679fc84dd033a7c6bfca2d3c52339537dbfdb81e3310927e1fc` |
| macOS arm64 | `9740774273` | `sha256:994b7571424dd76a237c01b469218b438ea04b483b2453b34fc9bc4fc0219476` |
| macOS lifecycle | `9740890339` | `sha256:4c3433692599769c3b0a9948ce4fe11b05d6ed3a98de4d0343331ed94716b364` |

## 7. 跨平台与未覆盖项

- A5 本机会话不是 macOS、无 `xcodebuild`；macOS Debug 动态复验为 `BLOCKED`；
- hosted macOS Release 是平台构建回退，不覆盖真实 App/menu bar/TCC 或物理 Win-Mac；
- 精确标签、draft Release 和 Release assets 尚未重建；
- 真正进程退出后的同 transferId 恢复、multi-file/folder resume、物理双向继续 `NOT_RUN` 或
  `FINAL_ACCEPTANCE_REQUIRED`。
