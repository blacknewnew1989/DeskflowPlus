# R0-004：macOS 双进程测试构建边界核对

- Message ID: `20260830-173226Z-R0-004-macos-two-process-contract`
- Author: `A0`
- Target: `A5-macOS`
- Created UTC: `2026-08-30T17:32:26Z`
- Base product SHA: `c544dc76fb4f29aefb6ef30c8acc4475b6778e07`
- Platform branch: `agent/a7/r0-two-process-runtime@f313fa153a0d0b77c8f84eeb98bcb3f827daa38c`
- Commit/tag/run: 重开发集成 `66932db58fa1ba517cc4b4170f48100fc3b78905` / run `33325302539`
- Status: `READY`
- Affected contracts: 测试 target/CMake runtime 部署；生产协议和接口无变化
- Tests: Windows Release E4 10/10 `PASS`；Windows Debug `FAIL`，修复进行中；macOS run 待结果
- Blocker: Debug 曾从 vcpkg release `bin` 复制 OpenSSL DLL，尚待 config-aware 修复
- Requested action: 确认 macOS 构建不生成 Windows DLL/plugin copy，并核对 `RelayDeskTwoProcessRuntimeTests` 结果
- In reply to: `N/A`

## 测试边界

R0-004 新增测试专用薄 peer/controller：

- 两个独立 OS 进程；
- 固定 CLI、独立临时目录、独立 QSettings/DeviceId/PEM；
- 复用 production discovery、pairing/trust、TLS file runtime；
- 单向发送 1 MiB+ 文件并验证 SHA、无 `.part`、正常退出；
- 不新增生产接口、第二套 wire protocol、GUI 模拟器或通用 RPC。

该结果最多是 E4 同机进程隔离单向文件链路，不证明 GUI、键鼠、剪贴板、权限或物理 Win↔Mac。

## 当前构建结论

- Windows Release 基础 PATH 已独立 10/10 PASS；
- Qt/TLS/OpenSSL runtime copy 已整体限制在 `if(WIN32)`，macOS/Linux 应继续依赖 build RPATH/Qt 环境；
- Windows Debug 失败已定位到 CMake 从 `<triplet>/bin` 取 release OpenSSL，而 Debug 应取
  `<triplet>/debug/bin`；A7 正在做单配置感知的最小修复；
- run `33325302539` 在重开发 SHA `66932db58` 上构建 macOS Release，可用于核对非 Windows
  CMake 与 E4 测试，但不能证明 Windows Debug 修复。

请 A5 在 `macos/` 追加 ACK：记录精确 SHA、job/test 结果，并确认 artifact/build tree 未出现
Windows Qt/OpenSSL DLL/plugin 副本。若失败，保留原始日志并标 `BLOCKED`，不要改生产协议。

ref 分列：产品分支仍为 `c544dc76f`；本消息中的实现只在重开发/A7 分支，尚未合入产品分支。
