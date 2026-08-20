/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/DeviceDiscoveryRuntime.h"

#include "relaydesk/discovery/AddressCandidateProvider.h"
#include "relaydesk/model/DeviceHomeModel.h"

#include <QDateTime>
#include <QSet>
#include <QThread>

#include <utility>

namespace deskflow::relaydesk {

namespace {
void setDiagnostic(QString *diagnostic, const QString &message)
{
  if (diagnostic != nullptr) {
    *diagnostic = message;
  }
}
} // namespace

DeviceDiscoveryRuntime::DeviceDiscoveryRuntime(
    DeviceInfo localDevice, model::DeviceHomeModel &deviceModel, DeviceDiscoveryRuntimeOptions options,
    QObject *parent
)
    : QObject(parent), m_deviceModel(deviceModel), m_manualAddresses(std::move(options.manualAddresses)),
      m_manualProbePort(options.manualProbePort),
      m_registry(new DiscoveryRegistry(localDevice.deviceId, options.registryTtl, {}, this)),
      m_service(new DiscoveryService(
          localDevice, options.serviceSettings, std::move(options.interfaceProvider),
       std::move(options.datagramSender), this
      )),
      m_manualCandidates(new AddressCandidateProvider(std::move(options.manualHostResolver), this))
{
  m_deviceModel.setLocalDevice({
      .id = localDevice.deviceId,
      .displayName = localDevice.displayName,
      .platform = localDevice.platform,
      .architecture = localDevice.architecture,
      .presence = DevicePresence::Online,
      .trusted = true,
      .capabilities = localDevice.capabilities,
      .pinnedFingerprint = localDevice.certificateFingerprintSha256,
      .lastSeenUtc = QDateTime::currentDateTimeUtc(),
  });

  connect(
      m_service, &DiscoveryService::advertisementReceived, m_registry,
      &DiscoveryRegistry::observeAdvertisement
  );
  connect(
      m_registry, &DiscoveryRegistry::deviceAdded, &m_deviceModel,
      &model::DeviceHomeModel::upsertRemoteDevice
  );
  connect(
      m_registry, &DiscoveryRegistry::deviceChanged, &m_deviceModel,
      &model::DeviceHomeModel::upsertRemoteDevice
  );
  connect(m_service, &DiscoveryService::errorOccurred, this, &DeviceDiscoveryRuntime::errorOccurred);
  connect(
      m_manualCandidates, &AddressCandidateProvider::candidatesResolved, this,
      [this](const AddressCandidateResult &result) {
        if (!isRunning()) {
          return;
        }
        const auto probePort = m_manualProbePort == 0 ? m_service->destinationPort() : m_manualProbePort;
        QSet<QString> manualIpv4Origins;
        for (const auto &candidate : result.candidates) {
          if (candidate.source == AddressCandidateSource::Manual &&
              candidate.address.protocol() == QAbstractSocket::IPv4Protocol) {
            manualIpv4Origins.insert(candidate.originHost);
          }
        }
        for (const auto &candidate : result.candidates) {
          if (candidate.source == AddressCandidateSource::Manual) {
            if (candidate.address.protocol() != QAbstractSocket::IPv4Protocol &&
                manualIpv4Origins.contains(candidate.originHost)) {
              continue;
            }
            QString diagnostic;
            if (!m_service->probePeer(candidate.address, probePort, &diagnostic)) {
              Q_EMIT errorOccurred(
                  DiscoveryServiceError::SendFailed,
                  QStringLiteral("Manual discovery probe for %1 failed: %2")
                      .arg(candidate.originHost.isEmpty() ? candidate.address.toString() : candidate.originHost, diagnostic)
              );
            }
          }
        }
      }
  );
}

DeviceDiscoveryRuntime::~DeviceDiscoveryRuntime()
{
  stop();
}

bool DeviceDiscoveryRuntime::start(QString *diagnostic)
{
  if (diagnostic != nullptr) {
    diagnostic->clear();
  }
  if (!onOwningThread(diagnostic)) {
    return false;
  }
  const auto started = m_service->start(diagnostic);
  if (started) {
    refreshManualDiscovery();
  }
  return started;
}

void DeviceDiscoveryRuntime::stop()
{
  if (QThread::currentThread() == thread()) {
    m_service->stop();
  }
}

bool DeviceDiscoveryRuntime::setFileEndpoint(FileEndpointAnnouncement announcement, QString *diagnostic)
{
  if (!onOwningThread(diagnostic)) {
    return false;
  }
  return m_service->setFileEndpoint(announcement, diagnostic);
}

void DeviceDiscoveryRuntime::setManualAddresses(QList<ManualAddress> addresses)
{
  m_manualAddresses = std::move(addresses);
  if (!isRunning()) {
    if (!m_manualAddresses.isEmpty()) {
      static_cast<void>(start());
    }
    return;
  }
  refreshManualDiscovery();
}

bool DeviceDiscoveryRuntime::isRunning() const
{
  return m_service->isRunning();
}

DiscoveryService &DeviceDiscoveryRuntime::service() const
{
  return *m_service;
}

DiscoveryRegistry &DeviceDiscoveryRuntime::registry() const
{
  return *m_registry;
}

bool DeviceDiscoveryRuntime::onOwningThread(QString *diagnostic) const
{
  if (QThread::currentThread() == thread() && m_deviceModel.thread() == thread() &&
      m_registry->thread() == thread() && m_service->thread() == thread()) {
    return true;
  }
  setDiagnostic(diagnostic, QStringLiteral("Discovery runtime must start on the device model's owning thread"));
  return false;
}

void DeviceDiscoveryRuntime::refreshManualDiscovery()
{
  if (!isRunning()) {
    return;
  }
  m_manualCandidates->resolveCandidates({
      .settings = {.manualAddresses = m_manualAddresses},
      .inputPort = kDefaultManualInputPort,
      .filePort = kDefaultManualFilePort,
  });
}

} // namespace deskflow::relaydesk
