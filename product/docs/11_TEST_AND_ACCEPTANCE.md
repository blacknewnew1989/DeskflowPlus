# 11 自动测试与最终用户验收计划

## 0. 验收责任边界

开发过程的构建、自动测试、包 smoke、Actions 日志、提交与推送验收由 Codex/A0 完成。用户不参与 Phase 0～4 的中间验收。

用户只在最终收到 Windows/macOS 安装包后完成真实设备验收。GitHub runner 无法可靠模拟的 macOS 权限点击、Windows 防火墙提示、物理鼠标跨屏和真实断网/睡眠操作，统一标记 `FINAL_ACCEPTANCE_REQUIRED`，不得在开发中途要求用户执行。

A0 在最终交付前必须先完成所有可自动化项，并从 `product/templates/FINAL_ACCEPTANCE.md` 生成填写完整的 `FINAL_ACCEPTANCE.md`。

## 1. 结果规范

测试结果只允许：

- PASS；
- FAIL；
- NOT_RUN；
- BLOCKED；
- SKIPPED_BY_SCOPE；
- FINAL_ACCEPTANCE_REQUIRED。

每个结果包含：

```text
test id
commit
platform
command/steps
expected
actual
log/artifact
date
operator/agent
```

禁止“代码看起来没问题”作为 PASS。

## 2. 自动化层级

### Unit

- FrameCodec；
- CBOR serialization；
- state machines；
- PathPolicy；
- ConflictResolver；
- ResumeStore；
- HistoryStore；
- speed/ETA；
- manifest hash。

### Component

- `QSslSocket` loopback；
- partial read/write；
- backpressure；
- sender/receiver with temp dirs；
- process restart simulation；
- certificate mismatch。

### Integration

- GUI service model；
- discovery multi-interface mocks；
- real filesystem；
- installer smoke；
- platform permissions detection。

### E2E

- Win→Mac；
- Mac→Win；
- Win→Win；
- Mac→Mac；
- input + transfer concurrently。

## 3. 仓库、提交与推送验收

| ID | 场景 | 预期 |
|---|---|---|
| GIT-001 | 当前目录启动 | A0 自动定位 Git root 和 origin |
| GIT-002 | 仅开发包/空仓库 | 自动 fetch/import Deskflow，不需要用户 clone |
| GIT-003 | 小功能完成 | 存在独立 commit 和最小测试记录 |
| GIT-004 | backlog 任务 Done | 任务分支已推送 origin |
| GIT-005 | A0 合并 | `product/relaydesk-v1` 已推送 |
| GIT-006 | Phase 完成 | 阶段汇总 commit 与 `phase-N-complete` 标签已推送 |
| GIT-007 | Actions | Windows/macOS run 与 commit SHA 对应 |
| GIT-008 | Artifacts | A0 自动下载并生成 SHA-256/manifest |
| GIT-009 | 跨平台 handoff | 有 commit、命令、产物和未验证项记录 |
| GIT-010 | 非门禁 | 未新增强制 PR、审批、required checks |

抽查任意三个小功能、一个任务和一个阶段。缺少提交或远程 push 记录则 FAIL。

## 4. 协议测试

| ID | 场景 | 预期 |
|---|---|---|
| PROTO-001 | 完整合法帧 | 解码一致 |
| PROTO-002 | 头分 1..31 bytes 到达 | 等待后正确解码 |
| PROTO-003 | 多帧粘包 | 逐帧解码 |
| PROTO-004 | magic 错误 | protocol error/close |
| PROTO-005 | version 不支持 | 明确 reject |
| PROTO-006 | metadata 超限 | 不分配超大内存 |
| PROTO-007 | payload 超限 | reject |
| PROTO-008 | 长度整数溢出 | reject |
| PROTO-009 | unknown optional field | 忽略 |
| PROTO-010 | illegal state message | state error |
| PROTO-011 | duplicate cancel | 幂等 |
| PROTO-012 | checkpoint 倒退 | reject |
| PROTO-013 | chunk offset 跳跃 | reject |
| PROTO-014 | manifest hash 不同 | reject/rescan |
| PROTO-015 | certificate mismatch | trust violation |

## 5. 路径安全

| ID | 输入 | 预期 |
|---|---|---|
| PATH-001 | `a/b.txt` | allow |
| PATH-002 | `../a.txt` | reject |
| PATH-003 | `/tmp/a` | reject |
| PATH-004 | `C:\a` | reject |
| PATH-005 | `\\server\share\a` | reject |
| PATH-006 | `a/../../b` | reject |
| PATH-007 | `con.txt` | Windows reject |
| PATH-008 | `file:stream` | reject |
| PATH-009 | `name.` / `name ` | Windows reject |
| PATH-010 | NUL/control | reject |
| PATH-011 | empty component | normalize/reject consistently |
| PATH-012 | depth > 128 | reject |
| PATH-013 | component > limit | reject |
| PATH-014 | 发送端 symlink 条目 | P0 skip and report |
| PATH-015 | 自定义接收根目录本身为 symlink/junction | reject with clear error |
| PATH-016 | Unicode NFC/NFD equivalent | deterministic conflict |
| PATH-017 | Emoji/中文 | allow |
| PATH-018 | case-only collision | resolve safely on target FS |

## 6. 文件大小与边界

```text
0 B
1 B
chunk-1
chunk
chunk+1
4 MiB
1 GiB
10 GiB+
sparse/logically huge where supported
```

每项验证：

- bytes；
- SHA-256；
- mtime policy；
- final name；
- no leftover inconsistent partial；
- memory；
- UI progress reaches exactly complete。

## 7. 文件夹

- empty folder；
- one file；
- nested 128 levels boundary；
- >boundary reject；
- 10,000 small files；
- hidden files；
- Chinese/Emoji；
- unreadable file；
- file deleted after manifest；
- file changed during send；
- symlink；
- hardlink；
- socket/FIFO on macOS；
- same names with case differences；
- source root renamed during scan。

## 8. 中断与恢复

| ID | 注入点 | 预期 |
|---|---|---|
| RESUME-001 | 10% 断网 | 从 durable offset 继续 |
| RESUME-002 | 90% 断网 | 不从 0 重传 |
| RESUME-003 | sender crash | 重启可恢复/明确失败 |
| RESUME-004 | receiver crash | `.part` 与 state 一致 |
| RESUME-005 | IP change | deviceId 重连 |
| RESUME-006 | Wi-Fi→Ethernet | 恢复 |
| RESUME-007 | sleep/wake | 恢复 |
| RESUME-008 | `.part` 被改 | 验证失败安全处理 |
| RESUME-009 | resume file 损坏 | 不越界写，提示 |
| RESUME-010 | manifest changed | 不错误续传 |
| RESUME-011 | target removed | 明确失败 |
| RESUME-012 | partial disk full | 保持可恢复或失败清晰 |

## 9. 冲突

- auto rename；
- overwrite file；
- overwrite directory/file mismatch；
- skip；
- ask；
- two simultaneous tasks same name；
- target appears after decision；
- antivirus locks target；
- case-insensitive collision；
- cancellation during commit；
- crash during commit。

原有文件在失败情况下不得损坏。

## 10. 配对与信任

- normal six-digit；
- wrong；
- 过期后重新发起；
- 错码；
- duplicate names；
- fingerprint change；
- revoked；
- trust store corruption；
- concurrent requests；
- discovery spoof cannot transfer；
- auto-accept only trusted；
- peer version mismatch。

## 11. 输入回归

在无传输/满速传输/哈希压力下：

- mouse motion；
- enter/leave screen；
- left/right/middle/back/forward；
- vertical/horizontal scroll；
- normal keys；
- Ctrl/Alt/Shift/Win/Command/Option；
- repeat；
- IME；
- hotkeys；
- release all keys after disconnect；
- sleep/wake；
- multiple monitors/DPI。

记录：

- input error；
- dropped/stuck keys；
- subjective latency；
- 可测量时事件延迟 p50/p95/p99。

## 12. 性能

### Memory

- 10GB 文件；
- 任务 1/2；
- chunk 1MiB；
- 监测 idle 与 peak；
- 目标新增 ≤256MiB；
- 发现线性增长视为 FAIL。

### Throughput

- loopback；
- 1GbE；
- Wi-Fi；
- SSD→SSD；
- CPU/hash utilization；
- 不设脱离硬件的绝对门槛，记录相对网络/磁盘吞吐。

### Input under load

- file throughput maximum；
- user continuous typing/mouse；
- input p95/subjective；
- 必要时降低 file concurrency。

### Small files

- 10k files；
- manifest memory/time；
- UI responsiveness；
- history overhead。

## 13. 平台矩阵

| Sender | Receiver | P0 |
|---|---|---:|
| Windows x64 | macOS arm64 | 必须 |
| macOS arm64 | Windows x64 | 必须 |
| Windows x64 | Windows x64 | 推荐 |
| macOS arm64 | macOS arm64 | 推荐 |
| macOS Intel | Windows | P1 |
| Windows ARM64 | macOS | P1 |

## 14. 安装验收

### Windows

- clean install；
- VC runtime；
- firewall allow/deny；
- start at login；
- upgrade；
- uninstall；
- config preserve；
- unsigned/signed behavior。

### macOS

- drag/install；
- permissions；
- local network；
- signed/notarized；
- upgrade permissions；
- uninstall manual behavior；
- Apple Silicon clean machine。

## 15. Codex Release Acceptance

Codex 只有完成以下自动化与产物准备后，才可把版本交给用户最终验收：

```text
[ ] Phase 0 baseline evidence
[ ] Bidirectional input
[ ] Pair/trust/reconnect
[ ] Win→Mac file/folder
[ ] Mac→Win file/folder
[ ] Pause/resume/cancel
[ ] Disconnect resume
[ ] Hash verification
[ ] Path security suite
[ ] 10GB+ bounded memory
[ ] Input under transfer
[ ] Windows package
[ ] macOS package
[ ] License/source package
[ ] Known issues documented
```


## 16. 最终用户验收

A0 一次性交付：

```text
Windows x64 unsigned installer
macOS arm64 unsigned DMG/App
source archive
full commit SHA
SHA256SUMS.txt
artifact-manifest.json
Actions run IDs
automated-test-report.md
known-issues.md
FINAL_ACCEPTANCE.md
```

用户最终只验证：

1. Windows 安装/启动/卸载；
2. Mac 安装、首次打开和权限授权；
3. Windows→Mac 与 Mac→Windows 鼠标、键盘、滚轮；
4. 文本与图片剪贴板；
5. 单文件、多文件、文件夹双向传输；
6. 暂停、继续、取消；
7. 大文件与断线续传；
8. 真实使用中键鼠是否流畅。

最终验收失败时，用户只提交现象和日志；Codex负责创建修复任务、提交、推送并重新生成双平台包。
