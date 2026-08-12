# 13 许可证与分发合规

> 本文是工程实施清单，不是法律意见。正式商业分发前应由熟悉开源许可证的专业人员复核。

## 1. 上游许可

Deskflow 官方仓库标示：

```text
GPL-2.0-only WITH OpenSSL exception
```

仓库通过 REUSE 对不同文件声明许可证；并非每个支持文件都一定是同一许可证。代理必须保留：

- `LICENSE`；
- `LICENSES/`；
- `REUSE.toml`；
- 原文件 SPDX header；
- 上游版权；
- OpenSSL exception；
- 第三方 notices。

## 2. 对本项目的影响

当修改并分发基于 Deskflow 的可执行程序时，不能把它简单当作闭源专有程序发布。工程默认路线：

- 产品源码按相应 GPL 义务提供；
- 接收 binary 的用户能获得对应版本 source；
- build scripts、修改和必要安装信息一并可用；
- 不用 EULA 剥夺 GPL 赋予的权利；
- 可以收费，但不能隐瞒源码义务。

公司内部不对外分发与向客户/公众分发的义务场景可能不同；由法务最终判断。

## 3. 新增文件

每个新 C++/CMake/脚本文件使用与其所在模块相容的 SPDX header。默认对链接入主程序的新产品代码采用：

```text
SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
```

这是 v1.26.0 上游 REUSE 使用的 exception 标识；copyright 行仍须按新增文件实际作者填写。A1 在首个新增源码 PR 中再次对照真实仓库。

文档/测试向量可采用与仓库策略一致的许可证；必须写入 REUSE，不留“无许可证”文件。

## 4. 品牌与署名

- RelayDesk 是临时代号，不代表已做商标检索。
- 不把产品伪装为 Deskflow 官方发行。
- About 明确：
  - based on Deskflow；
  - upstream project；
  - modifications；
  - license；
  - source location。
- 保留兼容性说明时准确使用名称。
- 正式名称确定前不要申请固定 bundle/package ID 到公开渠道。

## 5. Source package

每个 release binary 对应 source package应包含：

- 精确 source；
- submodule/dependency lock；
- CMake/构建脚本；
- patches；
- packaging scripts；
- translation；
- generated file 的可再生方式；
- license/notice；
- release tag/commit。

提供 Git 仓库链接时，必须保证对应 tag 长期可访问；同时生成 source tarball 更稳妥。

## 6. 第三方依赖

新增依赖前检查：

- 许可证与 GPL 兼容；
- 是否必要；
- 是否已由 Qt/标准库/上游提供；
- source/binary 分发要求；
- Windows/macOS 打包；
- 安全维护；
- 是否引入网络服务条款。

P0 优先不新增大型依赖。CBOR、TLS、网络、hash 使用 Qt/OpenSSL/上游。

维护：

```text
THIRD_PARTY_NOTICES
dependency name/version/source/license/purpose
```

## 7. 自动检查

CI 建议：

- REUSE lint；
- SPDX header；
- dependency license report；
- source archive completeness；
- no deleted upstream license；
- package contains notices；
- source tag matches binary version。

## 8. Release checklist

```text
[ ] 上游 tag/commit 写明
[ ] 产品修改 tag/commit 写明
[ ] LICENSE/REUSE 完整
[ ] 新文件有 SPDX
[ ] 第三方依赖许可证确认
[ ] source package 可构建
[ ] binary 用户可获得 source
[ ] About/网站不冒充官方
[ ] 正式产品名完成商标/法务检查
[ ] 商业分发条款经专业复核
```

## 9. 禁止事项

- 删除 GPL 提示；
- 只给“部分核心源码”；
- 将 source 放在短期不可访问位置；
- 对修改文件谎称未修改；
- 混用不兼容依赖；
- 在没有法务结论时宣称“完全可以闭源”；
- 把本开发包当作法律批准。
