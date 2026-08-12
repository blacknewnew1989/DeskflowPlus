# 19 最终安装与用户验收 Runbook

## 1. 演示环境

- Windows x64 设备 A；
- macOS arm64 设备 B；
- 同一局域网；
- 两台设备各自显示器；
- 一套主键鼠连接 A；
- 测试文件：
  - `hello.txt`；
  - 中文+Emoji 文件；
  - 1 GB 文件；
  - ≥10 GB 文件；
  - 含空目录和 1,000+ 小文件的文件夹。

记录版本、commit、IP、网络类型、签名状态。

## 2. 全新安装

### Windows

1. 安装测试包。
2. 启动。
3. 处理防火墙提示。
4. 确认托盘和服务状态。
5. 记录未签名/已签名表现。

### macOS

1. 安装 App/DMG。
2. 启动。
3. 完成 Accessibility/Input Monitoring/Local Network 实际授权。
4. 重新启动应用。
5. 确认权限页面变为正常。
6. 记录未公证/已公证表现。

## 3. 发现与配对

1. A/B 首页互相出现。
2. 核对设备名、平台和在线状态。
3. 从 A 发起配对。
4. 两端显示相同六位 SAS。
5. 用户在最终验收时确认。
6. 重启两端。
7. 验证已信任和自动重连。
8. 查看完整 fingerprint。
9. 不执行撤销，留到安全环节。

通过标准：

- 未配对前不能发送文件；
- SAS 不一致无法完成；
- 重启后无需账号；
- IP 地址变化不改变设备 identity。

## 4. 键鼠

1. 把 B 布局到 A 右侧。
2. 鼠标从 A 越过右边缘进入 B。
3. 打字、组合键、中文输入。
4. 垂直/水平滚动。
5. 切回 A。
6. 传输文件时重复。
7. 中途断开 B 网络，检查修饰键无卡住。
8. 恢复网络并重连。

## 5. 文件传输

### 小文件

1. 把 `hello.txt` 拖到 B 卡片。
2. B 接收。
3. 检查 UI 进度、通知、目标文件。
4. 比对 SHA-256。

### 文件夹

1. 发送包含空目录、中文和大量小文件的目录。
2. 检查结构、数量、名称。
3. 检查被跳过 symlink/不可读项是否清楚提示。

### 大文件与续传

1. 发送 ≥10 GB。
2. 记录内存、CPU、速度。
3. 在 30% 断开网络。
4. 观察 Interrupted。
5. 恢复网络。
6. 确认从 durable offset 继续，不从 0。
7. 完成并比对 SHA-256。
8. 传输期间持续移动鼠标和打字。

### 控制

- pause；
- resume；
- cancel keep partial；
- retry；
- cancel delete partial；
- same-name auto rename；
- overwrite（在专用测试文件上）；
- disk full/不可写目录。

## 6. 最低数据保护验收

1. 撤销 B 信任，确认不能继续自动连接。
2. 重新配对。
3. 运行最低路径用例：`../`、绝对路径、Windows 保留名、ADS、symlink parent。
4. 确认接收根目录外没有新文件。
5. 确认未完成任务只保留 `.part`，不会冒充正式文件。

## 7. 睡眠/升级

1. 传输中让接收端睡眠。
2. 唤醒并恢复。
3. 安装新版测试包覆盖旧版。
4. 检查 trust/history/config。
5. macOS 重新检查系统权限。
6. 继续未完成任务或清楚标记不兼容。

## 8. 演示结束报告

```text
Product version:
Commit:
Windows:
macOS:
Network:
Signed/notarized:
Pairing:
Input A→B:
Input B→A:
Small file:
Folder:
Large/resume:
Memory peak:
Input under load:
Security cases:
Install/upgrade:
NOT_RUN:
Known issues:
Verdict:
```

只有证据齐全才写 PASS。
