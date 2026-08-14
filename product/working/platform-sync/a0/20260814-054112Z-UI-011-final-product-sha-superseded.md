# UI-011 最终产品 SHA 更正

- 时间（UTC）：2026-08-14T05:41:12Z
- 状态：`FINAL_PRODUCT_SHA_SUPERSEDED`
- 当前最终产品 SHA：`939bbb3a02d9d7062a6a5cff4363cea95f1abd11`
- 已作废 SHA：`5b5469eaf6d7f37a0b63855b43e583c9f1251a63`
- 产品分支：`product/relaydesk-v1`

首轮精确 SHA Actions 的 `Development materials diagnostic` 发现 canonical 工作流与开发包模板副本不一致。A0 已把 Windows 安装树七语言校验同步到模板，并验证：

- `product/scripts/validate-package.py` PASS（49 个必需文件、7 个 JSON、60 个协议向量）；
- canonical/template 逐字节一致；
- 工作流 YAML 解析 PASS；
- Windows/macOS 打包契约 23/23 PASS。

修复已以非强制快进进入产品分支。后续双平台 Actions、App/DMG、七语言闭包和权限前台复检只能使用 `939bbb3a02d9d7062a6a5cff4363cea95f1abd11`；此前 SHA 与产物不得作为最终证据。新 Actions run：`31773763895`。
