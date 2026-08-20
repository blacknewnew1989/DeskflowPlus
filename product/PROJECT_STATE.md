# 项目状态

> A0 每次阶段推送后更新；远程仓库是唯一状态真相。

## 基本信息

- Product codename: RelayDesk
- origin: 由当前已连接 GitHub 仓库自动识别
- upstream: deskflow/deskflow
- Pinned tag: v1.26.0
- Pinned commit: 760e3b9
- Integration branch: `product/relaydesk-v1`
- Current phase: Phase 4 当前候选收口（手动地址入口、`Ask` 决策、接收方控制和主窗口/托盘回归已集成并通过组件测试；精确标签双平台构建、安装包和物理验收待执行）
- Last updated: 2026-08-20
- User action required during development: none

## Git 状态

- Repository root: `F:\github\DeskflowPlus`
- Active source worktree: `F:\github\DeskflowPlus\working\relaydesk-product`
- origin URL: `https://github.com/blacknewnew1989/DeskflowPlus.git`
- upstream URL: `https://github.com/deskflow/deskflow.git`
- Current branch: `product/relaydesk-v1`
- Last pushed integration tip: `d040b84392d7adf83235ae7b5a47ceab93fa65b5`
- Current local implementation tip: `a624a9e40f027c4165dd8838b61cbe98af68d7f2`（领先 `origin/product/relaydesk-v1` 23 个提交，尚未推送）
- Last frozen protocol commit: `0d091d301aea2140387fdd615150984dfed5bc08`
- Current implementation: v1 内部发布主链已组合。共享 Qt 外壳包含紧凑首页、权限分项、设备/传输区域、集中品牌资源、托盘后台生命周期、输入角色设置、可信设备撤销、手动地址管理和 `Ask` 冲突逐文件决策。接收方直接暂停/继续/取消已接入运行时；当前组件测试通过。macOS TCC/menu bar、物理 Win↔Mac 双机和 unsigned 提示交互仍为 `NOT_RUN`。
- Last verified stage tag: `relaydesk-phase4-20260813-03` (`05f92a1ab721f7fd8b893e47e05643d5988e1719`，历史候选；不得作为当前 2026-08-20 候选的构建或安装证据）

## 2026-08-20 收口复验

| ID | 状态 | Owner | 当前证据 / 下一步 |
|---|---|---|---|
| PAIR-006 | PASS | A2/A3/A0 | `dc4b7efed`、`19e5ab583`、`3f6efb6d1`、`d4d312e88`、`088702900`；撤销确认、TLS 断开、重连拒绝与 520×380 更多菜单已接通；MSVC/Qt 6.10.1 六目标 CTest 6/6 PASS |
| CTRL-002 | PASS | A6/A0 | 接收方直接 pause/continue/cancel 已集成；原生串行 CTest 98/98 PASS。真实双机控制链路仍为 `NOT_RUN`。 |
| DISC-005 | PASS | A2/A3/A0 | 手动地址录入、保存和定向探测已集成；原生串行 CTest 98/98 PASS。真实局域网发现链路仍为 `NOT_RUN`。 |
| CONFLICT-003 | PASS | A6/A3/A0 | `Ask` 的逐文件用户决策和运行时链路已集成；原生串行 CTest 98/98 PASS。真实双机传输决策链路仍为 `NOT_RUN`。 |
| WIN-019 | IN_PROGRESS | A4/A7/A0 | 当前 SHA 的 Windows unsigned 包、安装后桌面/托盘/参数/生命周期验收待精确标签 Actions 构建；本地 Release 打包因缺少原生 Strawberry Perl 已回退到 Actions。 |
| MAC-038 | NOT_RUN | A5/A7/A0 | 最新 App 的 TCC/menu bar 与 Win↔Mac 物理双机仍需真实 macOS 对端；不以本地或 Actions 测试替代 |

## 2026-08-14 紧凑界面变更

- 用户已确认设计输入：
  `product/assets/design/relaydesk-compact-ui-approved-20260814.png`；SHA-256：
  `2f9cf97352ab9819eb5aa2b5d54b9ec9a4fbf171cea56525fb7e2ef149cfbe94`。
- 已确认范围：小巧单栏首页、原创“双设备 + 中继点”临时 Logo、最小化/关闭到 tray 或
  menu bar，以及 macOS 权限分项能力门控与平台适配。
- 设计确认是实现输入，不是完成证据；现有 `relaydesk-phase4-20260813-03` 安装包早于本次
  改版，不能用于证明下列任务已实现。

| ID | 状态 | Owner | 当前证据 / 下一步 |
|---|---|---|---|
| UI-010 | PASS | A3/A0 | 七语言、主窗口布局与托盘回归均已通过；当前原生串行 CTest 98/98 PASS，待精确标签 Windows/macOS 构建 |
| UI-012 | PASS | A3/A0 | 高级页输入角色和 Client 远端主机配置已接通；定向 offscreen 各连续 10 次与 native 各 1 次 PASS，待安装包平台运行验证 |
| BRAND-002 | PASS | A3/A4/A5 | SVG 单源、主题资源、ICO/ICNS/DMG 与 CMake 接线及本地品牌校验 PASS，待平台包核验 |
| TRAY-001 | PASS | A3/A4/A5 | 最小化/关闭到 tray 独立设置及安全停机已实现；定向 offscreen 各连续 10 次与 native 各 1 次 PASS，待平台包与真机交互 |
| MAC-037 | IN_PROGRESS | A5/A3/A0 | 三项权限能力门控、ApplicationActive 自动复检与 150 ms 合并回归 PASS；待最终 App 的系统设置往返前台实测 |

## 自动执行状态

| 项目 | 状态 | 证据 |
|---|---|---|
| origin 可读写 | PASS | bootstrap push and subsequent integration push succeeded |
| upstream fetch | PASS | official refs fetched from `deskflow/deskflow` |
| v1.26.0=760e3b9 | PASS | `760e3b99b00053647a96b405276bf614bd860075` |
| bootstrap commit | PASS | `9b0a4111141abe0a619d5eaeea87b8690b771f70` |
| integration branch push | PASS | remote branch tracks local product branch |
| Windows build | PASS | Phase 2 tag run `31655013105`; CMake/Ninja/MSVC build, CPack MSI/7Z/source, CTest 74/74 |
| macOS build | PASS | Phase 2 tag run `31655013105`; arm64/Qt 6.10.2 build, DMG/App/source, CTest 75/75 |
| GitHub Actions artifacts | PASS | Windows artifact `9164266512`; macOS artifact `9164146467`; 30-day retention |
| Phase 1 implementation | PASS | brand/i18n/device/discovery/pairing/trust/reconnect/device UI and permission guidance integrated through `ead6acbd5` |
| Phase 1 dual-platform CI | PASS | tag run `31621226862`; Windows 60/60, macOS 61/61; build/package/upload all succeeded |
| Draft Release publication | PASS | Phase 2 tag run `31655013105`; four delivery binaries downloaded and API/manifest/local SHA-256 agree |
| Windows installer lifecycle | PASS | TEST-005 run `31657498852`; real clean install/repair/major-upgrade/two uninstalls and residue checks PASS; report `product/docs/reports/TEST-005_WINDOWS_INSTALL_LIFECYCLE.md` |
| Final macOS bundle seal/lifecycle | PASS | TEST-005 run `31657596578`; symlink-preserving App ZIP, strict ad-hoc codesign, DMG verify/mount, isolated install/upgrade/uninstall and user-data preservation PASS |
| Protocol/interface freeze | PASS | tag `relaydesk-protocol-v1-20260813-01`, run `31672497950`; Windows 84/84, macOS 85/85; artifact IDs `9170492840` / `9170386546` |
| Cross-platform file safety adapters | PASS | integration `e6f5fe519`; run `31678206041`: Windows 87/87, macOS 88/88, strict App seal and installer/lifecycle jobs PASS |
| Incoming file runtime composition | PASS | `8f5a992f8`; run `31682728899`: Windows 87/87, macOS 88/88, strict App seal and macOS lifecycle PASS; artifacts `9174449354` / `9174307269` |
| Multi-file/folder/resume production path | PASS | `e742ba4a4`, `7d9bfcbf6`, `5941ebd85`; real two-file/folder and 20 MiB interruption/resume TLS loopbacks PASS |
| Product GUI/reconnect/permission composition | PASS | `479a0f78f`, `b251933dd`, `cc923dacc`, `0341c9b86`, `f79cc64dd`; targeted composition/reconnect/firewall tests PASS |
| Pairing input-layout composition | PASS | `05f92a1ab`; trusted input peer add/persist/idempotency/rejection tests PASS on Windows and macOS |
| Phase 4 exact-tag release | PASS | tag `relaydesk-phase4-20260813-03`; run `31706167585`; Windows 89/89, macOS 90/90, Windows installer and macOS lifecycle PASS; unsigned draft Release published |
| UI-011 local closeout | PASS | product branch reached `939bbb3a0`; 7 Qt regressions, 29 Python contracts, Windows staged-QM loader and brand checks PASS |
| Current seven-language catalogs | PASS | `088702900`; en/es/it/ja/ko/ru/zh_CN are each 182/182; Qt catalog load and 14 translation contracts PASS |
| Current revoke-trust composition | PASS | `dc4b7efed` through `088702900`; clean MSVC/Qt 6.10.1 targeted CTest 6/6 PASS |
| Current exact-SHA dual-platform Actions | NOT_RUN | `a624a9e40` 的 Actions、标签、artifact 和安装生命周期尚未触发；本地 Release 打包已因缺少原生 Strawberry Perl 回退至 Actions |

状态只允许：`NOT_STARTED`、`IN_PROGRESS`、`BLOCKED`、`PASS`、`FAIL`、`NOT_RUN`。

## 阶段状态

| Phase | 状态 | 负责人 | 远程同步要求 |
|---|---|---|---|
| 0 仓库/基线 | PASS | A0/A1/A4/A5/A7 | tag `relaydesk-phase0-20260812-01`, run `31602699800` |
| 1 产品基础 | PASS | A2/A3/A0 | tag `relaydesk-phase1-20260813-04`; run `31623677270`; local Release asset SHA verification PASS |
| 2 文件传输 | PASS | A2/A6/A0 | tag `relaydesk-phase2-20260813-04`; run `31655013105`; Win 74/74, Mac 75/75; four assets triple-digest verified |
| 3 可靠性/UI | PASS | A3/A6/A7 | tag `relaydesk-phase3-20260813-01`; run `31691378517` SUCCESS; Win 88/88, Mac 89/89; physical Win↔Mac remains final acceptance |
| 4 平台/发布 | IN_PROGRESS | A4/A5/A7 | `relaydesk-phase4-20260813-03` 是历史候选；当前 `a624a9e40` 待推送、创建精确标签、触发双平台 Actions 并产生 unsigned 包 |
| 5 增强 | NOT_STARTED | A3/A4/A5 | 按价值推进 |

## 最终 artifact

### Windows（历史 Phase 4 内部候选，2026-08-13）

- Commit: `05f92a1ab721f7fd8b893e47e05643d5988e1719`
- Tag / workflow run: `relaydesk-phase4-20260813-03` / `31706167585`
- Artifact: `relaydesk-windows-x64-05f92a1ab721f7fd8b893e47e05643d5988e1719` (ID `9183676968`)
- Artifact ZIP SHA-256: `d0cd7ab0aee49473d62cd0673a2f0b9e80c6b04a6906fc43c375b2f748161e2c`
- MSI SHA-256: `28340705a8c31d663cd5f10ea605679210c5fec393048c5a2070ae92335d2f07`
- Portable SHA-256: `51e88f915007d51f7efcbe0a9e8496720edebb2b1ac98371584070eedf22655d`
- Build result: PASS (CTest 89/89; unsigned MSI + portable 7Z + source packages)
- Installer result: PASS (clean install, repair, real MSI major upgrade, two uninstalls, service,
  firewall, residue and user-data preservation)
- Physical Win↔Mac runtime result: NOT_RUN; final user acceptance required

### macOS（历史 Phase 4 内部候选，2026-08-13）

- Commit: `05f92a1ab721f7fd8b893e47e05643d5988e1719`
- Tag / workflow run: `relaydesk-phase4-20260813-03` / `31706167585`
- Artifact: `relaydesk-macos-arm64-05f92a1ab721f7fd8b893e47e05643d5988e1719` (ID `9183524798`)
- Artifact ZIP SHA-256: `4e03738e2186ff214081546875594c9c463615401dd5e81130683ba2f371013f`
- App ZIP SHA-256: `ad1a56cd74b32a7ebb499b73376a019745fe3a8e42ce69f1e73bc0696430b8af`
- DMG SHA-256: `0377d49f7bbb9284f666f2033219b5f39c73d7a496238257881ef299a35e2b29`
- Build result: PASS (CTest 90/90; ad-hoc App ZIP + DMG + source packages)
- Lifecycle result: PASS (strict ad-hoc codesign, ZIP symlinks, DMG verify/mount, isolated launch,
  replace, App-only uninstall and user-data preservation)
- Physical Win↔Mac runtime and OS permission result: NOT_RUN; final user acceptance required

### 2026-08-20 当前候选

- Commit: `a624a9e40f027c4165dd8838b61cbe98af68d7f2`。
- 本地 Debug 增量构建：PASS；原生串行 CTest 98/98 PASS（47.41 s），日志
  `product/working/windows-debug-ctest-20260820-131000.log`。
- 定向主窗口/托盘回归：offscreen 各连续 10 次及 native 各 1 次均 PASS。
- `product/tests`：26/26 PASS；`product/scripts/tests`：37/37 PASS；日志分别为
  `product/working/product-tests-a624a9e40.log` 和
  `product/working/script-tests-a624a9e40.log`。
- `validate-package.py`：PASS（49 个必需文件、10 个 JSON、60 个协议向量）；日志为
  `product/working/package-validation-a624a9e40-rerun.log`。
- Windows Release 本地打包：因缺少原生 Strawberry Perl 未执行，已回退到 Actions。
- 标签、Actions run、artifact、MSI/portable/App/DMG、安装生命周期：`NOT_RUN`，不得以本地组件测试替代。
- 物理 Win↔Mac、macOS TCC/menu bar 和 unsigned 提示交互：`NOT_RUN`。

## 最终用户验收

只有开发完成后才填写：

- [ ] Windows 安装
- [ ] macOS 安装/首次打开
- [ ] macOS 权限授权
- [ ] Win→Mac 键鼠
- [ ] Mac→Win 键鼠
- [ ] Win→Mac 文件
- [ ] Mac→Win 文件
- [ ] 大文件断点续传
