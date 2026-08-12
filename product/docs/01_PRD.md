# 01 产品需求文档（PRD）

## 0. 文档执行级别

本 PRD 同时定义产品需求和 Codex 全自动执行流程。A0 不得只输出方案、教程或命令清单，必须实际执行源码获取、仓库初始化、环境准备、开发、提交、推送、构建、打包与产物下载。

**默认前提：当前目录和会话已经连接用户的 GitHub 仓库，`origin` 可用。用户只负责最终安装与验收。**

## 1. 产品定义

RelayDesk 是一款无需账号、无需云端、在本地局域网工作的跨设备协作工具。用户使用一套键盘鼠标在 Windows 与 macOS 设备间无缝切换，并把文件或文件夹可靠发送到任意已配对设备。

## 2. 交付责任

### 2.1 Codex 必须负责

- 自动定位并在需要时解压本开发包；
- 自动获取 Deskflow v1.26.0 源码；
- 自动将源码放入当前 GitHub 项目仓库；
- 自动配置 `origin/upstream`；
- 自动创建产品分支、代理分支和 worktree；
- 自动准备或安装 Windows/macOS 开发依赖；
- 自动开发、测试、提交、合并和推送；
- 自动配置双平台 GitHub Actions 回退；
- 自动生成 Windows 安装包与 macOS App/DMG；
- 自动安装并触发 `.github/workflows/relaydesk-build.yml`；
- 自动监控 Actions、读取失败日志、修复并重新运行；
- 自动下载构建 artifact 或生成草稿 Release；
- 自动维护状态、日志、SHA-256 和最终验收说明。

### 2.2 用户只负责

项目全部开发完成后：

- 在 Windows 安装生成的包；
- 在 macOS 安装/打开生成的 App 或 DMG；
- 按系统提示授予 Accessibility/Input Monitoring/Local Network；
- 按最终验收表验证键鼠、剪贴板和文件互传；
- 若需要正式签名，再提供 Apple/Windows 证书。

开发过程中不得要求用户执行开发包解压/移动、clone、download、remote、branch、build、test、commit、push、package 等命令。

### 2.3 自动执行授权边界

本 PRD 中的所有命令块都是 **Codex/A0/子代理的可执行规范**，不是用户教程。对于可回退、仅影响本项目仓库或本项目工作目录的操作，Codex直接执行；遇到错误自行查看日志、调整命令并重试。不得因为“需要下载源码”“需要安装 Qt”“需要在 Mac 打包”“需要推送 GitHub”等理由暂停并询问用户。

允许留到最终验收的仅是操作系统明确要求真人点击的权限确认，以及真实代码签名证书；它们不阻塞 unsigned 内部包。

## 3. 当前 GitHub 仓库自动自举

### 3.0 开发包定位与自动解压

A0 先在当前会话可见目录、当前仓库和会话附件挂载目录中定位本开发包。若输入为 `RelayDesk-Codex-Development-Package-*.zip`，A0 使用系统解压工具或 Python `zipfile` 自动解压到项目临时目录，并从解压后的 `AGENTS.md`、`docs/01_PRD.md` 和 `scripts/autonomous-init-repo.py` 开始执行。不得要求用户先解压、移动或复制开发包。

若开发包文件已经直接位于当前目录，则直接使用；若资料已经安装到源码分支，则使用 `product/` 下的副本。A0 应自行解析实际路径，不得要求用户提供绝对路径。

### 3.1 固定仓库关系

```text
origin   = 当前已连接的用户 GitHub 仓库，负责协作和保存成果
upstream = https://github.com/deskflow/deskflow.git，负责获取上游源码和 tag
```

A0 禁止删除、重命名或覆盖 `origin`。不需要 GitHub Fork。

### 3.2 首轮检查

A0 进入当前目录后自行执行：

```bash
git rev-parse --show-toplevel
git status --short --branch
git remote -v
git ls-remote origin
```

若 `gh` 可用，同时执行：

```bash
gh auth status
gh repo view --json nameWithOwner,defaultBranchRef
```

上述命令用于记录，不需要用户确认。

### 3.3 添加并验证 upstream

```bash
if git remote get-url upstream >/dev/null 2>&1; then
  git remote set-url upstream https://github.com/deskflow/deskflow.git
else
  git remote add upstream https://github.com/deskflow/deskflow.git
fi

git fetch upstream --tags --prune
git rev-parse --short=7 'v1.26.0^{commit}'
# 必须为 760e3b9
```

### 3.4 自动导入源码决策

A0 检查当前仓库根目录是否存在：

```text
CMakeLists.txt
src/apps
src/lib
src/lib/platform
```

#### 情况 A：当前仓库已经是 RelayDesk/Deskflow 源码仓库

- 验证当前集成分支；
- fetch/pull 远程；
- 不重复导入；
- 继续开发。

#### 情况 B：当前仓库只有开发包、README 或空仓库

A0 自动执行：

1. 将当前开发包复制到临时目录，避免切换分支时丢失；
2. 若工作区有未提交资料，使用 stash 或安全临时分支保存，不删除；
3. 若远程已存在 `origin/product/relaydesk-v1`，直接跟踪该分支；
4. 否则从 `v1.26.0` 创建 `product/relaydesk-v1`；
5. 恢复根 `AGENTS.md`，并在 `product/AGENTS.md` 保留可重复执行副本；
6. 将其余开发资料安装到 `product/`；
7. 生成 bootstrap 报告；
8. 创建提交并推送。

为避免覆盖当前仅包含开发包的工作目录，`autonomous-init-repo.py` 可以在同一个 Git 仓库下自动创建相邻 worktree（例如 `../<repo>-relaydesk`），A0 随后切换到该 worktree 继续；用户不需要操作。

参考命令：

```bash
git switch --create product/relaydesk-v1 v1.26.0
python product/scripts/install-package.py \
  --package-root <临时开发包目录> \
  --repo "$(git rev-parse --show-toplevel)" \
  --force-agents

git add AGENTS.md product
git commit -m "chore(bootstrap): initialize RelayDesk from Deskflow v1.26.0"
git push -u origin product/relaydesk-v1
```

A0 的第一项实际操作必须是直接运行开发包提供的自举脚本，而不是把命令返回给用户：

```bash
python scripts/autonomous-init-repo.py \
  --repo "$(git rev-parse --show-toplevel)"
```

脚本输出 `SOURCE_WORKTREE=<path>` 后，A0 自动切换该 worktree 继续开发。脚本同时安装根 `AGENTS.md`、`product/` 资料和 `.github/workflows/relaydesk-build.yml`，创建 bootstrap commit 并默认推送。

PowerShell：

```powershell
python .\scripts\autonomous-init-repo.py --repo (git rev-parse --show-toplevel)
```

脚本默认自动创建 bootstrap 提交、推送 `product/relaydesk-v1`，并安装 `.github/workflows/relaydesk-build.yml`。脚本打印的 `SOURCE_WORKTREE` 是后续 Codex 会话工作目录；A0 必须自行切换进去继续执行。脚本失败时由 A0 读取错误并自行修正，不把命令交给用户。

### 3.5 不允许的 Git 操作

除非为恢复已确认的损坏仓库，否则禁止：

- `git push --force`；
- `git reset --hard` 覆盖未保存工作；
- 删除用户默认分支；
- 用上游地址替换 `origin`；
- 要求用户重新建仓库或 Fork。

## 4. Git 提交与远程同步

### 4.1 分支模型

```text
product/relaydesk-v1       唯一产品集成分支
agent/a1/*                  上游/构建
agent/a2/*                  网络/发现
agent/a3/*                  GUI
agent/a4/windows-*          Windows
agent/a5/macos-*            macOS
agent/a6/*                  文件传输
agent/a7/*                  测试/发布
release/*                   发布候选
```

### 4.2 小功能完成立即提交

每个可独立验证的小功能完成后，必须在当前代理分支创建提交：

```bash
git add <本功能涉及文件>
git commit -m "feat(file): FILE-001 complete single-file offer"
```

提交前至少执行受影响的最小构建或测试。禁止把数十个无关功能堆进一个提交。Codex 可调用：

```bash
python product/scripts/git-checkpoint.py \
  --task FILE-001 --type feat --area file \
  --summary "complete single-file offer" \
  --paths src/lib/filetransfer product/TASK_BOARD.md
```

任务完成、平台切换或需要另一代理消费时，加 `--push-task`，由脚本推送当前代理分支。

### 4.3 何时推送

以下任一条件满足时必须推送代理分支：

- 共享接口已可供另一平台代理使用；
- 当前工作需要从 Windows 切换到 Mac，或反向切换；
- 一个任务已完成；
- 会话即将结束；
- 需要 GitHub Actions 构建当前提交。

```bash
git push -u origin HEAD
```

### 4.4 阶段完成

A0 自动：

1. fetch 最新远程；
2. 合并或 cherry-pick 已验证提交；
3. 解决冲突；
4. 更新状态文档；
5. 运行当前可用测试；
6. 推送集成分支；
7. 创建阶段标签；
8. 触发 GitHub Actions；
9. 保存 artifact 地址和 SHA-256。

```bash
git switch product/relaydesk-v1
git pull --ff-only origin product/relaydesk-v1
# 合并已验证代理分支
git push origin product/relaydesk-v1
git tag -a relaydesk-phase2-20260812-01 -m "RelayDesk Phase 2"
git push origin relaydesk-phase2-20260812-01
```

可以调用：

```bash
python product/scripts/complete-stage.py \
  --stage phase2 \
  --summary "single/multi-file transfer core"
```

### 4.5 不设置开发门禁

- PR 可选；
- 不要求人工 review；
- 不启用 required checks；
- 不设置覆盖率阈值；
- 不设置发布审批；
- CI 失败用于修复，不用于让用户手工放行；
- A0 对可回退的内部变更自行决策并继续。

## 5. Windows 与 macOS 协同开发

### 5.1 唯一同步通道

源码只通过 GitHub 远程仓库同步。构建产物只通过 GitHub Actions artifact 或草稿 Release 同步。

禁止：

- 手工复制源码目录；
- 在两台机器分别长期维护未推送改动；
- 通过文件传输工具交换开发分支；
- 把 `build/`、Qt SDK 或安装包提交 Git。

### 5.2 每个平台会话开始

Windows PowerShell：

```powershell
git fetch origin --prune --tags
git switch product/relaydesk-v1
git pull --ff-only origin product/relaydesk-v1
git switch -c agent/a4/windows-<task>  # 不存在时创建
```

macOS：

```bash
git fetch origin --prune --tags
git switch product/relaydesk-v1
git pull --ff-only origin product/relaydesk-v1
git switch -c agent/a5/macos-<task>    # 不存在时创建
```

### 5.3 接口协同

文件传输协议、公共数据结构和 CMake target 由 A2/A6 先提交并推送。Windows/macOS 适配必须基于同一个共享 commit。

平台代理完成后：

```bash
git add <platform files>
git commit -m "feat(windows): WIN-001 integrate transfer service"
git push -u origin HEAD
```

A0 合并两端分支后触发双平台构建。另一平台不等待用户传文件，直接 fetch。

### 5.4 共享状态

A0 每次阶段推送同时更新：

- `product/PROJECT_STATE.md`；
- `product/TASK_BOARD.md`；
- `product/working/stages/*.md`；
- 当前 Windows/macOS 可构建 commit；
- artifact 名称与校验值；
- `PASS/FAIL/NOT_RUN`。

## 6. 自动环境准备

本节命令全部由 Codex 执行。不得输出“请在 Windows/Mac 上安装”后停止；本地不可自动准备时直接走 GitHub Actions runner。

A0/A4/A5 必须先读取真实上游：

```text
doc/dev/build.md
CMakeLists.txt
.github/actions/install-dependencies/action.yml
.github/workflows/continuous-integration.yml
deploy/windows/deploy.cmake
deploy/mac/deploy.cmake
```

固定最低要求：CMake 3.24+、Qt 6.7+、OpenSSL 3+、C++20。第一版自动构建优先复用上游 CI 使用的 Qt 和 WiX 版本。

### 6.1 Windows 自动准备

优先复用已安装工具。缺少时由 Codex 使用 `winget`、Python/aqtinstall、vcpkg 和 dotnet tool 自动安装：

```powershell
winget install --id Git.Git -e --accept-package-agreements --accept-source-agreements
winget install --id Kitware.CMake -e --accept-package-agreements --accept-source-agreements
winget install --id Ninja-build.Ninja -e --accept-package-agreements --accept-source-agreements
winget install --id Python.Python.3.12 -e --accept-package-agreements --accept-source-agreements
winget install --id Microsoft.DotNet.SDK.8 -e --accept-package-agreements --accept-source-agreements
winget install --id Microsoft.VisualStudio.2022.BuildTools -e `
  --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended" `
  --accept-package-agreements --accept-source-agreements

python -m pip install --user aqtinstall
# Qt 具体 arch 以当前 Qt 包和上游 CI 为准
python -m aqt install-qt windows desktop 6.10.1 win64_msvc2022_64 -O .tools/Qt

git clone https://github.com/microsoft/vcpkg.git .tools/vcpkg
.\.tools\vcpkg\bootstrap-vcpkg.bat

dotnet tool install --global wix --version 5.0.2
wix extension add --global WixToolset.UI.wixext/5.0.2
wix extension add --global WixToolset.Util.wixext/5.0.2
wix extension add --global WixToolset.Firewall.wixext/5.0.2
```

若某个包已存在，Codex 跳过安装。若本机无管理员权限或安装失败，不询问用户，改用 GitHub Actions Windows runner。

### 6.2 macOS 自动准备

优先复用 Xcode/Command Line Tools 和 Homebrew。缺少普通依赖时：

```bash
brew update
brew install cmake ninja qt openssl@3 googletest python
```

若没有 Homebrew，Codex可以安装 Homebrew或直接使用 GitHub Actions macOS runner。若缺失 Xcode 且无法非交互安装，本地构建标记 `NOT_RUN`，但必须继续通过 macOS Actions 构建 App/DMG。

## 7. 自动构建、测试与打包

### 7.1 Windows

Codex 自动定位 Visual Studio 开发环境、Qt、vcpkg，然后执行等价流程：

```powershell
cmake -S . -B build/windows/release -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DSKIP_BUILD_TESTS=ON `
  -DBUILD_TESTS=ON `
  -DBUILD_INSTALLER=ON `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-release `
  -DCMAKE_PREFIX_PATH="$env:RELAYDESK_QT_PREFIX"

cmake --build build/windows/release --config Release --parallel
ctest --test-dir build/windows/release -C Release --output-on-failure
cmake --build build/windows/release --config Release --target package package_source --parallel
```

上游 Windows 打包始终可生成 7Z；检测到 WiX 时生成安装包。Codex把结果复制到 `dist/windows/`，计算 SHA-256，并上传 artifact。

Windows 本地唯一入口由 A4 自动运行；脚本内部自动完成环境准备：

```powershell
& .\product\scripts\package-windows.ps1 `
  -RepoRoot (git rev-parse --show-toplevel)
```

### 7.2 macOS

```bash
QT_PREFIX="$(brew --prefix qt)"
OPENSSL_PREFIX="$(brew --prefix openssl@3)"

cmake -S . -B build/macos/release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14 \
  -DCMAKE_PREFIX_PATH="$QT_PREFIX;$OPENSSL_PREFIX" \
  -DOPENSSL_ROOT_DIR="$OPENSSL_PREFIX" \
  -DSKIP_BUILD_TESTS=ON \
  -DBUILD_TESTS=ON \
  -DBUILD_OSX_BUNDLE=ON \
  -DBUILD_INSTALLER=ON

cmake --build build/macos/release --parallel
ctest --test-dir build/macos/release --output-on-failure
cmake --build build/macos/release --target package package_source --parallel
```

上游 CPack 生成 `.dmg`。无 Developer ID 时使用 ad-hoc/unsigned 内部包并明确命名，不等待用户提供证书。

macOS 本地唯一入口由 A5 自动运行；脚本内部自动完成环境准备：

```bash
./product/scripts/package-macos.sh \
  --repo "$(git rev-parse --show-toplevel)"
```

### 7.3 GitHub Actions 回退

A0 必须通过 `product/scripts/install-github-workflows.py` 安装并维护唯一的非门禁工作流；禁止并存第二套工作流文件名：

```text
.github/workflows/relaydesk-build.yml
```

触发条件：

- `workflow_dispatch`；
- `product/relaydesk-v1` 的阶段推送；
- `relaydesk-phase*` 标签。

矩阵至少包含：

- Windows 2022 x64；
- macOS 15 arm64 runner（可用时）；
- build、test、package；
- artifact upload。

工作流不得成为 required check。A0 自动触发并监控：

```bash
gh workflow run relaydesk-build.yml --ref product/relaydesk-v1
gh run list --workflow relaydesk-build.yml --limit 5
gh run watch <run-id> --exit-status
gh run download <run-id> --dir dist/actions
# 或统一调用：
python product/scripts/run-github-actions.py --ref product/relaydesk-v1
```

失败时 A0 读取日志、修复代码或构建配置、创建小功能提交并重新运行，不要求用户操作。成功后 A0/A7 自动下载两个平台的 artifact，为下载文件生成 SHA-256，写入 Actions run ID、commit、run URL 和校验值，并在工作区安全时自动提交该轻量运行报告。若 `gh` 不可用，A0 使用当前会话已连接的 GitHub 工具完成同样的 workflow 查询、日志读取和 artifact 下载。

## 8. 产品平台范围

### P0

- Windows 10 1809+ / Windows 11 x64；
- macOS 14+ Apple Silicon（与固定上游 arm64 CI 目标一致）；
- 同一局域网；
- IPv4；
- unsigned 内部包允许作为交付。

### P1

- Intel macOS；
- Windows ARM64；
- 跨子网手动地址；
- 文件复制粘贴、屏幕边缘投递、Explorer/Finder 集成。

## 9. 用户故事

### 键鼠

- 鼠标越过屏幕边缘后进入另一台设备；
- 键盘焦点随目标设备切换；
- Windows `Ctrl` 与 macOS `Command` 可语义映射；
- 断线、睡眠或切换不留下卡住的修饰键；
- 重启后自动恢复连接。

### 文件传输

- 单文件、多文件、整个文件夹；
- 拖到目标设备卡片发送；
- 接收、拒绝或信任设备自动接收；
- 显示进度、当前文件、速度和 ETA；
- 暂停、继续、取消；
- 断线/重启后续传；
- 同名默认自动重命名；
- 中文、空格、Emoji 文件名；
- 10 GB 以上文件流式传输；
- 近期传输历史与打开目录。

## 10. 功能需求

### FR-BASE

| ID | 需求 | 优先级 |
|---|---|---|
| FR-BASE-001 | 保持 Deskflow 双向 Server/Client 能力 | P0 |
| FR-BASE-002 | 保持现有 TLS、剪贴板、布局和热键能力 | P0 |
| FR-BASE-003 | 固定 v1.26.0 基线并可同步上游 | P0 |
| FR-BASE-004 | 品牌集中配置 | P0 |
| FR-BASE-005 | 当前 GitHub 仓库自动导入、提交和推送 | P0 |
| FR-BASE-006 | Windows/macOS 自动构建和 artifact | P0 |

### FR-DISCOVERY

| ID | 需求 | 优先级 |
|---|---|---|
| FR-DISC-001 | 广播本机设备摘要和能力 | P0 |
| FR-DISC-002 | 同网段设备发现、去重、离线超时 | P0 |
| FR-DISC-003 | 手动 IP/主机名回退 | P0 |
| FR-DISC-004 | 多网卡选择 | P0 |

### FR-PAIR

| ID | 需求 | 优先级 |
|---|---|---|
| FR-PAIR-001 | 稳定 deviceId | P0 |
| FR-PAIR-002 | 复用/生成本地 TLS 身份 | P0 |
| FR-PAIR-003 | 简单六位确认或指纹确认 | P0 |
| FR-PAIR-004 | 本地信任保存与撤销 | P0 |
| FR-PAIR-005 | 指纹变化重新确认 | P0 |

内部版本不开发账号、组织、RBAC、复杂 PAKE、云端证书服务和审批流程。

### FR-FILE

| ID | 需求 | 优先级 |
|---|---|---|
| FR-FILE-001 | 独立文件连接 | P0 |
| FR-FILE-002 | 单文件/多文件/文件夹 | P0 |
| FR-FILE-003 | 流式分块，默认 1 MiB | P0 |
| FR-FILE-004 | `.part` 与原子完成 | P0 |
| FR-FILE-005 | SHA-256 完整性校验 | P0 |
| FR-FILE-006 | pause/resume/cancel | P0 |
| FR-FILE-007 | 断线/进程重启续传 | P0 |
| FR-FILE-008 | 自动重命名/覆盖/跳过/询问 | P0 |
| FR-FILE-009 | 本地历史和失败重试 | P0 |
| FR-FILE-010 | 最低路径越界防护 | P0 |
| FR-FILE-011 | 应用设备卡片拖放 | P0 |
| FR-FILE-012 | 跨设备文件复制粘贴 | P1 |
| FR-FILE-013 | Explorer/Finder 集成 | P1 |

### FR-UI

| ID | 需求 | 优先级 |
|---|---|---|
| FR-UI-001 | 当前设备和发现/配对设备首页 | P0 |
| FR-UI-002 | 拖动设备卡片配置位置 | P0 |
| FR-UI-003 | 配对向导 | P0 |
| FR-UI-004 | 传输中心 | P0 |
| FR-UI-005 | 中文/英文 i18n | P0 |
| FR-UI-006 | 托盘快速操作 | P0 |

## 11. 非功能需求

### 性能

- 键鼠事件优先于文件数据；
- GUI 不被扫描、哈希或磁盘 I/O 阻塞；
- 大文件传输内存有界；
- UI 进度刷新不高于 5 Hz；
- 默认总并发任务 2。

### 可靠性

- 临时文件不冒充完成文件；
- 任务可恢复或明确失败；
- 断线不留下卡住按键；
- 配置损坏使用简单备份/默认值。

### 内部使用最低安全

- 复用 TLS；
- 接收路径限制在目标目录；
- 拒绝绝对路径和父级遍历；
- 不自动执行收到文件；
- 密钥不入仓库。

不建设强制限流、病毒扫描、内容审查、DLP、WAF、零信任、复杂权限和安全审批。

### 可维护性

- 共享核心不包含平台头文件；
- 协议有版本与测试；
- 上游同步单独提交；
- Windows/macOS 只通过 GitHub 协同。

## 12. 默认行为

- 接收目录：`Downloads/RelayDesk`；
- 首次接收询问；
- 信任设备可自动接收；
- 同名自动重命名；
- 断线自动续传；
- 历史默认最近 1,000 个任务或 90 天；
- 收到文件不自动打开；
- 开发包默认 unsigned。

## 13. 最终交付物

A0 必须在远程仓库和 GitHub artifact/Release 中交付：

1. `product/relaydesk-v1` 完整源码；
2. 阶段化 Git 历史；
3. Windows x64 安装包/便携包；
4. macOS arm64 App/DMG；
5. source package；
6. SHA-256；
7. Windows/macOS 构建日志；
8. 安装说明；
9. 由 `product/templates/FINAL_ACCEPTANCE.md` 生成并填写的最终验收清单；
10. 已知问题与 `NOT_RUN`。

## 14. 最终验收场景

### AC-001 双向控制

Windows→Mac、Mac→Windows 鼠标跨屏、键盘、滚轮、自动重连。

### AC-002 文件互传

两个方向完成单文件、多文件和文件夹传输。

### AC-003 大文件与续传

≥10 GB 文件断网后续传，最终摘要一致，内存不随文件大小线性增长。

### AC-004 文件夹

包含空目录、中文、Emoji、10,000 小文件的目录结构正确。

### AC-005 输入隔离

文件传输接近链路上限时，键鼠无明显卡顿。

### AC-006 安装包

干净 Windows 和 macOS 可安装 unsigned 内部包；用户在最终验收时完成系统授权后，键鼠和文件互传可用。

### AC-007 Git 协作

远程仓库存在小功能提交、阶段推送、阶段标签和双平台 artifact；两端不依赖手工复制源码。
