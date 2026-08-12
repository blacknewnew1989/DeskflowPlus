# Agent Matrix

## 模型与角色

| Agent | 角色 | 建议模型等级 | 使用原则 |
|---|---|---|---|
| A0 | 总控、架构、集成、最终验收 | Sol 高 | 持续保留，处理跨模块决策和合并 |
| A1 | 上游基线、源码地图、许可证、同步 | Sol 中 | 调查和小改为主 |
| A2 | 设备、发现、配对、信任、重连 | Sol 高（协议阶段），后降中 | 安全状态机需高推理 |
| A3 | Qt GUI、i18n、交互 | Terra/默认高效档；复杂状态用 Sol 中 | 避免把高级模型浪费在纯布局 |
| A4 | Windows 平台、打包、Shell | Sol 中 | Win32/打包故障时临时升高 |
| A5 | macOS 平台、权限、签名、Finder | Sol 中 | Objective-C++/权限故障时临时升高 |
| A6 | 文件协议、I/O、续传、性能 | Sol 高（核心阶段），稳定后中 | 核心风险最高 |
| A7 | 自动化测试、E2E、CI、发布 | Terra/默认；复杂故障 Sol 中 | 批量测试可用经济模型 |

最高不超过 Sol 高。不要所有子任务默认使用最高档。

## 并行原则

Phase -1/0：

```text
A0 仓库识别/upstream/分支/push
A7 GitHub Actions/artifacts
```

Phase 0：

```text
A1 源码核查
A4 Windows 构建
A5 macOS 构建
A7 测试记录模板
```

Phase 1：

```text
A2 Device/Discovery/Pairing
A3 Devices/Pairing UI
A1 Branding/Upstream boundary
A4/A5 Platform permission adapters
```

Phase 2：

```text
A6 Protocol/Transfer core
A2 TLS/Trust integration
A3 Transfer UI model scaffold
A7 Codec/Path tests
```

Phase 3：

```text
A6 Resume/Conflict/History
A3 Transfer Center/Drop
A4/A5 Platform filesystem edge cases
A7 E2E/chaos/performance
```

同一公共接口不得由两个代理并行修改。A0 先指定 owner，其他代理只依赖已提交接口。

## 代理输出格式

每个代理完成一轮后向 A0 返回：

```text
Task IDs:
Branch/worktree:
Commits:
Remote branch:
Pushed SHA:
Actions run/artifact:
Files changed:
Commands run:
Tests:
Platform/E2E validation:
Not run:
Risks:
Interface changes:
Recommended next:
```

不得只说“已完成”。


## 全代理 Git 约束

- 小功能完成立即 commit；
- 任务完成必须 push；
- A0 合并并 push 集成分支；
- Phase 完成必须有汇总 commit、tag、双平台 Actions artifacts；
- 用户不参与中间操作；
- 不强制 PR、审批或 required gate。
