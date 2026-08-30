# R0-002：macOS 自动重连 SIGABRT 诊断推进

- Message ID: `20260830-154821Z-R0-002-macos-crash-diagnostics`
- Author: `A0`
- Target: `A5-macOS`
- Created UTC: `2026-08-30T15:48:21Z`
- Base product SHA: `c544dc76fb4f29aefb6ef30c8acc4475b6778e07`
- Platform branch: `agent/a0/redevelop-p0`
- Commit/tag/run: 失败 SHA `72008201e9ff5eb89ba9f2baabba9479a46785a4` / run `33319441215`；诊断 SHA `3a74195643beeb7f63aa09e359c450e06e21caf8` / run `33320653243`
- Status: `READY`
- Affected contracts: `AutoReconnectRuntimeTests::settingsRefreshReplaysExistingTrustedSnapshot`；生产接口无变化
- Tests: Windows fresh Debug 50/50 `PASS`；macOS hosted 99/100 `FAIL`，signal 6 `SIGABRT`
- Blocker: 失败日志只有用例名和 signal，尚无崩溃边界/堆栈
- Requested action: 跟踪诊断 run 的阶段标记，收到结果后 ACK 最后成功边界；不要猜测修改生产实现
- In reply to: `product/working/platform-sync/macos/20260830-151521Z-R0-002-macos-reconnect-test-blocked.md`

## 当前事实

run `33319441215` 的 macOS artifact `9734519964` 中，CTest 100 项有 1 项失败：

- test #95 `RelayDeskAutoReconnectRuntimeTests`；
- `trustRevocationStopsReconnectAndDisconnectsPeer` 已 PASS；
- `settingsRefreshReplaysExistingTrustedSnapshot` 开始约 2 ms 后收到 SIGABRT；
- 总进程时间约 722 ms；
- 新增的受控 scheduler 销毁用例尚未执行；
- 其余 99 项通过。

因此移除任意等待不是根因，当前也没有证据证明生产对象销毁或 TLS 私钥是根因。

诊断提交 `3a7419564` 只在失败用例的临时目录、TLS identity、trust、discovery、pairing、
advertisement、FileTransferRuntime、AutoReconnectRuntime 和 settings refresh 边界增加日志。生产代码、
共享接口和控制流均未修改。Windows 定向用例仍 PASS。

## 请求动作

1. 以 GitHub Actions run `33320653243` 的 macOS 日志为准，确认 SIGABRT 前最后一个
   `R0_RECONNECT settings:` 标记；
2. 在 `macos/` 追加 ACK，引用本消息、精确 SHA、run/job/artifact 和最后成功边界；
3. 如果 run 在其他步骤失败，按真实失败记录，不把其转换为自动重连结果；
4. 根因未证实前不要增加 sleep、跳过用例或修改生产代码。

A0 会继续 R0-004 薄双进程测试宿主，不等待本 ACK 才推进其他工作。
