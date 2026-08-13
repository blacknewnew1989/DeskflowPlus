/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "relaydesk/device/DeviceId.h"
#include "relaydesk/filetransport/FileTlsTransport.h"

#include <QHostAddress>
#include <QObject>

#include <memory>

namespace deskflow::relaydesk {

class DiscoveryRegistry;
class TrustedDeviceStore;

struct FileTransferRuntimeOptions
{
  QHostAddress listenAddress = QHostAddress::Any;
  quint16 listenPort = 0;
  FileTlsSettings tlsSettings;
};

enum class FileTransferRuntimeError
{
  WrongThread,
  ListenerFailed,
};

// Owns the independent RDFT listener and the application-level dependencies
// that later transfer slices compose. No Deskflow input socket is shared.
class FileTransferRuntime final : public QObject
{
  Q_OBJECT

public:
  FileTransferRuntime(
      DeviceId localDeviceId, const TrustedDeviceStore &trustedDevices, DiscoveryRegistry &discoveryRegistry,
      QString combinedPemPath, FileTransferRuntimeOptions options = {}, QObject *parent = nullptr
  );
  ~FileTransferRuntime() override;

  Q_DISABLE_COPY_MOVE(FileTransferRuntime)

  [[nodiscard]] bool start(QString *diagnostic = nullptr);
  void stop();
  [[nodiscard]] bool isRunning() const;
  [[nodiscard]] quint16 listeningPort() const;

Q_SIGNALS:
  void started(quint16 port);
  void stopped();
  void errorOccurred(
      deskflow::relaydesk::FileTransferRuntimeError error, deskflow::relaydesk::FileTlsError transportError,
      QString diagnostic
  );

private:
  [[nodiscard]] bool onOwningThread(QString *diagnostic);

  DeviceId m_localDeviceId;
  const TrustedDeviceStore &m_trustedDevices;
  DiscoveryRegistry &m_discoveryRegistry;
  QString m_combinedPemPath;
  FileTransferRuntimeOptions m_options;
  std::unique_ptr<FileTlsListener> m_listener;
};

} // namespace deskflow::relaydesk

Q_DECLARE_METATYPE(deskflow::relaydesk::FileTransferRuntimeError)
