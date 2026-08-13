# Scripts：全部由 Codex 调用

用户不执行本目录任何脚本。A0/平台代理根据仓库状态和当前平台自动调用；本地环境不可用时自动转到 GitHub Actions。

## 主流程

### `autonomous-init-repo.py`

首轮唯一仓库自举入口，默认自动：

- 识别并保留 `origin`；
- 添加/修正 `upstream`；
- fetch 并验证 Deskflow v1.26.0 / `760e3b9`；
- 创建或复用 `product/relaydesk-v1` worktree；
- 安装根 `AGENTS.md`、`product/` 资料和 `.github/workflows/relaydesk-build.yml`；
- 创建 bootstrap commit；
- 推送集成分支。

`--no-push` 仅用于脚本自测，A0 正式执行不得使用。

### `git-checkpoint.py`

用于小功能提交和任务分支推送。每个可独立验证的小功能立即 commit；任务完成或跨平台共享接口完成后 push。

### `complete-stage.py`

阶段完成后生成报告、提交阶段状态、推送集成分支和 `relaydesk-phase*` 标签。标签自动触发双平台工作流。

### `run-github-actions.py`

A0 自动查找或触发当前 commit 的 `relaydesk-build.yml`，等待完成、下载 Windows/macOS artifacts、生成下载文件 SHA-256，并写入/安全提交 `product/working/actions/`。没有 `gh` 时，A0 使用当前会话的 GitHub 工具执行等价操作，不询问用户。

## Windows

- `setup-windows.ps1`：自动检测/安装 Git、CMake、Ninja、Python、MSVC Build Tools、Qt、vcpkg、7-Zip 和 WiX；
- `build-windows.ps1`：Debug/Release 构建与测试；
- `package-windows.ps1`：CPack/WiX 打包、产物收集和 SHA-256。

Windows 签名是可选步骤。没有签名身份时脚本继续生成明确标注的 `unsigned` 内部包。需要签名时，可通过参数或对应环境变量提供：

- `-SigningCertificatePath` / `RELAYDESK_WINDOWS_SIGN_CERTIFICATE`；
- `-SigningCertificateThumbprint` / `RELAYDESK_WINDOWS_SIGN_THUMBPRINT`；
- `-SigningTimestampUrl` / `RELAYDESK_WINDOWS_SIGN_TIMESTAMP_URL`；
- `-SignToolPath` / `RELAYDESK_WINDOWS_SIGNTOOL`；
- `-SigningCertificatePassword`（`SecureString`）或 `RELAYDESK_WINDOWS_SIGN_PASSWORD`。

证书文件、密码和真实 thumbprint 不写入仓库或构建报告。PFX 路径与证书库 thumbprint 二选一；配置签名时失败会终止 signed 包，禁止把失败包误标为 signed。

本机权限或工具链不满足时，A0 使用 `.github/workflows/relaydesk-build.yml` 的 Windows runner，不向用户转交安装命令。

## macOS

- `setup-macos.sh`：自动检测 Xcode/Homebrew 并安装可非交互安装的依赖；
- `build-macos.sh`：Apple Silicon Release/Debug 构建与测试；
- `package-macos.sh`：App/DMG/源包、产物收集和 SHA-256；ad-hoc 包收集后会自动运行隔离的 TEST-005 安装、升级、卸载与 bundle 闭包回归，失败时整个打包入口返回失败。

本机缺少需要人工授权安装的 Xcode/Homebrew 时，A0 使用同一工作流的 macOS runner，不向用户转交环境准备。

## 其他

- `install-package.py`：把开发资料安装到 Deskflow 源码 worktree；
- `install-github-workflows.py`：安装非门禁 Actions 工作流；
- `collect-ci-artifacts.py`：收集包、App、校验值和 manifest；
- `validate-package.py`：开发包静态自检；
- `bootstrap-upstream.sh/.ps1`：兼容包装器，内部调用 `autonomous-init-repo.py`。

## 默认执行链

```text
A0 autonomous-init-repo
  → bootstrap commit + push
  → A1/A2/A3/A6 共享开发
  → 小功能 commit
  → 任务分支 push
  → A4/A5 本地构建；失败则 Actions 回退
  → A0 complete-stage
  → 集成分支 + 标签 push
  → relaydesk-build.yml 双平台包
  → A0/A7 run-github-actions 自动等待并下载 artifact
  → A7 校验并生成最终验收材料
```
