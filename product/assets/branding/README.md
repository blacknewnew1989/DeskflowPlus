# RelayDesk 品牌资产

## 当前状态

2026-08-14，用户确认
[`product/assets/design/relaydesk-compact-ui-approved-20260814.png`](../design/relaydesk-compact-ui-approved-20260814.png)
作为紧凑界面与临时 Logo 的视觉基线。参考图 SHA-256：
`2f9cf97352ab9819eb5aa2b5d54b9ec9a4fbf171cea56525fb7e2ef149cfbe94`。

该确认只冻结设计方向，不代表生产图标、平台资源或打包接线已经完成。生产资产由
`BRAND-002` 跟踪，当前状态为 `IN_PROGRESS`。RelayDesk 仍是临时代号，正式产品名和商标
决策留到对外发布前完成。

## Logo 设计契约

- 使用原创“双设备 + 中继点”图形，表达两台设备通过 RelayDesk 建立局域网协作；
- `product/assets/branding/relaydesk-mark.svg` 是计划中的唯一几何源，平台尺寸不得分别手绘；
- App、安装包和关于页使用彩色变体；Windows tray 与 macOS menu bar 使用同一几何派生的
  单色变体；
- `16 px` 下仍须看出两个设备和中继点，不依赖小字、细线或渐变传递核心语义；
- macOS menu bar 使用 template 图标，让系统适配浅色、深色和高对比外观；
- 不复用 Deskflow 图形，也不得让 RelayDesk 看起来像 Deskflow 官方发布。

## 计划输出

| 用途 | 资产/接入点 | 完成条件 |
|---|---|---|
| 几何单源 | `relaydesk-mark.svg` | 原创 SVG、可缩放、包含来源说明 |
| Windows App/安装包 | `.ico` 多尺寸派生资源 | 16/20/24/32/48/256 px 与高 DPI 检查通过 |
| Windows tray | 单色小尺寸派生资源 | 浅色、深色和高对比模式可辨 |
| macOS App/DMG | `.icns` 与安装图稿 | Finder、Dock、About、DMG 显示一致 |
| macOS menu bar | template 单色派生资源 | 系统明暗外观和 Retina 检查通过 |
| 集中配置 | `product/branding/RelayDeskBrand.cmake` | App、core、daemon 与打包入口不再使用旧图标回退 |

在 SVG 单源、派生资源、集中配置、16 px 视觉检查和双平台打包验证全部完成前，不得把
`BRAND-002` 标为 Done。
