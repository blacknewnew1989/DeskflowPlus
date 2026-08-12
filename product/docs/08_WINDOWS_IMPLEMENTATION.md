# 08 Windows 实现、自动构建与协作

## 0. 执行责任

Windows 平台的源码同步、依赖准备、构建、测试、打包、提交和推送全部由 Codex 对应平台代理完成。用户不负责安装工具链或运行命令。

默认协同流程：

```text
fetch origin -> rebase product/relaydesk-v1 -> 平台开发
-> 小功能 commit -> 任务完成 push -> A0 合并/push
-> GitHub Actions 自动构建 -> artifact 输出 dist/windows/<commit>/
```

若当前 Codex 主机不是 Windows，优先使用 GitHub Actions 对应 runner，不要求用户提供开发机。真实系统权限与跨屏行为只进入最终验收。

## 1. P0 平台目标

- Windows 10 version 1809+；
- Windows 11；
- x64；
- Visual Studio 2022 C++ Desktop workload；
- CMake；
- Qt 6.7+；
- OpenSSL；
- vcpkg（遵循上游实际配置）；
- Release 安装包。

Windows ARM64 不是首个可用版本的阻塞项，但不能破坏上游已有支持。

## 2. Phase 0 原版自动构建

A4 先从远程同步同一产品 commit，然后自行执行：

```powershell
git fetch origin --prune
git switch agent/a4/BASE-002-windows-baseline
git rebase origin/product/relaydesk-v1
pwsh product/scripts/setup-windows.ps1
pwsh product/scripts/build-windows.ps1 -Configuration Release -RunTests
pwsh product/scripts/package-windows.ps1 -Configuration Release
```

本机环境不可用时，A4/A0 触发 `.github/workflows/relaydesk-build.yml` 的 Windows job，读取日志并自动修复。不得要求用户安装 Visual Studio、Qt、vcpkg、WiX 或运行命令。

A4 必须记录：

```text
Windows edition/build
Visual Studio version
MSVC version
CMake version
Qt version/path
OpenSSL version/path
vcpkg commit/triplet
configure command
build command
test command
artifact path
runtime prerequisites
```

若上游依赖路径、生成器或本机工具链不同，先修环境，不要在 Phase 0 修改源代码掩盖环境问题。

## 3. 上游 Windows 平台层

A4 核查 `src/lib/platform/MSWindows*` 的真实职责，重点：

- input hook；
- key state；
- screen；
- clipboard；
- session；
- power；
- watchdog；
- daemon/service；
- DPI/multi-monitor；
- desktop/UAC 边界。

文件传输 P0 不改写这些输入类；只在必要时订阅：

- 网络变化；
- power/sleep；
- session lock/unlock；
- app lifecycle。

## 4. 文件系统

### 路径规则

Windows receiver 必须拒绝：

- drive absolute：`C:\...`；
- UNC：`\\server\share`；
- `..`；
- `:`，防止 ADS；
- NUL/control chars；
- 结尾空格或点；
- 保留设备名，不区分大小写：

```text
CON PRN AUX NUL
COM1..COM9
LPT1..LPT9
```

保留名带扩展名也必须拒绝，例如 `con.txt`。

### 长路径

- 协议路径仍设上限；
- 应用应在 manifest 阶段提前检测；
- 使用 Qt/平台路径 API，避免手写 `MAX_PATH` 假设；
- 打包和运行 manifest 应支持 long path 时再启用；
- P0 对超出目标平台可安全创建的路径明确失败，不截断静默改名。

### Junction / reparse point

P0 默认接收目录由应用创建，并跳过发送端链接类条目。自定义接收目录若本身是 junction/reparse point，直接提示不支持即可；首版不实现逐级句柄遍历和竞态加固。

## 5. 文件 I/O

- `QFile`/Win32 按真实性能选择；
- `.part` 独占写；
- 低磁盘空间提前检查只作为提示，写入错误仍须处理；
- pause 时 flush 到可恢复 checkpoint；
- 完成后关闭 handle，再原子 move/replace；
- 杀毒软件暂时占用导致 rename 失败时有限退避重试；
- 取消时确保 worker 结束后再删 partial。

## 6. 防火墙

P0 行为：

- 监听文件端口后，Windows 可能弹防火墙提示；
- UI 给出状态与诊断，不尝试静默绕过；
- 安装包可按上游方式添加规则，但需明确产品 executable/path；
- 默认只绑定 private network interfaces（实现可行时）；
- 用户拒绝后手动 IP 也应得到明确连接失败提示。

不要添加过宽的 Any/All profiles 规则而不说明。

## 7. 自启动与后台

复用上游 GUI/daemon 机制。A4 先回答：

- 当前由 GUI 启动 core 还是 service；
- 自启动设置在哪里；
- 安装包如何注册；
- 升级后路径变化如何处理；
- 无 GUI 时是否能接收文件。

P0 可接受“用户登录后托盘运行”，不需要控制 Windows 登录/UAC 安全桌面。

## 8. Windows 文件剪贴板（P1）

### 目标

在 Explorer 复制文件后，切到 Mac 并粘贴触发远程发送；不是把文件内容放进剪贴板。

可能涉及：

- `CF_HDROP`；
- OLE `IDataObject`；
- clipboard ownership/change notification；
- 自定义格式携带 RelayDesk remote reference。

要求：

- P0 应用内拖放不依赖此功能；
- 不阻塞 clipboard thread；
- 复制后源文件被删除/变化要提示；
- remote reference 有短生命周期和 peer 绑定；
- 不将任意远端路径交给 Explorer 直接打开。

## 9. Explorer 集成（P1）

优先级低于核心传输：

- 第一选择：SendTo shortcut/轻量 launcher；
- 第二选择：现代 context menu extension；
- 必须独立可禁用；
- extension 崩溃不能拖垮 Explorer；
- 不把主协议和私钥塞进 shell extension；
- extension 仅把选中路径交给已运行主应用的本地 IPC。

## 10. 通知

使用上游/Qt 可用方式：

- 传输请求；
- 完成；
- 失败；
- 点击打开应用或目录。

不默认打开收到的文件。

## 11. 自动打包与远程交接

A4 使用 `product/scripts/package-windows.ps1` 或 GitHub Actions 自动打包，优先复用上游 `deploy/`、CPack 和 WiX。A4 核查后确定：

- MSI/MSIX/Inno/WiX/portable 真实链路；
- VC++ runtime；
- Qt deploy；
- OpenSSL DLL；
- translations；
- firewall；
- upgrade code；
- uninstall data policy。

用户配置、信任和历史默认不因普通升级删除；卸载时是否保留由 UI/installer 明示。

## 12. 签名

开发包阶段：

- 可以生成未签名内部包；
- 签名命令参数化；
- 证书、密码、token 不进仓库；
- CI secret 只在 release job；
- 无凭据时 SKIP，不伪造签名成功。

## 13. Windows 测试

- 标准用户；
- 管理员应用前景时输入限制；
- 多 DPI、多屏；
- 横向滚动；
- 中文输入法；
- Defender/杀毒占用；
- 防火墙拒绝/允许；
- 睡眠/锁屏/解锁；
- 网络从 Wi-Fi 切有线；
- long path；
- reserved names；
- disk full；
- source file locked；
- 10k small files；
- 10GB+ file；
- 安装/升级/卸载。

## 14. Windows 任务完成后的 Git 操作

每个小功能完成后 commit。Windows backlog 任务完成后，A4 自动：

```powershell
git fetch origin --prune
git rebase origin/product/relaydesk-v1
# 运行受影响测试
git push -u origin HEAD
```

A0 合并并推送集成分支。A4 在 `product/working/handoffs/<task-id>.md` 写入 commit、构建命令、artifact 和 macOS 需要验证的事项。
