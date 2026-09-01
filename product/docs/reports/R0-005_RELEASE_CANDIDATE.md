# R0-005 重开发发布候选

## 候选范围

- A0 基线：`agent/a0/redevelop-p0@f2656459eb97939407374c57f7e74edc2e52ef46`；
- product 起点：`product/relaydesk-v1@c544dc76fb4f29aefb6ef30c8acc4475b6778e07`；
- product 起点是 A0 基线祖先；集成只允许 clean worktree 的 `git merge --ff-only`，禁止 merge commit、
  force push 或历史重写；
- 唯一候选标签：`relaydesk-phase4-20260901-01`。远端 tag 与 draft Release 均已确认未占用；
- 本文件所在候选提交形成后，product、tag 与最终 workflow 必须指向该同一精确 SHA。

## 唯一工作流

唯一工作流为 `.github/workflows/relaydesk-build.yml`。`relaydesk-phase*` tag 会运行 Windows x64 与
macOS arm64 package matrix、macOS install lifecycle，并由 `Publish unsigned draft release` job 创建或更新
同 tag 的草稿 Release。不得创建第二套 workflow、重复触发同一 SHA 或手工替代其 Release 资产流程。

分支 run `33464083567@38247729b3916ecc0c21d39a2fef8e85fab3dda4` 只证明当时分支状态，既不是本候选
product/tag SHA，也不会执行标签专属 draft Release，因此不作为 R0-005 最终证据。

## PASS 门槛

`R0-005` 当前为 `IN_PROGRESS`。只有以下证据全部绑定同一候选 tag/SHA 后才可转为 `PASS`：

1. Windows 与 macOS package jobs、CTest 和材料诊断终态成功；
2. Windows package artifact、macOS package artifact 与 macOS install lifecycle evidence artifact 齐全；
3. artifact API name、ID、size、digest 与本地下载 ZIP SHA-256 一致；
4. 解压后的 Windows/macOS package 清单与 `SHA256SUMS` 通过本地复核；
5. macOS lifecycle JSON 为 PASS，并保留 TCC 权限项的 `NOT_RUN`；
6. 标签对应 draft Release 存在，target commit、资产名、大小与 SHA-256 清单均可复读；
7. `product/PROJECT_STATE.md`、`product/TASK_BOARD.md` 与本报告写回最终 run、artifact、digest、
   Release 和剩余边界。

## 最终人工边界

`R0-006` 保持 `FINAL_ACCEPTANCE_REQUIRED`。物理 Windows↔macOS、macOS TCC/menu bar、unsigned
SmartScreen/Gatekeeper 与人工安装交互不由 hosted package/lifecycle 证据替代。
