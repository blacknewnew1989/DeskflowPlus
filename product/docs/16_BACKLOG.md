# 16 产品 Backlog

## 规则

- `P-1` 是全自动仓库准备；`P0` 是上游双平台基线；Phase 1～5 依次执行。
- Owner 可以拆子任务，但不得改变验收。
- 依赖未满足不得标 Done。
- 测试不可运行时写 NOT_RUN/BLOCKED。
- A0 从本表同步 `TASK_BOARD.md`，不复制无关远期任务到 Ready。
- 每个小功能完成必须 commit；每个任务 Done 必须 push 任务分支并由 A0 合并/push 集成分支。

| ID | Phase | 任务 | Owner | 依赖 | 验收 |
|---|---|---|---|---|---|
| AUTO-001 | P-1 | 定位当前 Git root、origin、分支和仓库形态 | A0 | 无 | bootstrap report |
| AUTO-002 | P-1 | 自动添加 upstream、fetch/验证 v1.26.0 | A0+A1 | AUTO-001 | tag=`760e3b9` |
| AUTO-003 | P-1 | 自动从固定 tag 建立 product 分支并安装开发资料 | A0 | AUTO-002 | 本地结构正确 |
| AUTO-004 | P-1 | 自动安装 GitHub Actions 和平台脚本 | A0+A7 | AUTO-003 | workflow 已提交 |
| AUTO-005 | P-1 | 初始化提交并推送 product 分支 | A0 | AUTO-004 | origin 可见 SHA |
| AUTO-006 | P-1 | 触发并监控首次 Win/Mac workflow | A0+A7 | AUTO-005 | run/log/artifact 记录 |
| BASE-001 | P0 | 核查上游 tag/commit/license/clean tree | A1 | AUTO-006 | 证据写入 PROJECT_STATE |
| BASE-002 | P0 | Windows 原版 Release 构建 | A4 | BASE-001 | 命令、日志、artifact 可复现 |
| BASE-003 | P0 | macOS 原版 Release 构建 | A5 | BASE-001 | 命令、日志、app 可复现 |
| BASE-004 | P0 | 核查 apps/libs/CMake/tests/package 结构 | A1 | BASE-001 | actual integration map |
| BASE-005 | P0 | Windows Server→Mac Client 联调 | A4+A5 | BASE-002,003 | 输入/滚轮/文本剪贴板 PASS |
| BASE-006 | P0 | Mac Server→Windows Client 联调 | A4+A5 | BASE-002,003 | 输入/滚轮/文本剪贴板 PASS |
| BASE-007 | P0 | 记录基线性能和已知问题 | A7 | BASE-005,006 | 报告可区分上游/产品 |
| BRAND-001 | P1 | 集中品牌配置与生成入口 | A1+A3 | BASE-004 | 无全仓盲替换 |
| I18N-001 | P1 | 中文翻译基线与 key 规范 | A3 | BASE-004 | 主要页面中文可用 |
| DEV-001 | P1 | DeviceIdentity/deviceId | A2 | BASE-004 | 重启稳定、重置可控 |
| DEV-002 | P1 | TLS identity/fingerprint adapter | A2 | BASE-004 | 复用/统一上游身份 |
| DISC-001 | P1 | UDP discovery codec | A2 | DEV-001 | 非法包/限长测试 |
| DISC-002 | P1 | 多网卡 discovery service | A2 | DISC-001 | Win/Mac 同网发现 |
| DISC-003 | P1 | 设备在线/offline 模型 | A2 | DISC-002 | 超时与重上线 |
| DISC-004 | P1 | 手动地址回退 | A2+A3 | DISC-003 | 发现关闭仍可连接 |
| PAIR-001 | P1 | pairing 状态机 | A2 | DEV-002 | 状态/超时测试 |
| PAIR-002 | P1 | 简单六位确认码与设备指纹交换 | A2 | PAIR-001 | 正常/错码/过期/重新配对 |
| PAIR-003 | P1 | TrustedDeviceStore | A2 | PAIR-001 | 原子保存/撤销 |
| PAIR-004 | P1 | TLS fingerprint pinning | A2 | PAIR-003 | 变化 hard fail |
| PAIR-005 | P1 | 自动重连/地址选择 | A2 | PAIR-004,DISC-003 | IP 变化恢复 |
| UI-001 | P1 | 设备首页模型 | A3 | DISC-003 | online/trusted 状态 |
| UI-002 | P1 | 设备卡片与配对入口 | A3 | UI-001,PAIR-001 | 完整配对 flow |
| UI-003 | P1 | 屏幕布局整合 | A3+A1 | BASE-004 | 不破坏上游布局 |
| UI-004 | P1 | 权限/错误状态 | A3+A4+A5 | UI-001 | 可操作提示 |
| FILE-001 | P2 | RDFT FrameCodec | A6 | BASE-004 | 半包/粘包/limits tests |
| FILE-002 | P2 | CBOR message types | A6 | FILE-001 | test vectors |
| FILE-003 | P2 | PathPolicy shared core | A6 | BASE-004 | 安全 corpus PASS |
| FILE-004 | P2 | Windows path adapter | A4 | FILE-003 | reserved names/非法路径 |
| FILE-005 | P2 | macOS path adapter | A5 | FILE-003 | Unicode/接收目录 |
| FILE-006 | P2 | file TLS listener/client | A6+A2 | PAIR-004,FILE-001 | pinned loopback |
| FILE-007 | P2 | capability negotiation | A6 | FILE-006 | limits 取交集 |
| FILE-008 | P2 | ManifestBuilder 单文件 | A6 | FILE-003 | 元数据/hash |
| FILE-009 | P2 | offer/accept/reject | A6 | FILE-007,008 | 纵向控制闭环 |
| FILE-010 | P2 | TransferSender stream | A6 | FILE-009 | bounded chunks |
| FILE-011 | P2 | TransferReceiver .part | A6 | FILE-009,003 | root 内安全写 |
| FILE-012 | P2 | SHA-256 verify/commit | A6 | FILE-010,011 | hash mismatch 不提交 |
| FILE-013 | P2 | Win→Mac 单文件 E2E | A4+A5+A6 | FILE-012 | 0B/1GB PASS |
| FILE-014 | P2 | Mac→Win 单文件 E2E | A4+A5+A6 | FILE-012 | 0B/1GB PASS |
| FILE-015 | P2 | 多文件/文件夹 manifest | A6 | FILE-008 | 空目录/10k files |
| FILE-016 | P2 | manifest paging/limits | A6 | FILE-015 | 有界内存 |
| FILE-017 | P2 | queue/concurrency/backpressure | A6 | FILE-010 | 输入压力达标 |
| FILE-018 | P2 | source mutation detection | A6 | FILE-010 | SOURCE_CHANGED |
| RESUME-001 | P3 | ResumeStore atomic state | A6 | FILE-011 | 重启可读 |
| RESUME-002 | P3 | durable checkpoint | A6 | RESUME-001 | offset 不超前 |
| RESUME-003 | P3 | reconnect resume query | A6+A2 | RESUME-002,PAIR-005 | 断网续传 |
| RESUME-004 | P3 | sender/receiver restart | A6+A7 | RESUME-003 | 真重启测试 |
| RESUME-005 | P3 | partial cleanup policy | A6+A3 | RESUME-001 | 7天/用户控制 |
| CTRL-001 | P3 | pause/resume/cancel 状态机 | A6 | FILE-017 | 幂等/竞态测试 |
| CONFLICT-001 | P3 | auto rename resolver | A6 | FILE-003 | 并发安全 |
| CONFLICT-002 | P3 | overwrite/skip/ask | A6+A3 | CONFLICT-001 | 原文件保护 |
| HIST-001 | P3 | 有界传输历史 | A6 | FILE-012 | 1000条/90天 |
| UI-005 | P3 | 发送选择/应用内拖放 | A3 | FILE-009,UI-002 | 卡片 drop |
| UI-006 | P3 | Incoming Offer dialog | A3 | FILE-009 | 接收/拒绝/策略 |
| UI-007 | P3 | Transfer Center model | A3 | FILE-010 | 状态/进度 |
| UI-008 | P3 | 速度/ETA/通知 | A3+A6 | UI-007 | 5Hz throttle |
| UI-009 | P3 | 历史/打开目录/重试 | A3 | HIST-001 | 完整用户闭环 |
| WIN-001 | P4 | Windows firewall/port diagnostics | A4 | FILE-006 | 允许/拒绝可诊断 |
| WIN-002 | P4 | Windows start-at-login integration | A4 | BASE-004 | 升级稳定 |
| WIN-003 | P4 | Windows package productization | A4+A7 | BRAND-001 | clean install |
| WIN-004 | P4 | Windows signing parameters | A4+A7 | WIN-003 | 无 secret 入库 |
| MAC-001 | P4 | macOS Local Network 权限引导 | A5+A3 | DISC-002 | 拒绝/授权 flow |
| MAC-002 | P4 | macOS Accessibility/Input 权限回归 | A5 | BASE-003 | 升级后检测 |
| MAC-003 | P4 | macOS bundle/DMG productization | A5+A7 | BRAND-001 | clean install |
| MAC-004 | P4 | codesign/notary parameters | A5+A7 | MAC-003 | 无凭据可 unsigned |
| TEST-001 | P4 | protocol/path complete suite | A7 | FILE-003,012 | CI PASS |
| TEST-002 | P4 | 10GB bounded memory benchmark | A7+A6 | FILE-017 | 报告 |
| TEST-003 | P4 | input-under-transfer benchmark | A7+A6 | FILE-017 | 无明显卡顿 |
| TEST-004 | P4 | sleep/network/IP interruption matrix | A7 | RESUME-004 | Win/Mac |
| TEST-005 | P4 | installer upgrade/uninstall | A7+A4+A5 | WIN-003,MAC-003 | 配置保留 |
| GIT-001 | P4 | 校验小功能 commit/任务 push/Phase push 记录 | A0+A7 | 所有P0 | 抽查通过 |
| CI-001 | P4 | Windows/macOS Actions 构建和 artifact 自动下载 | A0+A7 | WIN-003,MAC-003 | 同一 SHA 双平台产物 |
| CI-002 | P4 | 生成 artifact manifest 与 SHA256SUMS | A7 | CI-001 | 可追溯 |
| REL-001 | P4 | license/source archive | A7+A1 | 所有P0 | REUSE/source complete |
| REL-002 | P4 | RC checklist/known issues | A0+A7 | TEST-001..005 | RC 可发布 |
| P1-CLIP-001 | P5 | remote file reference schema | A6 | REL-002 | 短期/peer 绑定 |
| P1-CLIP-002 | P5 | Windows CF_HDROP/OLE bridge | A4 | P1-CLIP-001 | Explorer copy |
| P1-CLIP-003 | P5 | macOS NSPasteboard bridge | A5 | P1-CLIP-001 | Finder paste |
| P1-EDGE-001 | P5 | screen edge drop overlay | A3+A4+A5 | REL-002 | 默认目录投递 |
| P1-SHELL-001 | P5 | Explorer SendTo/context integration | A4 | REL-002 | 独立可禁用 |
| P1-SHELL-002 | P5 | Finder Quick Action | A5 | REL-002 | 主应用 IPC |
| P1-ARCH-001 | P5 | Intel Mac build/validation | A5+A7 | REL-002 | 条件目标 |
| P1-ARCH-002 | P5 | Windows ARM64 build/validation | A4+A7 | REL-002 | 条件目标 |

## 首个可用版本最短路径

```text
AUTO-001..006
 -> BASE-001..007
 -> DEV/DISC/PAIR
 -> UI-001..004
 -> FILE-001..014
 -> FILE-015..018
 -> RESUME/CTRL/CONFLICT/HIST
 -> UI-005..009
 -> WIN/MAC
 -> TEST/REL
```

P1-CLIP、P1-EDGE、P1-SHELL、P1-ARCH 不能阻塞首个可用版本。
