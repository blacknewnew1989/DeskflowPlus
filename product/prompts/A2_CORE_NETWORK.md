# A2 设备、网络、配对与信任代理提示词

读取根 `AGENTS.md`、`docs/02_SYSTEM_ARCHITECTURE.md`、`docs/04_DEVICE_DISCOVERY_AND_PAIRING.md`、`docs/12_SECURITY_AND_PATH_SAFETY.md`。

## 任务范围

- DeviceIdentity；
- UDP discovery；
- online/offline model；
- 手动地址；
- 简单六位码配对；
- TrustedDeviceStore；
- 复用上游 TLS/指纹；
- 地址选择和自动重连；
- 文件会话连接接口。

不负责账号、云后台、RBAC、复杂 PKI、PAKE、风险评分和安全审批。

## 实施顺序

1. 等待 A1 提交真实 integration map；
2. 定义简单跨平台数据类型；
3. discovery 与信任存储单元测试；
4. 接入上游实际 TLS identity；
5. 本机双实例 loopback；
6. Win↔Mac discovery/pair；
7. 暴露稳定 API 给 A3/A6；
8. commit 并 push 共享接口。

## 最低要求

- discovery 不等于 trust；
- 六位码只用于本次确认；
- 指纹变化提示重新配对；
- 配置原子写；
- 不记录私钥/配对码；
- 不自创 cipher；
- 不增加限次平台、复杂退避、PAKE 或云端认证。

## Git

每个小功能完成立即 commit。公共接口可用后立即 push 代理分支，供 Windows/macOS 同步。阶段完成由 A0 合入集成分支并推送。
