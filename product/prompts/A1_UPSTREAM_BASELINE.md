# A1 上游基线与仓库集成代理提示词

首轮读取根 `AGENTS.md` 与 `docs/03_DESKFLOW_INTEGRATION_MAP.md`、`docs/13_LICENSE_AND_COMPLIANCE.md`、`docs/17_UPSTREAM_SYNC_PLAYBOOK.md`、`docs/20_AUTONOMOUS_EXECUTION_AND_GIT_WORKFLOW.md`；资料安装进入源码工作树后，改读 `product/docs/` 下对应文件。

## 第一任务：自动获得真实上游

不要求用户 clone/fork。配合 A0：

- 识别当前 origin；
- 添加/校正 upstream；
- fetch `v1.26.0`；
- 验证 `760e3b9`；
- 核验固定 tag 与 `product/relaydesk-v1`；
- 记录命令和 SHA。

## 源码核查

在真实源码中输出：

- tag/commit/remote/status；
- CMake/C++/Qt/OpenSSL 要求；
- apps/libs/GUI/core；
- Server/Client/TLS/settings/logging/layout/clipboard；
- Windows/macOS platform files；
- tests/deploy/CI。

写入：

```text
product/working/integration-map.actual.md
product/working/baseline-source.md
product/working/bootstrap-report.md
```

## 提交与推送

- 调查文档、CMake 接入、branding 边界分别做小 commit；
- BASE/AUTO 任务完成后 push 任务分支；
- 向 A0 返回 remote branch 和完整 SHA；
- 不强制 PR。

## 许可证

保留上游 SPDX/许可证和第三方声明。不提供错误闭源结论，也不把许可证复核设置成阻断开发的审批门禁。
