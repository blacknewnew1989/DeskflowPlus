# R0-005 重开发发布候选

## 候选范围

- 初始 A0 基线：`agent/a0/redevelop-p0@f2656459eb97939407374c57f7e74edc2e52ef46`；
- product 起点：`product/relaydesk-v1@c544dc76fb4f29aefb6ef30c8acc4475b6778e07`；
- product 起点是 A0 基线祖先；集成只允许 clean worktree 的 `git merge --ff-only`，禁止 merge commit、
  force push 或历史重写；
- 当前唯一候选标签：`relaydesk-phase4-20260901-03`。远端 tag 与 draft Release 均已确认未占用；
- 本文件所在候选提交形成后，product、tag 与最终 workflow 必须指向该同一精确 SHA。

## 唯一工作流

唯一工作流为 `.github/workflows/relaydesk-build.yml`。`relaydesk-phase*` tag 会运行 Windows x64 与
macOS arm64 package matrix、macOS install lifecycle，并由 `Publish unsigned draft release` job 创建或更新
同 tag 的草稿 Release。不得创建第二套 workflow、重复触发同一 SHA 或手工替代其 Release 资产流程。

分支 run `33464083567@38247729b3916ecc0c21d39a2fef8e85fab3dda4` 只证明当时分支状态，既不是本候选
product/tag SHA，也不会执行标签专属 draft Release，因此不作为 R0-005 最终证据。

## 首次标签诊断

首次 product fast-forward 与标签 `relaydesk-phase4-20260901-01@9905434d0c8c42e833339294fc4209ff626dce8d`
保留不重写。tag run `33466625278` 的材料诊断与 macOS 102/102 成功；Windows build、package、100/101
CTest、安装/repair/major-upgrade/uninstall 与 artifact 上传完成，但
`RelayDeskTransferRuntimeCompositionTests::productionTransferCenterButtonsPauseResumeAndCancelLoopbackTransfers`
达到 300 秒 timeout 并以 `0xC0000409` 退出。Windows package artifact `9785480151` 与 macOS package
artifact `9785274312` 保留；因 package matrix 终态失败，macOS lifecycle 与 draft Release jobs 被跳过，
该标签不满足 R0-005。

`38247729b..9905434d0` 只有文档变化；同实现的 branch run `33464083567` 曾成功，本机同槽 Release
复现也在 52.997 秒通过，因此不能把失败归因于候选文档或直接外推为 production 缺陷。独立 A7 审阅确认
该槽的捕获连接和 0ms timers 错用长寿命测试对象 `this` 为 context，失败早退/析构时存在访问局部状态的
生命周期风险，与 `0xC0000409` 一致。

A7 owner `211b8eb08bccf8e2e33a3e0b1f3952e8bb73c363` 只修复该测试槽：所有捕获连接和 timers 改用
局部 connection context，失败 scope guard 在局部状态仍存活时停止 runtimes，并增加阶段 receipt；
production、业务断言和 timeout 均未改变。fresh Windows Release 定向槽 3/3（每轮约53秒）、完整
Composition 14/14，独立 release reviewer GO。

第二候选 `relaydesk-phase4-20260901-02@21c454c1d57039873cb5af2fb1e50371baa16c33` 的 tag run
`33470396960` 仍在同一槽达到 300 秒并以 `0xC0000409` 退出；Windows package/install 与 artifact
`9786722273`、macOS artifact `9786539148` 完成，但 lifecycle/Release 被跳过。阶段 receipt 未进入
artifact，代码复核定位到 cancel menu 的一次性 `cancelClickQueued`：第一次 queued callback 若在
More/menu/action geometry 尚未就绪时返回，门闩永久保持 true，真实 cancel 手势不再尝试；随后失败 guard
在半完成传输上 stop，最终拖到函数 timeout。

A7 `5694d8b0c19bb714b02c65926caef9e9bcf0cc17` 只把该测试手势改为局部 10ms 有界探测：仅当
model、More、cancel action、menu 与 action geometry 全部就绪后停止 timer，并通过真实鼠标点击菜单取消；
所有 callbacks 继续绑定 local connection context，production、业务断言和 timeout 不变。fresh Release
三轮分别 58.294/72.935/61.508 秒，均记录 `cancel-wait→menu-ready→cancel-click` 且 3/0；完整
Composition 14/0、111.887秒，独立 review GO。当前 `relaydesk-phase4-20260901-03` 必须指向包含两个测试
修复及本候选文档的同一新 SHA，不重跑前两个失败 SHA/tag。

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
