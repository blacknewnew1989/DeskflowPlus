# SPDX-FileCopyrightText: (C) 2026 RelayDesk contributors
# SPDX-License-Identifier: MIT

# Source packages must not recurse into in-tree build output or local tooling.
# In particular, Ninja writes build/.ninja_log while CPack is collecting the
# source tree, which makes copying that file racy on Windows.
set(CPACK_SOURCE_IGNORE_FILES
  "/\\.git/"
  "/build/"
  "/dist/"
  "/\\.tools/"
  "/\\.cache/"
  "/vcpkg/"
  "/vcpkg_installed/"
  "/tmp/"
)
