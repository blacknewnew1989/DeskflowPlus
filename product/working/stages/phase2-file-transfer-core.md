# Phase 2 文件传输内核报告

- 当前结论：`IN_PROGRESS`（实现与本地验证 PASS，等待阶段标签双平台 Actions）
- 集成分支：`product/relaydesk-v1`
- 候选提交：`bd342ed5e`
- 目标标签：`relaydesk-phase2-20260813-03`
- 产物性质：unsigned/ad-hoc 内部包；签名凭据不阻塞本阶段。

## 已完成范围

| 范围 | 结果 | 证据 |
| --- | --- | --- |
| CORE-001 / FILE-001/002/007 | `PASS` | 严格 CBOR 控制消息、冻结 32-byte RDFT header、FrameCodec、HELLO/Auth/Capabilities；未知 major/type、畸形/尾随 CBOR 与长度上限均有负例 |
| FILE-003 | `PASS` | 统一 PathPolicy，拒绝 absolute/drive/UNC/`..`/ADS/reserved/control，NFC/case-fold collision key，接收根 containment |
| FILE-008/015/016 | `PASS` | 1 MiB 流式单文件、多文件/文件夹 manifest；确定性 CBOR digest、100,000 entries/64 MiB metadata hard ceiling、分页与冻结向量 |
| FILE-006 | `PASS` | 独立 QSslServer/QSslSocket 文件通道，复用 Deskflow identity 与 trust pinning；认证前不放行 RDFT frame |
| FILE-009 | `PASS` | offer/accept/reject 状态机、超时和幂等决策 |
| FILE-010/017/018 | `PASS` | 流式 sender、1-frame pending、high/low water backpressure、source mutation detection；网络回调不读盘/哈希 |
| FILE-011/012 | `PASS` | `.part` receiver、逐帧顺序/offset 校验、最终 SHA-256 与 atomic rename |
| CONFLICT-001/002 | `PASS` | 并发 reservation、AutoRename/Skip/Ask/Overwrite；resolver 不删除或修改现有文件 |
| TEST-002/003 | `PASS` | 10 GiB logical sparse bounded-memory benchmark；输入调度与 network callback I/O ownership probe |

## 本地执行证据

- Qt 6.7.3 / MinGW 13.1，整仓等价 `QT_NO_KEYWORDS` probe 真实编译。
- `RelayDeskConflictResolverTests`：PASS。
- `RelayDeskPartialCleanupPolicyTests`：PASS（Phase 3 回归叠加验证）。
- `RelayDeskTransferProgressPublisherTests`：PASS（Phase 3 回归叠加验证）。
- `RelayDeskLargeFileBoundedBenchmark`：PASS；logical size 10,737,418,240 bytes，仅在高水位前读取约 4 MiB。
- `RelayDeskInputPriorityUnderTransferBenchmark`：PASS；transfer saturated 时 callback 不执行 sender 读盘/哈希。
- `RelayDeskTransferRecoveryMatrixTests`：PASS（Phase 3 恢复组合叠加验证）。
- 代理分支上真实 Qt full transfer harness 累计最高 `18/18 PASS`；当前产品树的新增目标逐项重新编译运行。
- `git diff --check` 与开发材料验证 PASS。
- 当前产品树 Qt 6.7.3 / MinGW 13.1：文件传输 probe `21/21 PASS`，GUI/平台/发现组合 probe `10/10 PASS`。

## 标签构建诊断

- `relaydesk-phase2-20260813-01` / Actions run `31627604618`：双平台 Configure PASS，但 Build FAIL，后续 package 与有效 CTest 因目标未生成而跳过/Not Run。
- Windows 首个失败点：`WindowsFirewallProbe.cpp` 使用 IPv6 MIB 表类型，但 Microsoft SDK 只在 Winsock2 IPv6 定义已加载时声明该类型；提交 `d933716fc` 按 SDK include 顺序补入 `ws2tcpip.h`。
- macOS 首个失败点：发现端口条件表达式由 `quint16` 与整型字面量共同推导成 `int`，Clang 在 `DeviceInfo` 列表初始化时拒绝运行时窄化；提交 `1db34ff6d` 将变量显式定为 `quint16`，且原有 `1..65535` 范围检查保持不变。
- 两端失败均发生在编译阶段，未将该 run 的诊断测试步骤误记为 PASS；新候选标签必须重新完成 build、package、CTest 与 artifact。
- `relaydesk-phase2-20260813-02` / Actions run `31653533200`：macOS Build/Package/App stage 均 PASS，但 CTest `72/74`，恢复矩阵在 `main()` 前 SEGFAULT，Windows start-at-login 的 5 个平台无关 fake-adapter 用例因宿主路径判断失败；Windows 则在 WiX custom action 的 MSVC 预处理阶段失败。
- 恢复矩阵根因是跨 translation unit 动态初始化顺序：测试全局初始化调用 `DeviceId::fromString()` 时，另一个 translation unit 的全局 `QRegularExpression` 尚未构造。提交 `da0428940` 改为首次调用时构造的函数局部 static；恢复矩阵本地重复 10/10 PASS。
- start-at-login 根因是 Windows 命令契约错误依赖宿主 `QFileInfo::isAbsolute()`/native separator；提交 `5845dbc3c` 改为平台无关的 drive/UNC 规范化，注入式用例本地 PASS。
- WiX 根因是 MSVC legacy preprocessor 不接受 custom action 的 C++20 `__VA_OPT__`；提交 `bd342ed5e` 仅对 MSVC target 启用 `/Zc:preprocessor`，并由 packaging contract 测试锁定。

## 架构边界

- 文件传输与 Deskflow 键鼠/剪贴板连接、队列和缓冲区独立。
- sender/manifest 使用流式 I/O，不把大文件读入内存。
- receiver 只写受 PathPolicy 约束的 `.part`，完整 SHA-256 成功后原子提交。
- discovery 广播指纹不当作 pinned trust；文件 TLS 在 pinning 通过前不交付 frame。
- 不含账号、云端、RBAC、病毒扫描、DLP、文件白名单或强制业务大小阈值。

## 阶段完成条件

- [x] 文件传输核心以独立小提交合入并推送。
- [x] 当前可用 Qt/MinGW 定向测试与 benchmarks 通过。
- [x] 生成 Phase 2 报告。
- [x] 创建并推送 `relaydesk-phase2-20260813-01`（诊断 run 失败，已保留证据）。
- [x] 创建并推送 `relaydesk-phase2-20260813-02`（第二轮诊断 run 失败，已保留证据）。
- [ ] 创建并推送 `relaydesk-phase2-20260813-03`。
- [ ] 标签 Windows x64 / macOS arm64 configure、build、CTest、package、artifact 全部 PASS。
- [ ] 下载阶段 Release assets 并记录 API digest/本地 SHA。

## NOT_RUN

- `NOT_RUN`：两台真实 Windows/macOS 机器之间的完整文件传输。
- `NOT_RUN`：真实网络 IP 切换、Wi-Fi/Ethernet 切换、sleep/wake。
- `NOT_RUN`：打包 App 内由 GUI intent 到文件 service 的完整运行时链；COMP 审计已明确区分核心实现与启动接线，后续阶段继续完成。
- `NOT_RUN`：正式 Windows/Apple 签名和公证。

这些真机/凭据项不阻塞 Phase 2 内核代码、测试与 unsigned artifacts。
