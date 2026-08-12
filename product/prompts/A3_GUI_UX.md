# A3 Qt GUI 与产品体验代理提示词

读取根 `AGENTS.md`、`docs/07_UI_UX_SPEC.md`、`docs/01_PRD.md`。先核查上游实际 GUI 结构、Qt Widgets/QML 形式和模型风格，遵循原项目，不自行换框架。

## 任务范围

- 品牌显示与资源；
- 中文/英文 i18n；
- Devices 首页；
- discovered/trusted/online 状态；
- 配对向导；
- 屏幕布局整合；
- 设备卡片 drop target；
- Send Files dialog；
- Incoming Offer dialog；
- Transfer Center；
- settings/trust/history；
- 权限引导；
- 通知/托盘入口。

## 硬约束

- UI 不持有 socket、QFile、hash worker。
- 只通过 A2/A6 的 service API 和 immutable snapshot。
- 用户可见字符串全部翻译。
- 进度更新不高于 5 Hz。
- 不把远端字符串当 HTML。
- 拖文件时异步扫描，不冻结 GUI。
- 日常页面不迫使用户理解 Server/Client；高级设置可保留。
- 不做账号、团队、云端、统计大屏。
- P0 不做原生 OLE/NSDraggingSession 跨系统拖拽延续。

## 首个纵向 UI

在 service 可用后按顺序：

1. 设备卡片展示；
2. 配对入口；
3. drop files 到 trusted online card；
4. offer/accept；
5. 传输进度；
6. 完成/失败；
7. pause/resume/cancel；
8. history/open folder。

## 视觉

- 使用现有 Qt theme 能力；
- 强状态层级和留白；
- 不加入大型 UI 依赖；
- 图标来源和许可证清楚；
- 支持 DPI、暗色、键盘导航；
- 动效短且可关闭。

## 测试

- model/state unit tests；
- snapshot 更新；
- 10k file task 不创建 10k 可见 widgets；
- 翻译编译；
- keyboard navigation；
- permission/error states；
- screenshot/manual checklist on Win/Mac。

## 输出

真实可运行 UI、截图或可复现步骤、翻译文件、测试结果。不要只创建静态 mockup。
