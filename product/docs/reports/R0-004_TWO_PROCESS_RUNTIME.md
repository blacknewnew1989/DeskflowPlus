# R0-004 同机双进程运行时纵向验证

## 1. 结论

`R0-004` 在限定范围内为 `PASS`：两个独立 OS 进程通过现有 production discovery、pairing/trust
和独立 TLS file runtime，完成单向 1 MiB+ 文件传输、接收、SHA-256 校验、无残留 `.part` 和
正常退出。

该结论属于 E4 同机进程隔离证据，不证明：

- 完整 GUI、设备卡拖放或托盘/menu bar；
- 键鼠、滚轮、文本/图片剪贴板；
- 暂停、继续、取消、断点续传、多文件或文件夹；
- 物理 Windows 与 macOS 互通；
- 系统权限、SmartScreen、Gatekeeper 或签名。

上述未覆盖项继续使用 `NOT_RUN` 或 `FINAL_ACCEPTANCE_REQUIRED`。

## 2. 精确版本

| 项目 | SHA / run |
|---|---|
| 产品分支（未合入） | `product/relaydesk-v1@c544dc76fb4f29aefb6ef30c8acc4475b6778e07` |
| A7 最终提交 | `agent/a7/r0-two-process-runtime@2cc7aaaa924bd3af839e2538860dbff1f2cd32f1` |
| A0 集成关闭顺序提交 | `34e36f075` |
| 当前重开发提交 | `agent/a0/redevelop-p0@c9d5dceb8e09a6485b1e4c8defca6c7f2bc42358` |
| 完整双平台 run | `33326619207` (`SUCCESS`) |

产品分支仍为 `c544dc76f`，不得把本报告写成产品分支验收完成。

## 3. 实现边界

新增内容仅位于 `src/unittests/relaydesk/app/`：

- `TwoProcessRelayDeskPeer.cpp`：固定 sender/receiver 角色的薄 `QCoreApplication` 宿主；
- `TwoProcessRelayDeskRuntimeTests.cpp`：通过 `QProcess` 启动两个 peer 的 QtTest 控制器；
- `CMakeLists.txt`：注册 peer/test 和 Windows 测试 runtime 部署。

peer 使用固定 CLI、独立临时目录和 result JSON，不提供通用 RPC，不新增生产 wire protocol 或
生产接口。每个 peer 使用不同 QSettings、DeviceId 和自签名 PEM；真实 UDP probe、配对/信任、
TLS file channel 和接收 runtime 均复用生产实现。

receiver 完成内容校验后等待 production `peerDisconnected` 再退出；sender/receiver 的终态清理通过
`QTimer::singleShot(0)` 返回事件循环后执行，避免在 production signal 栈内同步 stop 触发 Qt Debug
断言。这里的 `0` 是事件循环调度，不是任意时间等待。

控制器使用一个覆盖启动和运行的 15 秒 deadline；失败时 kill 后 `waitForFinished`，不得留下 test
host 或 peer。

## 4. Windows runtime 闭包

Windows 基础 PATH 下测试可独立运行：

- Qt controller/peer DLL 在 `WIN32` 内部署；
- TLS `qopensslbackend` plugin 只部署到 peer；
- Debug 只复制 vcpkg `<triplet>/debug/bin` 的 OpenSSL；
- Release 只复制 `<triplet>/bin` 的 OpenSSL；
- macOS/Linux 不生成 Windows DLL/plugin copy 命令。

最终独立验收 `2cc7aaaa9`：

| 配置 | OpenSSL runtime | 单轮 | 重复 | 残留 |
|---|---|---:|---:|---:|
| Debug / `x64-windows` | `libcrypto` 8,207,872 B；`libssl` 1,838,080 B | PASS | 10/10 PASS | 0 |
| Release / `x64-windows-release` | `libcrypto` 4,835,840 B；`libssl` 829,952 B | PASS | 10/10 PASS | 0 |

A0 在集成 worktree 的 fresh Debug build 重新验证：基础 PATH、OpenSSL 字节数与 debug source 一致，
`RelayDeskTwoProcessRuntimeTests` 10/10 PASS，总计 9.20 秒，peer/controller 残留均为 0。

## 5. 同 SHA 双平台 Actions

run `33326619207` 的 `headSha` 为
`c9d5dceb8e09a6485b1e4c8defca6c7f2bc42358`：

| 平台 | CTest | R0-004 目标 | 包/生命周期 |
|---|---|---|---|
| Windows x64 | 100/100 PASS，37.06 秒 | test #98 PASS，0.47 秒 | package、TEST-005 成功 |
| macOS arm64 | 101/101 PASS，31.55 秒 | test #99 PASS，0.51 秒 | App/DMG 和 lifecycle 成功 |

Artifacts：

- Windows `9736565882`，36,317,790 bytes，API digest
  `sha256:e5eb1a8744f7b0d88d383fc09d4849a94b31c967b845115955b4178c7850c88a`；
- macOS `9736477844`，65,931,161 bytes，API digest
  `sha256:1c9f3ea32647ef041d1612fb0c65b0ccef1feb5105e8a5a9b5e650f0379e2b23`；
- macOS lifecycle `9736572377`，12,574 bytes，API digest
  `sha256:4f21c5d11c2afd2f257f3fc0ed48160c5ecd1eceb55eeb43ead21eb2971411ea`。

分支 run 的 Release job 按规则 `skipped`，本报告不是阶段标签或发布候选证据。

## 6. 跨平台沟通

A0 通过 `coord/platform-sync` 即时发送 R0-004 构建边界和同 SHA run 映射；A5 在
`product/working/platform-sync/macos/20260830-182027Z-R0-002-R0-004-run-split-ack.md`
回传 macOS 101/101、R0-004 #99 PASS，并确认 macOS App ZIP 未出现 `.dll`、`.exe` 或
`qwindows` 污染。ACK commit 为 `d12afd4cca56ae0d366d554bf35edc1b18fded3c`。

## 7. 后续

- 将同一薄宿主扩展到暂停/继续/取消、多文件/文件夹和断线续传前，先完成 R0-002 根因修复；
- 每个新增 E4 场景保持独立小提交和 Windows/macOS 同 SHA 证据；
- 不把同机进程测试升级为物理跨平台 PASS。
