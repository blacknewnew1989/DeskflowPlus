/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/platform/IPlatformFileSafety.h"

namespace deskflow::relaydesk {

class WindowsFileSafety final : public IPlatformFileSafety
{
public:
  [[nodiscard]] FileSafetyResult verifyReceiveRoot(const VerifyReceiveRootRequest &request) const override;
  [[nodiscard]] FileSafetyResult
  verifyNoLinkTraversal(const VerifyNoLinkTraversalRequest &request) const override;
  [[nodiscard]] FileSafetyResult commitStagedFile(const CommitStagedFileRequest &request) override;
};

} // namespace deskflow::relaydesk
