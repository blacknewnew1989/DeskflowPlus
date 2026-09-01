# R4 文件传输设置运行时验收

## 范围

本报告记录 `R4-UI-009` 在 owner `8aa690359c38096f155c3883647612ac5a1eb7ee`、A0 内容等价提交 `d015027e9470ff26d532ddbef80b03912bc18a52` 的动态证据。范围仅为 Qt localhost/offscreen 下 production 文件传输设置的保存、重开和同窗口运行时应用。production 零改动；测试只把 runtime 槽改为通过真实 DevicesDock 设置按钮进入 dialog。

## 动态证据

`MainWindowLayoutTests::fileTransferSettingsPersistAndReopen` 通过真实 `SettingsDialog` 的 Save 按钮设置 receive root、incoming policy 与 conflict policy。保存后重开 dialog，三个控件分别精确回显：规范化 receive root、`AutoAcceptTrusted` 和 `Skip`。

`MainWindowLayoutTests::fileTransferSettingsEntryAppliesToRuntime` 在同一 `MainWindow` 的 `IncomingOfferModel` 放入 pending offer，确认 offer panel 与 `relaydeskChangeIncomingOfferSettingsButton` 可见且启用，再用真实 mouse click 进入 production 设置入口。Save 后，同一 `MainWindow` 的 `TransferRuntimeComposition::incomingOffers()` 立即读取到新的 destination root、trusted auto-accept 与 `Overwrite` conflict policy；`TransferSettingsStore` 复读三项持久化值一致，无需重启应用。

A0 作为唯一构建执行者在 fresh 目录 `C:\Users\52323\AppData\Local\Temp\relaydesk-a3-r4-settings-runtime-evidence-a0` 构建两个目标 282/282、退出 0。`ui009-logs` 中：

- `fileTransferSettingsPersistAndReopen-{1,2,3}.txt` 均为 3/0/0；
- `fileTransferSettingsEntryAppliesToRuntime-{1,2,3}.txt` 均为 3/0/0；
- `main-window-full.txt` 为 19/0，`transfer-settings-full.txt` 为 10/0；
- 所有进程退出 0，A7 只读复核 GO。

A0 cherry-pick 后在 `C:\Users\52323\AppData\Local\Temp\relaydesk-a0-ui001b` 增量构建 MainWindowLayout 4/4；`ui009-integration-logs` 中两个槽分别为 3/0/0、退出 0。

## 结论与边界

`R4-UI-009` 在 Qt localhost/offscreen production UI 范围为 `PASS`。这不证明 native Windows/macOS 窗口、系统文件选择器、系统设置、TCC、物理 Win↔Mac 或正式发布行为；这些项目保持 `NOT_RUN`。
