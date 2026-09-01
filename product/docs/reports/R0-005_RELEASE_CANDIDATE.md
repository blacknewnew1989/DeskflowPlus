# R0-005 重开发发布候选

## 候选范围

- 初始 A0 基线：`agent/a0/redevelop-p0@f2656459eb97939407374c57f7e74edc2e52ef46`；
- product 起点：`product/relaydesk-v1@c544dc76fb4f29aefb6ef30c8acc4475b6778e07`；
- product 起点是 A0 基线祖先；集成只允许 clean worktree 的 `git merge --ff-only`，禁止 merge commit、
  force push 或历史重写；
- 当前精确候选标签：`relaydesk-phase4-20260901-06`，指向 `caeccb8c62ce19d98474dad21c74f39128324f5d`；
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
Composition 14/0、111.887秒，独立 review GO。第二次修复形成 `-03` 候选，不重跑前两个失败 SHA/tag。

第三候选 `relaydesk-phase4-20260901-03@23940663abe959dab213454bf04a50049878ac81` 的 tag run
`33473271512` 中，Windows controls 槽已经通过，但
`productionTransferMiniBarReflectsAndControlsLoopbackTransfer` 达到 300 秒 timeout；Windows job
`99747185841` 失败，run 终态为 `failure`。该 run 与前两次失败一并保留，不删除、不在同 SHA 重跑，
也不能由此前本机 Composition 14/14 覆盖。

A7 `5a3b81e3b70b30df5abecf93141cff33f092563a` / A0 `7b17b81b745af74382c931e61e89b1455e5fb588`
只调整 CTest 注册：复用同一 `RelayDeskTransferRuntimeCompositionTests` EXE，原聚合项显式运行其余10个
轻量函数，controls 与 mini-bar 各由函数参数启动独立进程。两个重型槽仍为必跑项、可单独失败且日志名称
可定位；没有提高 timeout、QSKIP/条件跳过、删除断言或修改 production。

Windows fresh Release 中 controls 与 mini-bar 隔离槽各 3/3，轻量聚合 PASS；最终完整 CTest
`103/103`、退出0，`#93/#94/#95` 分别为轻量聚合、controls、mini-bar 且均 PASS，工作流等价汇总
守卫退出0。独立 A7 reviewer GO、无未关闭问题。下一唯一候选
`relaydesk-phase4-20260901-04` 必须指向包含该隔离提交与本候选文档的同一新 SHA；第四个 workflow
只能在该 SHA/tag 形成后触发一次，不得先试跑或重跑前三个失败 SHA/tag。

第四候选 `relaydesk-phase4-20260901-04@522793bf3832b7088435f906e50378e2f372f5fa` 的 tag run
`33478646382` 已终态 `failure`。Windows job `99763152526` 的完整 CTest 为 `100/103`，失败项只有：

- `#94 RelayDeskTransferRuntimeCompositionControlsTests`：300008ms，`0xC0000409`；
- `#95 RelayDeskTransferRuntimeCompositionMiniBarTests`：300006ms，`0xC0000409`；
- `#101 RelayDeskTwoProcessRuntimeTests`：58.52s，聚合目标失败但 job 日志与 artifact `ctest.log` 均未
  输出内部函数或 JSON，不能归因为任一具体 two-process 场景。

该 job 的 build、package、Windows translation、安装/repair/major-upgrade/uninstall 与 artifact 上传均
成功，测试 outcome 为 failure，draft Release 因此跳过。Windows artifact `9789710222` 名称为
`relaydesk-windows-x64-522793bf3832b7088435f906e50378e2f372f5fa`，大小 36,607,985 字节；API digest 与
本地下载 ZIP SHA-256 均为 `c7642acc1dce8cf71f32ffa1f48f7e280adaedbc9b4284b909762c7ba3cdd7dd`。
该 run 保留，不在同 SHA 重跑。

A7 `4335ac5d7211c2fe7e6e54c4c3de08da3576fe33` / A0 `619ad34ff` 只给 Composition 的 #93/#94/#95
CTest 注册固定 `QT_QPA_PLATFORM=offscreen`。hosted `-04` 环境已有 `QT_PLUGIN_PATH` 但没有
`QT_QPA_PLATFORM`，而本地此前绿色命令显式使用 offscreen；该修复没有修改 timeout、断言或 production，
也不用于解释旧 #101。

A7 `8dc19127a0bb31264346aeb59bc4d160025960b2` / A0 `df844142c` 将旧聚合
`RelayDeskTwoProcessRuntimeTests` 的8个函数按原顺序注册为同一 EXE 的8个独立 CTest 进程：Complete、
PauseResume、Cancel、FileTree、ListenerResume、ReceiverRelaunch、ReceiverFileTreeRelaunch 与
SenderRelaunch。失败将由独立测试名定位并继续输出既有 process evidence；没有增加场景、跳过或 deadline。
生命周期复核确认普通场景在业务断言前停止双方子进程，三类 relaunch 以逆序析构执行 evidence→stop→
QProcess→临时目录。

父环境清除 QPA 后，controls 与 mini-bar 各 3/3；TwoProcess 8个独立测试按固定顺序连续3轮均为
`8/8`（15.32s、16.67s、14.68s）。最终完整 Release CTest 为 `110/110`、退出0：#94/#95 与新的
#101-#108 均逐项 PASS；汇总守卫逐名校验这10项、总数、退出码和无 FAILED 列表，退出0。纠偏后的
A7 reviewer GO。下一唯一候选 `relaydesk-phase4-20260901-05` 只允许在包含两项 CMake 修复及本报告的
同一精确 SHA 上触发一次；hosted 结果仍是最终判断，不能用本地绿色覆盖。

第五候选 `relaydesk-phase4-20260901-05@83c92d34ea5b395c8415738ceb5ab121d1157fbd` 的 tag run
`33484722108` 已终态 `failure`。Windows job `99782026128` 的 CTest 为 `109/110`，唯一失败是
`#95 RelayDeskTransferRuntimeCompositionMiniBarTests`：300008ms、`0xC0000409`。同一 run 中：

- `#94 RelayDeskTransferRuntimeCompositionControlsTests` hosted 96.46s PASS；
- 新的 `#101-#108` eight-scenario TwoProcess 测试全部逐项 PASS；
- build、package、translation、Windows install/repair/major-upgrade/uninstall 与 artifact 上传成功；
- 汇总守卫因 Test outcome failure 退出1，draft Release 跳过。

因此 Composition 进程隔离、offscreen 与 TwoProcess 可定位性都已得到 hosted 证据，但不能解释 mini-bar
单槽仍在300秒终止。Windows artifact `9791979762` 名称为
`relaydesk-windows-x64-83c92d34ea5b395c8415738ceb5ab121d1157fbd`，API 大小 36,615,919 字节，API
digest 为 `sha256:885faaed926262eb18e0d5c4522750ec9dcb8f1baf7231f0040996d124c06fe1`；本轮未下载，
不写本地 digest 一致结论。

本地把 mini-bar 测试进程限制为2核后，修复前连续3轮仍 PASS（60.5s、71.6s、71.5s），所以不能把
hosted 失败简化为算力不足。A7 `e4957e0271344719b470261d1dcefb88fed4b9ea` / A0 `63fa60736`
只增加 `start→offer→accept→active→details→pause→paused→resume→completed→teardown→stopped` 阶段日志，
并在 `QTest::currentTestFailed()` 时于所有 runtime、connection context 与局部状态仍存活时输出 stage/errors，
随后停止 composition/sender。所有业务断言、timeout、文件大小和 production 均未改变。

staged guard 后2核三轮均完整到 `stopped`（64.5s、68.4s、54.4s），完整 Release CTest `110/110`、
逐项守卫退出0。A7 reviewer GO 的含义仅为允许提交并触发一次新的 hosted 诊断；它不证明 #95 根因已修。
下一唯一候选 `relaydesk-phase4-20260901-06` 必须绑定包含该 guard 与本报告的同一精确 SHA；若仍失败，
按最后 stage/guard errors 继续定位，不得再把300秒黑箱当成绿色或重复同 SHA。

## 第六候选 Windows 终态

第六候选 `relaydesk-phase4-20260901-06@caeccb8c62ce19d98474dad21c74f39128324f5d` 的 tag run
`33488670032` 已终态 `success`。annotated tag object `b57ef0426514b17b002e77e5758035eb40eff367`
指向该精确 commit，`product/relaydesk-v1` 远端复读也为同一 SHA。Windows job `99794677509`：

- build、package、translation、CTest、artifact 与安装/repair/major-upgrade/uninstall 全部 SUCCESS；
- CTest `110/110`、总耗时196.14s；#94 controls 69.40s、#95 mini-bar 69.04s；
- #101-#108 eight-scenario TwoProcess 全部逐项 PASS；
- Windows translation JSON 与 `test005-windows-install-regression.json` 均为 `PASS`。

Windows artifact `9793399071` 名称为
`relaydesk-windows-x64-caeccb8c62ce19d98474dad21c74f39128324f5d`，API size 36,614,412字节，API digest
`sha256:2fefb4273959562b41ed756ffd32faf691b88d8e54c1fdb350256a5773f9ef0b`；本地下载 ZIP 大小与
SHA-256 完全一致。解压后 `artifact-manifest.json` 的 commit/platform 为精确 SHA / `windows-x64`，
`SHA256SUMS.txt` 四项全部本地复算通过：

- Windows portable 7z：`87e85918bb9f50727874721ce68f2fabee68c9e7acfdf3b39a94da82f74ce917`；
- Windows unsigned MSI：`f406bffd22dabb611f8f44826f97554953ea469b9777bc91ba2e938f755deed8`；
- source portable 7z：`3e85af1779fd9a01551f5688f32be44234fdb14546214105db3a17855a93a6bd`；
- source portable zip：`bd48168aa38e2359604f2a58c9c4e9b2f5f3ec86cffc531e16cb6af08ba7c96d`。

草稿 Release `380350411`（`draft=true`，tag `relaydesk-phase4-20260901-06`）已存在；Windows portable、
MSI 与两份 source portable 资产的名称、大小、digest 与本地 artifact 清单一致。Release API 的
`target_commitish=product/relaydesk-v1`，同时 tag object 与该 branch 均已复读到精确候选 SHA，未依赖
可变分支名推断候选身份。

本报告当前只关闭 Windows 子门槛。R0-005 总项仍为 `IN_PROGRESS`，等待 macOS owner 对同一 SHA 的
package artifact 本地 digest、包内清单和 lifecycle evidence 做独立复核；本线程不把 hosted run 的
macOS success 外推为该复核已完成。

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
