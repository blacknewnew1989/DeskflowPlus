/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QMetaType>
#include <QtTypes>

namespace deskflow::relaydesk {

/** Facts published by discovery for the independent file-transfer endpoint. */
struct FileEndpointAnnouncement
{
  quint16 port = 0;
  bool fileV1 = false;
  bool folderV1 = false;
  bool resumeV1 = false;

  [[nodiscard]] static constexpr FileEndpointAnnouncement disabled() noexcept
  {
    return {};
  }

  [[nodiscard]] constexpr bool isDisabled() const noexcept
  {
    return port == 0 && !fileV1 && !folderV1 && !resumeV1;
  }

  [[nodiscard]] constexpr bool isValid() const noexcept
  {
    return isDisabled() || (port != 0 && fileV1);
  }

  bool operator==(const FileEndpointAnnouncement &) const = default;
};

} // namespace deskflow::relaydesk

Q_DECLARE_METATYPE(deskflow::relaydesk::FileEndpointAnnouncement)
