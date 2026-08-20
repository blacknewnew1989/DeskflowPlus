# WIN-020 Windows 单机真实运行验收

## 结论

2026-08-20 在 Windows 11 x64 当前用户桌面上，使用精确产品提交
`c134126b95977ca6b97036be18dcfc33a4a3a09a` 的 unsigned portable 包完成真实 GUI、进程和
网络操作。Windows 单机范围为 `PASS`；需要第二个独立操作系统或真实 macOS 的功能保持
`NOT_RUN`。

验收包：

- `relaydesk-c134126b95977ca6b97036be18dcfc33a4a3a09a-win-x64-unsigned-portable.7z`
- SHA-256：`66f08d9cd90094c4009ae2dd98aefa2d13f3ae819f964bb31b3f76651d057647`
- 来源：阶段标签 `relaydesk-phase4-20260820-02`，Actions run `32362194153`

## 真实操作结果

| 范围 | 结果 | 可观察证据 |
|---|---|---|
| 启动与七语言 | PASS | en/es/it/ja/ko/ru/zh_CN 逐项切换，完全退出后重启，首页文本按所选语言恢复 |
| 托盘生命周期 | PASS | 最小化和关闭均隐藏主窗口且进程存活；通知区点击恢复；`Ctrl+Q` 真退出返回 0 |
| 手动地址 | PASS | 主机名规范化，保存后重启恢复；删除并再次重启后保持为空 |
| 输入核心 Server | PASS | GUI 保存 Server 角色后启动 `deskflow-core.exe server`，TCP `0.0.0.0:24800` 监听，首页显示等待客户端 |
| 停止输入核心 | PASS | 确认首次启动提示后 `Ctrl+T`，子进程退出、24800 关闭、首页恢复未运行 |
| 发现与文件通道监听 | PASS | GUI 进程实际绑定 UDP `0.0.0.0:24802`；独立文件通道在同一动态 TCP 端口监听 IPv4/IPv6 |
| 传输中心入口 | PASS | 点击首页历史按钮，560×420 主窗口内显示传输中心及“传输任务会显示在这里”空态 |
| 文件传输设置 | PASS | 接收目录、来件策略和冲突策略改为非默认值；完全退出并重启后从 UI 读回一致 |
| 登录启动 | PASS | GUI 勾选后 HKCU Run 写入当前 `deskflow.exe --start-in-tray`；重启 GUI 后仍为选中；验收后恢复原注册表状态 |
| 状态清理 | PASS | portable 配置和 HKCU Run 均恢复验收前状态；无 `deskflow.exe` / `deskflow-core.exe` 残留 |

本地证据位于忽略的运行目录：

- `dist/runtime/full-functional-c134126b9/evidence-english-only-20260820/result.json`
- `dist/runtime/full-functional-c134126b9/evidence-manual-address-20260820/result.json`
- `dist/runtime/full-functional-c134126b9/evidence-core-runtime-final2-20260820/result.json`
- `dist/runtime/full-functional-c134126b9/evidence-single-host-surface-final-20260820/result.json`

运行目录包含截图、临时配置副本和 JSON；构建产物与运行证据不提交到 Git。

## 调试记录

七语言首轮曾因 UI Automation 时序竞争找不到语言控件；同一精确包随后完整 7/7 重跑通过，
没有产品崩溃或控件缺失的稳定复现。输入核心停止脚本最初被“服务器现已运行”模态提示拦截；
确认提示后真实停止成功。两项均为验收自动化根因，不修改产品代码。

## 明确保留的 NOT_RUN

当前机器只有一个 Windows 交互式桌面。运行中的 Hyper-V worker 属于 Docker Desktop/WSL2，
不是可登录的第二个 Windows 客体；RelayDesk GUI 又以固定共享内存键强制单实例。因此下列项目
不能由本机双 portable 或组件回环替代：

1. 两端真实发现、六位配对、信任持久化、撤销和指纹变化；
2. 双向键盘、鼠标、滚轮、文本和图片剪贴板；
3. 单文件、多文件、文件夹、暂停/继续/取消、冲突决策、失败重试和断线续传；
4. 10 GiB、10k 小文件、输入负载隔离、换网和睡眠唤醒；
5. macOS TCC、menu bar、Gatekeeper 与物理 Win↔Mac；
6. 当前非提升终端无法代办的 MSI UAC/SmartScreen 视觉交互。

Hosted Windows/macOS 构建、安装生命周期和 TLS 回环仍只作为各自范围的支持证据，不提升上述
`NOT_RUN` 项。
