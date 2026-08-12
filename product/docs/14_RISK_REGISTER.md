# 14 风险登记表

| ID | 风险 | 概率 | 影响 | 预防/缓解 | Owner | 触发指标 |
|---|---|---:|---:|---|---|---|
| R-001 | 未建立原版基线就开发，无法区分上游问题 | 高 | 高 | 优先建立基线；本机不可用时用 Actions/NOT_RUN，不阻塞独立任务 | A0/A1 | 双平台无可复现原版构建 |
| R-002 | 大文件堵塞键鼠通道 | 中 | 极高 | 独立 TLS socket/thread/queue、压测 | A6 | 传输时输入 p95 明显恶化 |
| R-003 | Deskflow 真实结构与文档假设不同 | 高 | 中 | A1 源码核查、actual map | A1 | target/class 不存在 |
| R-004 | macOS 权限升级后失效 | 高 | 高 | 稳定 bundle ID、升级测试、引导 | A5 | 新版 event tap 不工作 |
| R-005 | Windows 防火墙阻断发现/文件通道 | 中 | 中 | 诊断、安装规则、手动地址 | A4 | peer 可发现但无法 connect |
| R-006 | 路径越出接收目录 | 低 | 高 | 相对路径规范化、专用接收根目录、链接条目 P0 跳过 | A6/A7 | 文件落到 root 外 |
| R-007 | 断点状态与 `.part` 不一致 | 中 | 高 | 原子 state、durable checkpoint、重哈希 | A6 | resume 后 hash mismatch |
| R-008 | 10k 小文件导致 UI/内存异常 | 高 | 中 | manifest 分页、有界模型、批量 UI | A3/A6 | GUI 卡顿或内存线性增长 |
| R-009 | 源文件传输中变化导致混合内容 | 中 | 高 | size/mtime/file identity 前后检查 | A6 | hash/size 与 offer 不一致 |
| R-010 | 自动重命名并发竞态覆盖文件 | 中 | 高 | 最终原子 reserve/rename、冲突重试 | A6 | 两任务得到同一目标 |
| R-011 | 产品重品牌导致上游同步冲突 | 高 | 中 | 集中 branding、不全仓 rename | A1/A3 | 每次 merge 大量文本冲突 |
| R-012 | GPL 分发不合规 | 中 | 极高 | source package、REUSE、法务复核 | A0/A7 | binary 无对应 source |
| R-013 | 正式名称存在商标冲突 | 中 | 高 | 临时代号、发布前检索 | Product | 准备公开发布 |
| R-014 | macOS notarization/Windows signing 凭据缺失 | 高 | 中 | 直接产出内部 unsigned 包；正式分发时再注入凭据 | A4/A5/A7 | RC 无签名证书 |
| R-015 | 真正跨系统原生拖拽范围失控 | 高 | 高 | 明确 P2/独立立项 | A0 | P0 开始 OLE/NSDragging bridge |
| R-016 | 配对实现过度复杂拖慢首版 | 中 | 中 | 复用 TLS/指纹，仅做简单六位确认 | A2 | 配对代码膨胀或引入新密码学依赖 |
| R-017 | 多网卡/IPv6 导致发现错误 | 高 | 中 | 手动地址回退、多接口测试 | A2 | 同网段看不到或选错地址 |
| R-018 | 证书与 Deskflow 现有 TLS 体系冲突 | 中 | 高 | Phase 0 核查、统一 identity owner | A1/A2 | 两套不一致 trust store |
| R-019 | file module crash 拖垮 input | 中 | 高 | 清晰线程边界、异常处理；必要时后续拆进程 | A6 | 文件错误导致 core exit |
| R-020 | hash CPU 占用影响输入 | 中 | 中 | worker/限并发/优先级/benchmark | A6/A7 | CPU 满载时输入卡顿 |
| R-021 | macOS/Windows 文件名语义不一致 | 高 | 中 | 协议规范化、target policy、冲突测试 | A6 | 接收创建失败/覆盖 |
| R-022 | 外置盘/网络盘中途断开 | 中 | 中 | I/O 错误、可恢复状态、UI | A4/A5 | target unavailable |
| R-023 | history/resume 数据无限增长 | 中 | 中 | 90 天/1000 条、partial cleanup | A6 | app data 持续膨胀 |
| R-024 | 日志泄露文件名/路径/配对码 | 中 | 高 | 结构化脱敏、diagnostic opt-in | A7 | logs 出现 secrets |
| R-025 | 上游新 release 诱发中途升级 | 高 | 中 | v1.26.0 锁定，稳定后同步 | A0 | 功能 PR 顺带 upgrade |
| R-026 | CI 只编译不真机验证 | 高 | 高 | Win↔Mac E2E 门槛、NOT_RUN 诚实标记 | A7 | RC 无真机报告 |
| R-027 | App 内拖放接收云占位文件 | 中 | 中 | 检测本地可读性/异步 materialize 提示 | A3/A6 | QFile 无法打开 |
| R-028 | 文件过大导致 uint64/size 转换错误 | 低 | 极高 | checked arithmetic、边界测试 | A6/A7 | length overflow |
| R-029 | UI 高频进度导致性能问题 | 中 | 中 | 5Hz throttle、snapshot diff | A3 | 进度时 GUI CPU 高 |
| R-030 | 信任撤销后活跃传输继续 | 低 | 高 | revoke 关闭 session/取消任务 | A2/A6 | revoked peer still writes |

## 风险处理规则

- 高影响问题优先增加自动测试；只有必须依赖物理设备的结果进入最终验收，不设置人工审批门禁。
- 风险触发后 A0 在 ADR/PROJECT_STATE 记录，不只在聊天中说明。
- 不以“增加复杂审批系统”代替技术缓解。
- 风险降低后保留历史，不删除记录。
