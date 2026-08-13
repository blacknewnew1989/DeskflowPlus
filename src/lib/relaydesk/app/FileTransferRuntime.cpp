/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/FileTransferRuntime.h"

#include "relaydesk/discovery/DiscoveryRegistry.h"
#include "relaydesk/trust/TrustedDeviceStore.h"

#include <QThread>

#include <utility>

namespace deskflow::relaydesk {
namespace {
void setDiagnostic(QString *output, const QString &diagnostic)
{
  if (output != nullptr) {
    *output = diagnostic;
  }
}
} // namespace

FileTransferRuntime::FileTransferRuntime(
    DeviceId localDeviceId, const TrustedDeviceStore &trustedDevices, DiscoveryRegistry &discoveryRegistry,
    QString combinedPemPath, FileTransferRuntimeOptions options, QObject *parent
)
    : QObject(parent), m_localDeviceId(std::move(localDeviceId)), m_trustedDevices(trustedDevices),
      m_discoveryRegistry(discoveryRegistry), m_combinedPemPath(std::move(combinedPemPath)),
      m_options(std::move(options))
{
}

FileTransferRuntime::~FileTransferRuntime()
{
  stop();
}

bool FileTransferRuntime::start(QString *diagnostic)
{
  if (diagnostic != nullptr) {
    diagnostic->clear();
  }
  if (!onOwningThread(diagnostic)) {
    return false;
  }
  if (isRunning()) {
    return true;
  }

  auto listener = std::make_unique<FileTlsListener>(
      m_localDeviceId, &m_trustedDevices, m_combinedPemPath, m_options.tlsSettings
  );
  connect(listener.get(), &FileTlsListener::failed, this, [this](FileTlsError error, const QString &message) {
    Q_EMIT errorOccurred(FileTransferRuntimeError::ListenerFailed, error, message);
  });

  QString listenDiagnostic;
  const auto result = listener->listen(m_options.listenAddress, m_options.listenPort, &listenDiagnostic);
  if (result != FileTlsError::None) {
    setDiagnostic(diagnostic, listenDiagnostic);
    Q_EMIT errorOccurred(FileTransferRuntimeError::ListenerFailed, result, listenDiagnostic);
    return false;
  }

  m_listener = std::move(listener);
  Q_EMIT started(m_listener->serverPort());
  return true;
}

void FileTransferRuntime::stop()
{
  if (QThread::currentThread() != thread() || m_listener == nullptr) {
    return;
  }
  m_listener->close();
  m_listener.reset();
  Q_EMIT stopped();
}

bool FileTransferRuntime::isRunning() const
{
  return m_listener != nullptr && m_listener->isListening();
}

quint16 FileTransferRuntime::listeningPort() const
{
  return m_listener == nullptr ? 0 : m_listener->serverPort();
}

bool FileTransferRuntime::onOwningThread(QString *diagnostic)
{
  if (QThread::currentThread() == thread() && m_discoveryRegistry.thread() == thread()) {
    return true;
  }
  const auto message = QStringLiteral("File transfer runtime must start on the discovery registry owning thread");
  setDiagnostic(diagnostic, message);
  Q_EMIT errorOccurred(FileTransferRuntimeError::WrongThread, FileTlsError::None, message);
  return false;
}

} // namespace deskflow::relaydesk
