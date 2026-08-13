/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "RelayDeskInputLayout.h"

#include "ServerConfig.h"
#include "gui/validators/ComputerNameValidator.h"
#include "relaydesk/device/DeviceSnapshot.h"

namespace deskflow::gui {

RelayDeskInputLayoutResult syncRelayDeskInputScreen(
    ServerConfig &config, const relaydesk::DeviceSnapshot &peer
)
{
  if (!peer.trusted) {
    return RelayDeskInputLayoutResult::NotTrusted;
  }
  if (!peer.capabilities.input) {
    return RelayDeskInputLayoutResult::InputUnsupported;
  }
  if (config.useExternalConfig()) {
    return RelayDeskInputLayoutResult::ExternalConfigActive;
  }

  const auto screenName = peer.displayName;
  const validators::ComputerNameValidator validator(QString{});
  if (screenName.isEmpty() || screenName.contains(QLatin1Char(' ')) || !validator.validate(screenName)) {
    return RelayDeskInputLayoutResult::InvalidScreenName;
  }
  if (config.screenExists(screenName)) {
    return RelayDeskInputLayoutResult::AlreadyPresent;
  }
  if (config.isFull()) {
    return RelayDeskInputLayoutResult::LayoutFull;
  }

  config.addClient(screenName);
  return config.screenExists(screenName) ? RelayDeskInputLayoutResult::Added
                                         : RelayDeskInputLayoutResult::LayoutFull;
}

} // namespace deskflow::gui
