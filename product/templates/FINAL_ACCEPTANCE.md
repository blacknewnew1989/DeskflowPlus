# RelayDesk 最终验收单

> 本文件由 A0/A7 在交付时复制为仓库根或发布目录中的 `FINAL_ACCEPTANCE.md`，并自动填写构建信息。用户只执行本清单中的最终真实设备验收，不参与开发、源码获取、依赖安装、提交、推送或打包。

## 1. 版本与产物

- Release/RC:
- Integration branch:
- Full commit SHA:
- Stage/release tag:
- Windows Actions run:
- macOS Actions run:

### Windows

- Artifact name:
- Installer/portable path:
- SHA-256:
- Signed state: signed / unsigned-internal

### macOS

- Artifact name:
- App/DMG path:
- SHA-256:
- Architecture: arm64
- Signed state: Developer ID / ad-hoc / unsigned-internal
- Notarization state:

## 2. Codex 已完成证据

- [ ] 源码、全部小功能提交和阶段提交已推送 GitHub
- [ ] `product/relaydesk-v1` 与阶段/发布 tag 可解析到上述 commit
- [ ] Windows 与 macOS 产物来自同一 commit
- [ ] 自动测试报告已附带
- [ ] `SHA256SUMS.txt` 与 artifact manifest 已附带
- [ ] 安装、卸载、权限和已知问题说明已附带
- [ ] 所有无法自动化的项目已明确标记 `FINAL_ACCEPTANCE_REQUIRED`

## 3. Windows 最终验收

- [ ] 安装包或便携包能启动
- [ ] Windows 防火墙提示可正常允许局域网通信
- [ ] 应用能显示本机设备信息
- [ ] 卸载或退出行为符合说明

## 4. macOS 最终验收

- [ ] App/DMG 能安装或打开
- [ ] 在 System Settings → Privacy & Security → Accessibility 授权
- [ ] 系统要求时完成 Input Monitoring 授权
- [ ] 系统要求时完成 Local Network 授权
- [ ] 重启应用后权限状态识别正常

## 5. 双向键鼠与剪贴板

- [ ] Windows → macOS 鼠标跨屏
- [ ] Windows → macOS 键盘、滚轮与组合键
- [ ] macOS → Windows 鼠标跨屏
- [ ] macOS → Windows 键盘、滚轮与组合键
- [ ] Windows ↔ macOS 文本剪贴板
- [ ] Windows ↔ macOS 图片剪贴板
- [ ] 切换、断线或退出后无卡住的修饰键

## 6. 双向文件传输

- [ ] Windows → macOS 单文件
- [ ] macOS → Windows 单文件
- [ ] 多文件
- [ ] 文件夹与空目录
- [ ] 中文、空格和 Emoji 文件名
- [ ] 暂停、继续和取消
- [ ] 网络中断后续传
- [ ] 10 GB 以上大文件（条件允许时）
- [ ] 文件传输期间鼠标键盘无明显卡顿
- [ ] 接收文件 SHA-256/完整性结果正确

## 7. 验收问题反馈

出现问题时只需记录：

- 发生平台与系统版本；
- 当前 commit/版本；
- 操作步骤；
- 预期与实际现象；
- 应用日志和截图。

用户不需要自行修复。Codex根据反馈创建任务、开发、提交、推送并重新生成双平台包。

## 8. 结论

- [ ] 验收通过
- [ ] 有条件通过，已知问题如下
- [ ] 验收失败，问题编号如下

备注：
