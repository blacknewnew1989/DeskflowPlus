# WIN-021 Windows MSI 最低运行库要求

## 结论

Windows MSI 的最低 VC++ runtime 条件已从 v14.51 修正为 v14.44。本机 runtime 为
`14.44.35211`；修复包可完成安装、修复和卸载，未要求用户手动升级运行库。

## 问题与根因

旧 `CMakeLists.txt` 读取构建机注册表，并将当时较新的 Runtime minor 固化进 MSI；本机
连续两次安装均以 exit `1603` 失败，日志显示最低门槛为 v14.51，高于主机已安装的
v14.44.35211。修复后从 MSVC 19.44 推导 Runtime 14.44；云端 MSI 反编译复核确认
LaunchCondition 为 v14.44，且不再存在 v14.51 要求。

## 本机修复验收

| 项目 | 事实 | 结果 |
|---|---|---|
| 修复 MSI | SHA-256 `1A2404AAD157F821DBC3ACA28B20A529EA1B1D986FB4D03FAE6A4ACF7B86FCD0` | PASS |
| 安装 | exit `0`，GUI 实际启动 | PASS |
| 修复 | exit `0` | PASS |
| 安装后注册 | RelayDesk 服务、两条防火墙规则和开始菜单均存在 | PASS |
| 卸载 | exit `0`，服务、防火墙、开始菜单和安装残留均不存在 | PASS |
| 用户配置 | 原 SHA-256 `F14E7A4612680A8DCE5099BD350D238D7ED785A3D6E97F90D98629F0B3A08E4C` 恢复匹配 | PASS |

## Hosted 回归

产品实现 `1b1a24739dea3775d64fa7987d30e9b37372a5c1` 的分支 run `32433749495` 为
`SUCCESS`。Windows job `96630636007` 完成 CTest 99/99（34.41 s）；artifact
`9430307996` 为 36,254,057 bytes，API SHA-256
`a60f9885a6da1e3aaee2a3a7a69b7ac374bfab8eba266b777daae49891392d52`。当前 MSI 为
16,305,874 bytes，`a4a4bc07b677692cc424f5e82e9bf2f38e65bc52b38f6b61ef811c4e917ba8d9`；portable
为 13,322,560 bytes，`63487f414cfafdfa12d82cc15199d802393e59c5dc4cac9c9bed7639e27768da`。

此为分支回归，未创建新标签或草稿 Release；最后已验证标签仍为
`relaydesk-phase4-20260820-02`。

## NOT_RUN 边界

unsigned SmartScreen/UAC 的视觉与人工确认仍为 `NOT_RUN`。本机安装成功不代表该交互
提示已验证，也不替代物理 Win↔Mac 或 macOS 系统权限验收。
