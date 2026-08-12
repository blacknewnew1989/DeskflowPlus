# Third-party notices and source boundary

## Deskflow

本开发包面向 Deskflow v1.26.0（commit `760e3b9`）二次开发，但 ZIP 不直接捆绑 Deskflow 完整源码。Codex 使用 `scripts/autonomous-init-repo.py` 在当前已连接 GitHub 仓库中自动添加官方 upstream、获取固定 tag、创建产品分支并推送。

Deskflow 主体采用 `GPL-2.0-only WITH LicenseRef-OpenSSL-Exception`，仓库内不同文件可能有各自 REUSE/SPDX 声明。实际产品必须保留源码中的许可证和版权信息。

## Expected build dependencies

上游固定基线要求 C++20、CMake 3.24+、Qt 6.7+、OpenSSL 3+。Windows 使用 MSVC/vcpkg/WiX，macOS 使用 Xcode/AppleClang 与 Qt/macdeployqt/CPack。Codex 负责自动安装或通过 GitHub Actions runner 构建，用户不负责准备开发环境。

## RelayDesk starter

`starter/` 是协议帧和路径策略起始代码，不是 Deskflow 官方模块。Codex 必须把它接入真实上游仓库、编译和测试，不能把 starter 本身当成已完成产品。
