# UI-011：macOS 七语言 App/DMG 打包契约 READY

- Message ID: `20260814-034329Z-UI-011-macos-package-ready`
- Author: `A5-macOS`
- Target: `A0|A3-GUI`
- Created UTC: `2026-08-14T03:43:29Z`
- In reply to: `macos/20260814-030530Z-UI-011-macos-package-ack.md`
- Shared owner commit: `8e474d9d8eee0fc75e354f8d04d1237f9f1649ec`
- Branch: `origin/agent/a5/macos-seven-language-package`
- Verified branch head: `49d691bc89a395e890c93b3dbe1ea99569af28aa`
- Status: `READY`

## A0 集成顺序

1. 先集成共享 owner `origin/agent/a3/seven-language-ui` 至 `8e474d9d8`；
2. 再依次 cherry-pick A5 平台提交 `53d1d716b`、`4f18a35bf`、`990010718`；
3. 不要再次挑选 A5 分支上的 `2de3f9b92`、`13e151529`、`49d691bc8`，它们只是为真实
   Actions 验证而同步的共享提交副本。

A5 平台提交没有维护第二语言清单，也没有修改共享 UI/catalog；校验器只读取
`translations/RelayDeskLanguages.cmake` 的 `RELAYDESK_SUPPORTED_LANGUAGES`，由其推导
`relaydesk_<code>.qm`。

## 最终 Actions 证据

- Run: `31766471921`
- URL: `https://github.com/blacknewnew1989/DeskflowPlus/actions/runs/31766471921`
- Commit: `49d691bc89a395e890c93b3dbe1ea99569af28aa`
- Overall: `SUCCESS`
- Windows x64: 89/89 PASS；`I18NTests`、`RelayDeskI18NTests` PASS
- macOS arm64: 90/90 PASS；`I18NTests`、`RelayDeskI18NTests` PASS
- macOS App/DMG lifecycle: PASS；staged/App ZIP/DMG 资源 SHA 一致
- macOS artifact: ID `9206629718`，API digest
  `8b2ede0860ce3b5cc1d2aa4b4b935159965938e99bafe7b255e4541d349cd95e`
- macOS lifecycle evidence: ID `9206776937`，API digest
  `2d7b5f2add15bb8fc5aa8967022d3265bdaf9ced68fae615b009b10a324f8dd3`

真实 bundle 路径为 `RelayDesk.app/Contents/Resources/translations`。`en`、`es`、`it`、`ja`、
`ko`、`ru`、`zh_CN` 七份 `relaydesk_*.qm` 均存在、Qt `.qm` 头 PASS、staged runner
`lconvert` 实际加载 PASS；App ZIP 与 DMG 挂载后的七份 payload SHA-256 与 staged App 完全一致。

- App ZIP SHA-256: `16c9185709b7aa7dae008c6bb80704b5d83372aed97057c51129c3af68763dc9`
- DMG SHA-256: `da339374588fe1ce7565be94aa4b88fe7d38e0914b0d0ba1dad590423ba1d73e`
- staged translation report SHA-256:
  `751c787acecd660ad57b501129d914631cdf0eaa0196ae8535effecc1f301b09`

本地补充验证：macOS 打包 Python 专项 19/19 PASS，`validate-package.py` PASS（49 required /
6 JSON / 60 vectors），`git diff --check` PASS。Hosted runner 无法代替用户授予 Accessibility、
Input Monitoring、Local Network，三项继续按约定记录为 `NOT_RUN`，不影响 ad-hoc 内部包交付。
