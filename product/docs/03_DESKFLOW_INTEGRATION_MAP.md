# 03 Deskflow 集成映射

> 本文是基于 v1.26.0 的预期映射。A1 必须在真实源码中核查，并在 `product/working/integration-map.actual.md` 记录实际 target、类和文件。源码是最终事实。

## 1. 上游主要区域

预期可见：

```text
src/apps/
├── deskflow-cli/
├── deskflow-core/
├── deskflow-daemon/       # 以真实 tag 为准
└── deskflow-gui/

src/lib/
├── client/
├── server/
├── net/
├── gui/
├── platform/
├── common/
└── deskflow/
```

平台文件通常按以下前缀组织：

- Windows：`MSWindows*`
- macOS：`OSX*`，包含 `.cpp`、`.m`、`.mm`

A1 要通过搜索真实类名回答：

- 输入 Server/Client 如何创建；
- GUI 如何启动/停止 core；
- TLS 证书与指纹如何存储；
- Settings 与日志路径；
- Clipboard 平台抽象；
- 网络 reactor/event loop；
- 测试 target 与 fixtures；
- Windows/macOS 打包入口。

## 2. 集成原则

### 保留上游

- 输入捕获与注入；
- 屏幕边界切换；
- 键盘映射基础；
- Server/Client 网络协议；
- 文本/图片剪贴板；
- 热键；
- 日志和设置框架；
- 平台权限基础；
- 打包/CI 基础。

### 新增产品层

建议真实源码核查后增加类似区域：

```text
src/lib/relaydesk/
├── device/
├── discovery/
├── pairing/
├── trust/
├── transfer/
└── app/

src/lib/gui/
└── relaydesk/             # 或遵循上游 GUI 模块实际结构
```

不要仅为品牌把所有命名空间从 `deskflow` 批量替换为 `relaydesk`。优先：

- 保留上游核心命名；
- 新增产品命名空间；
- UI/安装包显示名通过配置；
- 以后同步上游时减少冲突。

## 3. 产品身份集中化

需要集中处理：

- displayName；
- executable/package display；
- bundleId；
- Windows AppUserModelID；
- company/organization；
- website；
- protocol scheme（未来）；
- default receive folder；
- icon resources；
- translation context；
- update feed（P0 禁用）。

不得对 `Deskflow` 文本全仓盲替换，因为其中包含：

- 上游版权；
- 协议兼容说明；
- 类/target；
- 文档链接；
- 许可证；
- 配置兼容键。

## 4. 文件传输接入点

### GUI

新增：

- Devices 页面/模型；
- Pairing dialog；
- Transfer Center；
- Incoming Offer dialog；
- Drop target；
- Settings/File Transfer。

### Application lifecycle

需要在 GUI/core 生命周期中：

- 初始化 DeviceIdentity；
- 启动 DiscoveryService；
- 启动 FileTransferService；
- 订阅网络变化、睡眠/唤醒；
- 退出时有序暂停/持久化；
- 不改变 input core 启动失败处理。

### TLS

优先复用上游已经统一的 TLS utility/certificate 能力，但不能假定其接口支持对等文件连接。A1/A2 先核查：

- 是否可复用证书；
- 私钥格式和路径；
- fingerprint API；
- `QSslSocket` 使用情况；
- pinning 逻辑；
- 是否与 GUI/core 进程边界冲突。

若复用会造成强耦合，可复用相同设备身份文件、在文件模块封装独立 TLS session；不要复制一套不一致的证书。

### Settings

产品设置需版本化，避免污染上游键值：

```text
relaydesk/device/*
relaydesk/discovery/*
relaydesk/pairing/*
relaydesk/transfers/*
relaydesk/ui/*
```

迁移代码必须幂等。

## 5. CMake 策略

A1 核查实际 target 后：

- 新模块单独 library target；
- 协议/PathPolicy 尽量只依赖 Qt Core；
- 网络依赖 Qt Network；
- GUI 依赖上层 service 接口；
- tests 与模块同级；
- 平台源通过 `WIN32`/`APPLE` 条件加入；
- 遵循上游 warning、sanitizer、translation、install 规则。

示意，不可未经核查直接照抄：

```cmake
add_library(relaydesk-transfer STATIC ...)
target_link_libraries(relaydesk-transfer
    PUBLIC Qt6::Core
    PRIVATE Qt6::Network
)
target_link_libraries(actual-gui-target PRIVATE relaydesk-transfer)
```

## 6. 上游同步边界

提交类型分开：

1. `chore(upstream): merge deskflow ...`
2. `feat(relaydesk): ...`
3. `fix(platform): ...`

不要在一次提交混合上游更新和产品功能。

维护一份 patch inventory：

| Patch | 上游可贡献 | 产品专属 | 冲突风险 |
|---|---:|---:|---:|
| 通用崩溃/键鼠修复 | 是 | 否 | 低 |
| 文件传输 | 可能部分 | 是 | 中 |
| 品牌/UI | 否 | 是 | 高 |
| 平台权限通用修复 | 是 | 部分 | 中 |

## 7. A1 实际核查清单

```text
[ ] git tag/commit
[ ] license/REUSE
[ ] CMake minimum、C++ standard、Qt minimum
[ ] apps target
[ ] lib target
[ ] GUI entry
[ ] core entry
[ ] client/server startup
[ ] TLS certificate classes
[ ] trust/fingerprint dialogs
[ ] settings storage
[ ] logging
[ ] screen layout model
[ ] clipboard abstraction
[ ] network change handling
[ ] sleep/wake handling
[ ] Windows platform files
[ ] macOS platform files
[ ] test framework
[ ] CI workflows
[ ] Windows packaging
[ ] macOS bundle/DMG
```

## 8. 禁止的捷径

- 在上游没有实际构建前写一套新的跨平台输入系统；
- 用 C#/.NET 重写整个 GUI 与核心；
- 在键鼠 socket 里塞文件块；
- 在 UI 中直接开启 TCP server；
- 通过硬编码 IP 替代发现/配对；
- 因打包麻烦删除 daemon 或权限处理；
- 为减少冲突把产品代码放在仓库外且无法构建。
