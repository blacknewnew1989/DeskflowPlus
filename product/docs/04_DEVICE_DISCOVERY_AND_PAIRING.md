# 04 设备发现、配对与本地信任

## 1. 目标与范围

RelayDesk 仅在内部局域网使用。第一版配对目标是**简单、可用、无需账号**，不建设复杂密码学协议、云端证书平台、组织权限或审批。

流程：

```text
局域网发现
→ 用户选择设备
→ 两端显示/输入六位确认码
→ 记录 deviceId 与 Deskflow/TLS 指纹
→ 后续自动重连
```

## 2. DeviceIdentity

```cpp
struct DeviceIdentitySummary {
    QUuid deviceId;
    QString displayName;
    QString platform;
    QString architecture;
    QString appVersion;
    QByteArray certificateFingerprintSha256;
};
```

- `deviceId` 首次启动生成并本地保存；
- 显示名仅用于 UI；
- 复用上游已有 TLS 身份/指纹能力，避免建立第二套 PKI；
- 私钥不进入发现包、日志和仓库。

## 3. 局域网发现

优先使用 Qt `QUdpSocket`：

- IPv4 multicast/broadcast；
- 3～5 秒心跳；
- 15 秒未见标记离线；
- 多网卡遍历；
- 手动 IP/主机名回退；
- 小型 CBOR 消息。

建议字段：

```text
protocol
version
deviceId
displayName
platform
architecture
appVersion
inputPort
filePort
capabilities
certificateFingerprint
```

发现仅用于列出设备，不自动开始控制或文件接收。

## 4. 简单配对流程

### 方案

1. A 选择 B；
2. A 生成六位随机码并建立临时连接；
3. A 显示六位码；
4. B 输入或确认相同码；
5. 双方交换 deviceId、显示名和证书指纹；
6. 用户确认后保存本地信任记录；
7. 后续连接校验保存的指纹。

六位码只用于本次内部配对确认，不作为长期密码。第一版不实现 PAKE、复杂 transcript、失败计数平台、IP 风险评分或云端认证。

允许实现一个简单有效期，例如 5 分钟；实现方式以最少代码为准。失败后用户可以重新发起，不需要复杂指数退避。

## 5. TrustedDevice

```json
{
  "schemaVersion": 1,
  "devices": [
    {
      "deviceId": "23a61a88-56e8-4a5d-a89a-f49d4eefc918",
      "alias": "MacBook Pro",
      "platform": "macos",
      "fingerprintSha256": "BASE64...",
      "lastAddresses": ["192.168.1.20"],
      "autoAcceptFiles": false,
      "revoked": false
    }
  ]
}
```

要求：

- 简单 JSON/CBOR 本地保存；
- 原子替换，避免配置写一半；
- 支持撤销；
- 指纹变化时提示重新配对；
- 不引入数据库服务器。

## 6. 连接

- 输入连接继续复用 Deskflow；
- 文件连接复用已有 TLS/证书能力；
- 指纹与本地记录不一致时停止自动连接并提示重新配对；
- 文件连接与输入连接互不阻塞。

## 7. 自动重连

- 以 deviceId 为主键；
- 最近成功地址优先；
- 发现地址其次；
- 手动地址回退；
- 网络恢复后重试；
- 不建设复杂连接策略中心。

## 8. UI

### 未配对设备

- 设备名；
- 平台；
- 在线状态；
- 配对按钮。

### 配对页

- 目标设备；
- 六位码；
- 确认/取消；
- 可展开查看指纹。

### 已配对设备

- 改别名；
- 自动接收文件开关；
- 重新配对；
- 撤销信任。

## 9. 测试

- 正常配对；
- 错码；
- 码过期后重新发起；
- 同名设备；
- IP 变化；
- 多网卡；
- 证书指纹变化；
- 撤销后不能自动连接；
- 配置文件损坏使用空配置/备份恢复；
- Windows↔macOS 实际连接。

这些测试不设置为人工审批门禁；失败由 Codex 修复并继续。
