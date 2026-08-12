# 17 上游同步 Playbook

## 1. 固定 remote

```text
origin   = 用户当前已连接 GitHub 仓库
upstream = https://github.com/deskflow/deskflow.git
```

不要求 Fork，不重命名 `origin`。

## 2. 初始自动配置

A0 自行执行：

```bash
git remote add upstream https://github.com/deskflow/deskflow.git 2>/dev/null ||   git remote set-url upstream https://github.com/deskflow/deskflow.git

git fetch upstream --tags --prune
git switch -c product/relaydesk-v1 v1.26.0
```

若远程产品分支已存在：

```bash
git fetch origin product/relaydesk-v1
git switch --track origin/product/relaydesk-v1
```

## 3. 分支

```text
product/relaydesk-v1
chore/upstream-vX.Y.Z
agent/*
release/*
```

## 4. 同步步骤

1. 选择明确 tag；
2. `git fetch upstream --tags --prune`；
3. 创建 `chore/upstream-vX.Y.Z`；
4. 合并/变基上游；
5. 运行上游与产品测试；
6. 更新 patch inventory；
7. 创建独立提交；
8. 推送代理分支；
9. A0 合并到产品分支；
10. 推送产品分支并触发双平台构建。

不要求人工 PR 审批。不得混入无关产品功能。

## 5. 冲突原则

- 品牌集中；
- 产品模块独立；
- 不全仓重命名；
- 不格式化无关上游文件；
- 共享 adapter 保持薄；
- 冲突最小解决并记录。

## 6. 安全修复

必要修复可优先 cherry-pick。修复后自动构建和推送，不等待用户放行。

## 7. 失败

- 不强推；
- 保留旧阶段 tag；
- 自动记录首个冲突和构建错误；
- 可回退到上一阶段；
- 不伪造已同步。
