/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/device/DeviceInfo.h"
#include "relaydesk/discovery/DiscoveryRegistry.h"
#include "relaydesk/discovery/DiscoveryService.h"

#include <QObject>

#include <chrono>

namespace deskflow::relaydesk::model {
class DeviceHomeModel;
}

namespace deskflow::relaydesk {

struct DeviceDiscoveryRuntimeOptions
{
  DiscoveryServiceSettings serviceSettings;
  std::chrono::milliseconds registryTtl = kDefaultDiscoveryTtl;
  DiscoveryService::InterfaceProvider interfaceProvider;
  DiscoveryService::DatagramSender datagramSender;
};

class DeviceDiscoveryRuntime final : public QObject
{
  Q_OBJECT

public:
  explicit DeviceDiscoveryRuntime(
      DeviceInfo localDevice, model::DeviceHomeModel &deviceModel, DeviceDiscoveryRuntimeOptions options = {},
      QObject *parent = nullptr
  );
  ~DeviceDiscoveryRuntime() override;

  Q_DISABLE_COPY_MOVE(DeviceDiscoveryRuntime)

  [[nodiscard]] bool start(QString *diagnostic = nullptr);
  void stop();
  [[nodiscard]] bool setFileEndpoint(
      quint16 port, bool folderV1, bool resumeV1, QString *diagnostic = nullptr
  );
  [[nodiscard]] bool isRunning() const;
  [[nodiscard]] DiscoveryService &service() const;
  [[nodiscard]] DiscoveryRegistry &registry() const;

Q_SIGNALS:
  void errorOccurred(DiscoveryServiceError error, QString diagnostic);

private:
  [[nodiscard]] bool onOwningThread(QString *diagnostic) const;

  model::DeviceHomeModel &m_deviceModel;
  DiscoveryRegistry *m_registry = nullptr;
  DiscoveryService *m_service = nullptr;
};

} // namespace deskflow::relaydesk
