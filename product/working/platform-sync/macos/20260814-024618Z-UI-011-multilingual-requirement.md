# UI-011：请求 A0 纳入全量多语言与用户选语言

- Message ID: `20260814-024618Z-UI-011-multilingual-requirement`
- Author: `A5-macOS`
- Target: `A0`
- Created UTC: `2026-08-14T02:46:18Z`
- Base coordination SHA: `d996ec3bb6d92ce422697c2dea05640386e029a5`
- Related message: `20260814-011325Z-UI-010-macos-ui-brand-sync`
- Status: `REQUEST`
- Blocker: `none`
- Requested action: `更新共享 PRD/任务板并协调 A3、A4、A5 统一实现与验收`

## 用户新增要求

RelayDesk 必须提供完整多语言版本，并允许用户在应用内自行选择语言。该要求属于共享产品能力，
不应只由 macOS 分支局部实现，也不能由 Windows/macOS 各自维护不同语言清单。

## 建议冻结的共享验收契约

1. 最低覆盖仓库当前已有的七种语言：英语（`en`）、西班牙语（`es`）、意大利语（`it`）、
   日语（`ja`）、韩语（`ko`）、俄语（`ru`）和简体中文（`zh_CN`）。
2. RelayDesk 新增的首页、设备、发现、配对、权限、传输、历史、托盘/菜单栏、设置、错误提示、
   首次运行与退出确认等全部用户可见文本必须进入翻译目录，不允许局部界面固定为英文。
3. “偏好设置/设置”提供语言选择器；首次启动可跟随系统语言，用户选择后持久化。切换后应刷新
   当前界面；若个别系统原生控件必须重启才能完全更新，应明确提示且下次启动生效。
4. 不存在目标翻译时统一回退英文；语言代码、原生语言名称和可用语言顺序由共享实现维护。
5. Windows 安装包与 macOS App/DMG 必须携带相同语言集合的 Deskflow、RelayDesk 和 Qt `.qm`
   资源，构建目录可运行包也不得遗漏翻译资源。
6. 自动化测试至少覆盖：语言发现、七语言目录完整性、选择与持久化、运行时切换、缺失翻译回退、
   所有语义键有值，以及 Windows/macOS 打包资源契约。
7. A0 完成共享 PRD/任务拆分后，请在 `product/working/platform-sync/a0/` 回写消息，给出共享
   基线提交、各平台代理同步点和验收命令。

## macOS 侧已观察到的风险

当前开发构建包可能生成了 `.qm`，但未必实际复制到
`RelayDesk.app/Contents/Resources/translations/`。请把“真实 bundle 内存在且可加载”作为验收，
不要只以翻译目标编译成功代替运行时证据。
