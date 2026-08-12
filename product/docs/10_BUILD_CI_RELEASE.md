# 10 自动构建、CI、打包与发布

## 1. 执行责任

A0/A4/A5/A7 负责全部环境检查、依赖下载、安装、构建、测试、打包、Actions 监控、artifact 下载、校验和远程同步。用户不执行本文件中的任何命令。

Codex 不得只把命令写进报告；必须实际执行。某台本机无法自动准备时，立即使用 GitHub Actions 对应 runner，不向用户转交环境安装。

## 2. 固定上游构建基线

```text
Deskflow tag:        v1.26.0
Baseline commit:     760e3b9
Integration branch: product/relaydesk-v1
CMake:               3.24+
Qt:                  6.7+
OpenSSL:             3.0+
C++:                 20
Windows CI Qt:       6.10.1
macOS arm64 CI Qt:   6.10.1
WiX:                 5.0.2
macOS arm64 target:  14+
```

A1 每次改变构建流程前必须重新读取：

```text
doc/dev/build.md
CMakeLists.txt
.github/actions/install-dependencies/action.yml
.github/workflows/continuous-integration.yml
deploy/windows/deploy.cmake
deploy/mac/deploy.cmake
```

## 3. 自动选择构建路径

```text
本机工具链完整
  → 本机构建、测试、打包

缺少普通依赖且可非交互安装
  → Codex 自动安装后重试

无管理员权限、缺 Xcode/Homebrew 或安装失败
  → 自动触发 relaydesk-build.yml

缺代码签名凭据
  → 继续生成明确标注的 unsigned 内部包
```

缺签名、缺本地平台或缺系统权限，不得阻塞共享核心开发、提交、推送和 CI 打包。

## 4. Windows 自动准备与打包

### 4.1 唯一入口

A4 在 Windows 会话中自动运行：

```powershell
& .\product\scripts\package-windows.ps1 `
  -RepoRoot (git rev-parse --show-toplevel)
```

`package-windows.ps1` 会依次调用：

```text
setup-windows.ps1
  → build-windows.ps1 Release + tests
  → CPack package + package_source
  → collect-ci-artifacts.py
  → dist/windows/<commit>/
```

### 4.2 自动安装内容

`setup-windows.ps1` 自动检测并尽量安装：

- Git；
- CMake；
- Ninja；
- Python；
- Visual Studio 2022 Build Tools / MSVC；
- Qt 6.10.1；
- vcpkg；
- OpenSSL/gtest；
- .NET SDK；
- WiX 5.0.2 与扩展；
- 7-Zip。

本地准备失败时脚本写入 `product/working/toolchains/windows.json`，A0 随即触发 Windows Actions job。不得要求用户安装上述工具。

### 4.3 等价核心命令

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
cmake --build build/windows/release --config Release `
  --target package package_source --parallel
```

### 4.4 Windows 产物

至少交付一种可运行便携包；WiX 可用时同时交付安装包：

```text
dist/windows/<commit>/
├─ *.7z / *.zip
├─ *.msi（WiX 成功时）
├─ source package
├─ SHA256SUMS.txt
└─ artifact-manifest.json
```

## 5. macOS 自动准备与打包

### 5.1 唯一入口

A5 在 macOS 会话中自动运行：

```bash
./product/scripts/package-macos.sh \
  --repo "$(git rev-parse --show-toplevel)"
```

脚本执行：

```text
setup-macos.sh
  → build-macos.sh Release + tests
  → CPack package + package_source
  → collect-ci-artifacts.py
  → dist/macos/<commit>/
```

### 5.2 自动准备内容

- Xcode/Command Line Tools 检测；
- Homebrew 检测；
- CMake；
- Ninja；
- Qt；
- OpenSSL 3；
- googletest；
- Python。

Xcode/Homebrew 缺失且需要人工系统授权时，本地构建记为 `NOT_RUN`，A0 直接使用 macOS Actions runner，不要求用户在开发阶段安装。

### 5.3 等价核心命令

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
cmake --build build/macos/release \
  --target package package_source --parallel
```

### 5.4 macOS 产物

```text
dist/macos/<commit>/
├─ *.dmg
├─ RelayDesk-*-unsigned-*.app.zip
├─ source package
├─ SHA256SUMS.txt
└─ artifact-manifest.json
```

无 Developer ID 时使用上游可行的 ad-hoc/unsigned 内部包。正式签名、公证和 staple 是可选发布步骤，不是开发门禁。

## 6. GitHub Actions 双平台回退

A0 首轮自举自动安装：

```text
.github/workflows/relaydesk-build.yml
```

工作流触发：

- `workflow_dispatch`；
- 推送 `product/relaydesk-v1`；
- 推送 `release/**`；
- 推送 `relaydesk-phase*` 或 `v*` 标签。

矩阵至少包含：

- `windows-2022` x64；
- `macos-15` arm64；
- configure/build/test/package；
- binary/source package；
- artifact manifest 和 SHA-256；
- artifact upload。

开发资料诊断 job 使用 `continue-on-error`，工作流不创建 branch protection、required check、环境审批或人工发布审批。

## 7. Actions 自动监控与下载

A0/A7 自动调用：

```bash
python product/scripts/run-github-actions.py \
  --repo "$(git rev-parse --show-toplevel)" \
  --ref product/relaydesk-v1
```

阶段标签已经通过 push 自动触发时：

```bash
python product/scripts/run-github-actions.py \
  --repo "$(git rev-parse --show-toplevel)" \
  --ref <relaydesk-phase-tag> \
  --no-trigger
```

脚本负责：

1. 验证 `gh` 登录；
2. 触发或查找匹配 commit 的 run；
3. 等待运行结束；
4. 失败时自动保存失败日志入口并以非零状态返回给 A0；
5. 下载全部 artifacts，并为下载文件生成 `DOWNLOAD_SHA256SUMS.txt`；
6. 写入 `product/working/actions/<run-id>.json`，记录 commit、run URL、下载状态和文件 SHA-256；
7. 工作区无预先 staged 改动时，自动提交并推送该轻量运行报告，提交带 `[skip ci]`，避免报告提交重复触发构建。

若 `gh` 不可用，A0 使用当前会话已连接的 GitHub 工具执行同等操作，不向用户转交命令。

## 8. 提交、推送与构建节奏

### 小功能

```text
实现最小切片
→ 运行受影响测试
→ 独立 commit
→ 共享接口/任务完成/平台切换时 push 代理分支
```

### 阶段完成

```text
A0 合并到 product/relaydesk-v1
→ 更新 PROJECT_STATE/TASK_BOARD
→ 运行当前可用测试
→ complete-stage.py
→ push 集成分支
→ push relaydesk-phase* 标签
→ Actions 自动打包
→ 下载 artifacts
→ 写入 run ID、SHA-256 和 NOT_RUN
```

入口：

```bash
python product/scripts/complete-stage.py \
  --stage phase3 \
  --summary "transfer center and resume"
```

## 9. 内部发布交付物

- Windows x64 安装包/便携包；
- macOS arm64 App/DMG；
- source package；
- LICENSE/NOTICE；
- commit、branch、tag；
- SHA-256；
- Actions run URL 与构建日志；
- 安装/卸载说明；
- macOS 权限说明；
- Windows 防火墙说明；
- 最终验收清单；
- 已知问题与 `PASS/FAIL/NOT_RUN`。

## 10. 禁止新增的门禁

- 强制 PR；
- 强制双人 review；
- branch protection；
- required checks；
- 覆盖率阈值；
- SAST/DAST 阻断；
- 发布审批；
- 必须签名后才允许开发或测试；
- 因本地缺少某个平台而停止共享核心开发。

CI 用于自动发现问题和生成产物，不是人工审批系统。
