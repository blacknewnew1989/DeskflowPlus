/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

class ServerConfig;

namespace deskflow::relaydesk {
struct DeviceSnapshot;
}

namespace deskflow::gui {

enum class RelayDeskInputLayoutResult
{
  Added,
  AlreadyPresent,
  NotTrusted,
  InputUnsupported,
  InvalidScreenName,
  ExternalConfigActive,
  LayoutFull,
};

[[nodiscard]] RelayDeskInputLayoutResult syncRelayDeskInputScreen(
    ServerConfig &config, const relaydesk::DeviceSnapshot &peer
);

} // namespace deskflow::gui
