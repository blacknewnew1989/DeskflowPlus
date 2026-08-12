# START HERE

## 执行边界

本开发包进入已经连接 GitHub 仓库的 Codex 会话后，Codex 自行定位并解压开发包、获取源码、安装依赖、执行命令、提交、推送和制作安装包。开发过程中用户不参与。

项目完成后，用户只在 Windows 和 Mac 上安装生成的包并执行最终验收。`CODEX_START_PROMPT.txt` 是启动 A0 的唯一目标指令。

## Codex 首轮必须自动完成（以下均不由用户操作）

1. 识别当前 Git 仓库与 `origin`；
2. 验证 GitHub 读取和推送；
3. 保留 `origin`，添加 Deskflow 官方 `upstream`；
4. 获取 v1.26.0 并验证 `760e3b9`；
5. 自动创建或恢复 `product/relaydesk-v1`；
6. 把本包资料放入根 `AGENTS.md` 与 `product/`；
7. 安装 `.github/workflows/relaydesk-build.yml`；
8. 创建 bootstrap 提交并推送；
9. 检测 Windows/macOS 构建环境；
10. 本机缺少工具时自动安装，无法安装时使用 GitHub Actions 回退；
11. 自动监控 Actions、下载双平台 artifact；
12. 开始 Phase 0 和可并行的功能开发。

## 首条提示词

```text
当前目录就是已连接并已认证 GitHub 的项目仓库；读取根目录 AGENTS.md 与 docs/01_PRD.md（若资料已安装则读取 product/docs/01_PRD.md），以 A0 总控模式全自动执行。第一项实际操作必须定位并运行 scripts/autonomous-init-repo.py 或 product/scripts/autonomous-init-repo.py，保留 origin、添加 upstream、获取并验证 Deskflow v1.26.0、安装 RelayDesk 资料和 relaydesk-build.yml、创建 bootstrap 提交并推送；读取脚本输出的 SOURCE_WORKTREE 后自行切换进去继续。之后自动安装或准备 Windows/macOS 依赖、创建分支与 worktree、并行开发和测试；每个可独立验证的小功能立即提交，任务完成或跨平台共享时推送代理分支，每个阶段完成后合并并推送 product/relaydesk-v1、推送 relaydesk-phase 标签、监控双平台 Actions、修复失败并下载 Windows 安装包/便携包与 macOS App/DMG。不得把源码下载、工具安装、命令执行、提交、推送、Actions、artifact 下载、打包或跨平台同步交给用户；不得新增强制 PR、人工审批、required checks、覆盖率阈值或过度安全设计。缺少签名凭据时交付明确标注的 unsigned 内部包，用户只负责最终安装、系统权限授权和验收。
```

## 最终用户验收范围

Codex 最终应交付：

- Windows x64 安装包或便携包；
- macOS Apple Silicon `.app`/`.dmg`；
- 两个平台的版本号、commit 和 SHA-256；
- 安装与卸载说明；
- macOS 权限授权步骤；
- Windows 防火墙提示；
- Win→Mac、Mac→Win 键鼠和文件传输验收表；
- 已知问题与 `NOT_RUN` 项。

用户最终只负责：安装、按系统提示授权、连接两台机器并执行验收用例。

## 自动入口说明

PRD 中出现的 Git、PowerShell、Bash、CMake、CPack 和 `gh` 命令全部是 Codex 的执行规范，不是给用户的操作步骤。Codex 必须自行运行、读取失败日志并修复；用户在最终交付前不参与环境准备或跨平台同步。
