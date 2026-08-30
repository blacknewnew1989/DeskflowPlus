/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "net/SecureUtils.h"
#include "relaydesk/app/DeviceDiscoveryRuntime.h"
#include "relaydesk/app/FileTransferRuntime.h"
#include "relaydesk/app/PairingTrustRuntime.h"
#include "relaydesk/device/DeviceIdentity.h"
#include "relaydesk/model/DeviceHomeModel.h"
#include "relaydesk/model/PairingWizardModel.h"
#include "relaydesk/trust/TlsIdentityAdapter.h"
#include "relaydesk/trust/TrustedDeviceStore.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>

using namespace deskflow::relaydesk;
using namespace deskflow::relaydesk::model;
using namespace ::relaydesk::transfer;

namespace {

enum class Role { Sender, Receiver };
enum class Scenario { Complete, PauseResume, Cancel };

constexpr quint64 kControlThresholdBytes = 1024U * 1024U;

QByteArray fileSha256(const QString &path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  QCryptographicHash hash(QCryptographicHash::Sha256);
  while (!file.atEnd()) hash.addData(file.read(1024 * 1024));
  return hash.result();
}

bool containsPartFile(const QString &root)
{
  QDirIterator files(root, QStringList{QStringLiteral("*.part")}, QDir::Files, QDirIterator::Subdirectories);
  return files.hasNext();
}

std::optional<qint64> partBytes(const QString &root)
{
  QDirIterator files(root, QStringList{QStringLiteral("*.part")}, QDir::Files, QDirIterator::Subdirectories);
  qint64 bytes = 0;
  bool found = false;
  while (files.hasNext()) {
    files.next();
    bytes += files.fileInfo().size();
    found = true;
  }
  return found ? std::optional<qint64>{bytes} : std::nullopt;
}

class Peer final : public QObject
{
public:
  Peer(Role role, Scenario scenario, QString root, quint16 discoveryPort, quint16 peerDiscoveryPort,
       QString sourcePath, QByteArray expectedSha256, QString resultPath, QObject *parent = nullptr)
      : QObject(parent),
        m_role(role),
        m_scenario(scenario),
        m_root(std::move(root)),
        m_discoveryPort(discoveryPort),
        m_peerDiscoveryPort(peerDiscoveryPort),
        m_sourcePath(std::move(sourcePath)),
        m_expectedSha256(std::move(expectedSha256)),
        m_resultPath(std::move(resultPath))
  {
  }

  bool start(QString *error)
  {
    if (!QDir().mkpath(m_root)) return failStart(error, QStringLiteral("could not create peer root"));

    m_settings = std::make_unique<QSettings>(
        QDir(m_root).filePath(QStringLiteral("settings.ini")), QSettings::IniFormat
    );
    DeviceIdentity identity(*m_settings);
    const auto deviceId = identity.loadOrCreate(error);
    if (!deviceId.has_value()) return false;
    m_settings->sync();

    m_pemPath = QDir(m_root).filePath(QStringLiteral("tls/identity.pem"));
    if (!QDir().mkpath(QFileInfo(m_pemPath).absolutePath())) {
      return failStart(error, QStringLiteral("could not create TLS directory"));
    }
    try {
      deskflow::generatePemSelfSignedCert(m_pemPath);
    } catch (const std::exception &exception) {
      return failStart(error, QString::fromUtf8(exception.what()));
    }
    const auto tlsIdentity = TlsIdentityAdapter::inspect(m_pemPath);
    if (!tlsIdentity.ok()) return failStart(error, tlsIdentity.diagnostic);

    m_local.emplace(DeviceInfo{
        .deviceId = *deviceId,
        .displayName = m_role == Role::Sender ? QStringLiteral("R0 Sender") : QStringLiteral("R0 Receiver"),
        .platform = QSysInfo::productType(),
        .architecture = QSysInfo::currentCpuArchitecture(),
        .appVersion = QStringLiteral("r0-two-process"),
        .inputPort = 24800,
        .capabilities = {.input = true, .clipboardText = true, .clipboardImage = true, .fileV1 = true},
        .certificateFingerprintSha256 = tlsIdentity.fingerprintSha256,
    });
    DeviceDiscoveryRuntimeOptions discoveryOptions;
    discoveryOptions.serviceSettings = {
        .port = m_discoveryPort,
        .announcementIntervalMs = 100,
        .broadcastsEnabled = false,
    };
    discoveryOptions.interfaceProvider = [] { return QList<DiscoveryInterface>{}; };
    m_discovery = std::make_unique<DeviceDiscoveryRuntime>(*m_local, m_devices, std::move(discoveryOptions), this);
    if (!m_discovery->start(error)) return false;

    PairingTrustRuntimeOptions pairingOptions;
    pairingOptions.sasGenerator = [] { return 123456U; };
    pairingOptions.endpointResolver = [this](const DeviceId &) {
      return std::optional(std::pair(QHostAddress(QHostAddress::LocalHost), m_peerDiscoveryPort));
    };
    m_pairing = std::make_unique<PairingTrustRuntime>(
        *m_local, QDir(m_root).filePath(QStringLiteral("state/trusted-devices.json")), *m_discovery, m_devices,
        m_pairingModel, std::move(pairingOptions), this
    );
    if (!m_pairing->isReady()) return failStart(error, m_pairing->loadResult().diagnostic);

    FileTransferRuntimeOptions transferOptions;
    transferOptions.listenAddress = QHostAddress::LocalHost;
    transferOptions.tlsSettings.maxQueuedWriteBytes = 2U * 1024U * 1024U;
    m_files = std::make_unique<FileTransferRuntime>(
        m_local->deviceId, m_pairing->trustedDevices(), *m_discovery, m_pemPath, std::move(transferOptions), this
    );
    if (!m_files->start(error)) return false;

    connect(
        &m_discovery->registry(), &DiscoveryRegistry::deviceAdded, this,
        [this](const DeviceSnapshot &snapshot) { observe(snapshot); }
    );
    connect(
        &m_discovery->registry(), &DiscoveryRegistry::deviceChanged, this,
        [this](const DeviceSnapshot &snapshot) { observe(snapshot); }
    );
    connect(m_pairing.get(), &IPairingService::pairingChanged, this, [this](const PairingSnapshot &snapshot) {
      pairingChanged(snapshot);
    });
    connect(m_pairing.get(), &IPairingService::operationFailed, this, [this](const PairingOperationResult &result) {
      finish(false, result.diagnostic);
    });
    connect(m_files.get(), &FileTransferRuntime::peerReady, this, [this](const DeviceId &peer, const auto &) {
      if (m_role != Role::Sender || peer != m_peerId || m_sendStarted) return;
      m_sendStarted = true;
      const auto started = m_files->send(m_peerId, {QUrl::fromLocalFile(m_sourcePath)}, {});
      if (!started.ok()) finish(false, started.diagnostic);
    });
    connect(m_files.get(), &FileTransferRuntime::peerDisconnected, this, [this](const DeviceId &peer) {
      if (m_role == Role::Receiver && peer == m_peerId) {
        m_senderDisconnected = true;
        finishReceiverIfComplete();
      } else if (m_role == Role::Sender && m_scenario == Scenario::Cancel && peer == m_peerId &&
                 m_senderObservedCancelled) {
        finish(true, {});
      }
    });
    connect(m_files.get(), &IFileTransferService::incomingOffer, this, [this](const IncomingOffer &offer) {
      if (m_role != Role::Receiver) return;
      const auto destination = QDir(m_root).filePath(QStringLiteral("receive"));
      if (!QDir().mkpath(destination)) {
        finish(false, QStringLiteral("could not create receive directory"));
        return;
      }
      m_files->accept(offer.offer.transferId, {.destinationRoot = destination});
    });
    connect(
        m_files.get(), &IFileTransferService::transferChanged, this,
        [this](const TransferSnapshot &snapshot) { transferChanged(snapshot); }
    );
    connect(m_files.get(), &FileTransferRuntime::errorOccurred, this, [this](auto, auto, const QString &diagnostic) {
      if (!m_finished) finish(false, diagnostic);
    });

    m_probeTimer.setInterval(100);
    connect(&m_probeTimer, &QTimer::timeout, this, [this] {
      if (m_peerSeen || m_finished) return;
      QString ignored;
      (void)m_discovery->service().probePeer(QHostAddress::LocalHost, m_peerDiscoveryPort, &ignored);
    });
    m_probeTimer.start();
    return true;
  }

private:
  bool failStart(QString *error, const QString &message)
  {
    if (error != nullptr) *error = message;
    return false;
  }

  void observe(const DeviceSnapshot &snapshot)
  {
    if (m_finished || snapshot.id == m_local->deviceId) return;
    m_peerSeen = true;
    m_peerId = snapshot.id;
    if (m_role == Role::Sender && !m_pairingStarted) {
      m_pairingStarted = true;
      const auto result = m_pairing->startPairing(m_peerId);
      if (!result.ok()) finish(false, result.diagnostic);
    }
  }

  void pairingChanged(const PairingSnapshot &snapshot)
  {
    if (m_finished) return;
    m_peerId = snapshot.peer.id;
    if (snapshot.state == PairingState::AwaitingUserComparison && !m_pairingConfirmationQueued) {
      m_pairingConfirmationQueued = true;
      QTimer::singleShot(0, this, [this, snapshot] {
        if (m_finished) return;
        const auto result = m_role == Role::Sender
                                ? m_pairing->confirmMatchingSas(snapshot.pairingSessionId)
                                : m_pairing->submitDisplayedSas(snapshot.pairingSessionId, QStringLiteral("123456"));
        if (!result.ok()) finish(false, result.diagnostic);
      });
      return;
    }
    if (snapshot.state == PairingState::Completed) {
      m_trusted = m_pairing->trustedDevices().find(m_peerId).has_value();
      if (!m_trusted) {
        finish(false, QStringLiteral("completed pairing was not persisted"));
      } else if (m_role == Role::Sender && !m_connectStarted) {
        m_connectStarted = true;
        QString diagnostic;
        if (!m_files->connectPeer(m_peerId, &diagnostic)) finish(false, diagnostic);
      }
    } else if (snapshot.state == PairingState::Failed || snapshot.state == PairingState::Rejected ||
               snapshot.state == PairingState::Expired) {
      finish(false, QStringLiteral("pairing did not complete"));
    }
  }

  void transferChanged(const TransferSnapshot &snapshot)
  {
    if (snapshot.state == TransferState::Failed || snapshot.state == TransferState::Interrupted) {
      finish(false, QStringLiteral("transfer did not complete"));
      return;
    }
    if (snapshot.direction == TransferDirection::Sending && m_role == Role::Sender) {
      if (m_scenario == Scenario::PauseResume) {
        if (snapshot.state == TransferState::Paused) m_senderObservedPaused = true;
        if (snapshot.state == TransferState::Completed) {
          finish(
              m_senderObservedPaused,
              m_senderObservedPaused ? QString{} : QStringLiteral("sender did not observe remote pause")
          );
        }
        return;
      }
      if (m_scenario == Scenario::Cancel) {
        if (snapshot.state == TransferState::Cancelled) m_senderObservedCancelled = true;
        return;
      }
      if (snapshot.state == TransferState::Completed) finish(true, {});
      return;
    }
    if (snapshot.direction != TransferDirection::Receiving || m_role != Role::Receiver) return;
    if (m_scenario == Scenario::PauseResume) {
      if (!m_receiverControlIssued && snapshot.state == TransferState::Transferring &&
          snapshot.progress.completedBytes >= kControlThresholdBytes) {
        m_receiverControlIssued = true;
        QTimer::singleShot(0, this, [this, transferId = snapshot.id] {
          if (!m_finished) m_files->pause(transferId);
        });
      }
      if (m_receiverControlIssued && snapshot.state == TransferState::Paused) scheduleReceiverResume(snapshot);
      if (snapshot.state == TransferState::Completed) {
        const auto received = QDir(m_root).filePath(QStringLiteral("receive/") + QFileInfo(m_sourcePath).fileName());
        const bool valid = fileSha256(received) == m_expectedSha256 &&
                           !containsPartFile(QDir(m_root).filePath(QStringLiteral("receive")));
        if (!valid || !m_pauseBytesStable) {
          finish(false, valid ? QStringLiteral("pause bytes were not stable")
                              : QStringLiteral("receiver completion integrity check failed"));
        } else {
          m_receiverTransferCompleted = true;
          finishReceiverIfComplete();
        }
      }
      return;
    }
    if (m_scenario == Scenario::Cancel) {
      if (!m_receiverControlIssued && snapshot.state == TransferState::Transferring &&
          snapshot.progress.completedBytes >= kControlThresholdBytes) {
        m_receiverControlIssued = true;
        QTimer::singleShot(0, this, [this, transferId = snapshot.id] {
          if (!m_finished) m_files->cancel(transferId, {.partialDisposition = PartialDisposition::Remove});
        });
      }
      if (snapshot.state != TransferState::Cancelled) return;
      const auto receiveRoot = QDir(m_root).filePath(QStringLiteral("receive"));
      const auto incomingRoot = QDir(receiveRoot).filePath(QStringLiteral(".incoming/%1").arg(snapshot.id.toString()));
      const auto resumePath = QDir(receiveRoot).filePath(
          QStringLiteral(".incoming/resume-active/%1.resume.cbor").arg(snapshot.id.toString())
      );
      m_cancelCleanupValid = !containsPartFile(receiveRoot) && !QFileInfo::exists(incomingRoot) &&
                             !QFileInfo::exists(resumePath);
      m_receiverCancelled = true;
      finish(m_cancelCleanupValid, m_cancelCleanupValid ? QString{} : QStringLiteral("cancel cleanup failed"));
      return;
    }
    if (snapshot.state == TransferState::Completed) {
      const auto received = QDir(m_root).filePath(QStringLiteral("receive/") + QFileInfo(m_sourcePath).fileName());
      const bool valid = fileSha256(received) == m_expectedSha256 &&
                         !containsPartFile(QDir(m_root).filePath(QStringLiteral("receive")));
      if (!valid) {
        finish(false, QStringLiteral("receiver completion integrity check failed"));
      } else if (m_scenario != Scenario::PauseResume || m_pauseBytesStable) {
        m_receiverTransferCompleted = true;
        finishReceiverIfComplete();
      } else {
        finish(false, QStringLiteral("pause bytes were not stable"));
      }
    }
  }

  void scheduleReceiverResume(const TransferSnapshot &snapshot)
  {
    if (m_pauseCheckScheduled) return;
    m_pauseCheckScheduled = true;
    const auto transferId = snapshot.id;
    const auto pausedBytes = snapshot.progress.completedBytes;
    const auto pausedPartBytes = partBytes(QDir(m_root).filePath(QStringLiteral("receive")));
    QTimer::singleShot(0, this, [this, transferId, pausedBytes, pausedPartBytes] {
      for (const auto &current : m_files->activeTransfers()) {
        if (current.id == transferId) {
          const auto currentPartBytes = partBytes(QDir(m_root).filePath(QStringLiteral("receive")));
          m_pauseBytesStable = current.state == TransferState::Paused && current.progress.completedBytes == pausedBytes &&
                               pausedPartBytes.has_value() && currentPartBytes == pausedPartBytes;
          if (!m_pauseBytesStable) {
            finish(false, QStringLiteral("receiver pause bytes or partial file advanced"));
          } else {
            m_files->resume(transferId);
          }
          return;
        }
      }
      finish(false, QStringLiteral("paused transfer disappeared"));
    });
  }

  void finish(bool passed, const QString &error)
  {
    if (m_finished || m_finishQueued) return;
    m_finishQueued = true;
    QTimer::singleShot(0, this, [this, passed, error] { finishNow(passed, error); });
  }

  void finishNow(bool passed, const QString &error)
  {
    m_finished = true;
    m_probeTimer.stop();
    QJsonObject result{
        {QStringLiteral("passed"), passed},
        {QStringLiteral("role"), m_role == Role::Sender ? QStringLiteral("sender") : QStringLiteral("receiver")},
        {QStringLiteral("error"), error},
        {QStringLiteral("deviceId"), m_local->deviceId.toString()},
        {QStringLiteral("fingerprint"), QString::fromLatin1(m_local->certificateFingerprintSha256.toHex())},
        {QStringLiteral("settingsFile"), m_settings == nullptr ? QString{} : m_settings->fileName()},
        {QStringLiteral("trusted"), m_trusted},
        {QStringLiteral("discovered"), m_peerSeen},
        {QStringLiteral("filePort"), static_cast<int>(m_files == nullptr ? 0 : m_files->listeningPort())},
        {QStringLiteral("pauseBytesStable"), m_pauseBytesStable},
        {QStringLiteral("cancelCleanupValid"), m_cancelCleanupValid},
        {QStringLiteral("cancelled"), m_role == Role::Sender ? m_senderObservedCancelled : m_receiverCancelled},
        {QStringLiteral("receiverControlled"), m_receiverControlIssued},
        {QStringLiteral("senderObservedPause"), m_senderObservedPaused},
    };
    QSaveFile output(m_resultPath);
    if (output.open(QIODevice::WriteOnly)) {
      output.write(QJsonDocument(result).toJson(QJsonDocument::Compact));
      output.commit();
    }
    if (m_files != nullptr) m_files->stop();
    if (m_discovery != nullptr) m_discovery->stop();
    QCoreApplication::exit(passed ? 0 : 1);
  }

  void finishReceiverIfComplete()
  {
    if (m_receiverTransferCompleted && m_senderDisconnected) finish(true, {});
  }

  Role m_role;
  Scenario m_scenario;
  QString m_root;
  quint16 m_discoveryPort;
  quint16 m_peerDiscoveryPort;
  QString m_sourcePath;
  QByteArray m_expectedSha256;
  QString m_resultPath;
  std::unique_ptr<QSettings> m_settings;
  QString m_pemPath;
  std::optional<DeviceInfo> m_local;
  DeviceHomeModel m_devices;
  PairingWizardModel m_pairingModel;
  std::unique_ptr<DeviceDiscoveryRuntime> m_discovery;
  std::unique_ptr<PairingTrustRuntime> m_pairing;
  std::unique_ptr<FileTransferRuntime> m_files;
  QTimer m_probeTimer;
  DeviceId m_peerId = DeviceId::generate();
  bool m_finished = false;
  bool m_finishQueued = false;
  bool m_peerSeen = false;
  bool m_pairingStarted = false;
  bool m_pairingConfirmationQueued = false;
  bool m_connectStarted = false;
  bool m_sendStarted = false;
  bool m_receiverTransferCompleted = false;
  bool m_senderDisconnected = false;
  bool m_trusted = false;
  bool m_pauseCheckScheduled = false;
  bool m_pauseBytesStable = false;
  bool m_receiverControlIssued = false;
  bool m_senderObservedPaused = false;
  bool m_senderObservedCancelled = false;
  bool m_receiverCancelled = false;
  bool m_cancelCleanupValid = false;
};

} // namespace

int main(int argc, char *argv[])
{
  QCoreApplication application(argc, argv);
  QCommandLineParser parser;
  QCommandLineOption roleOption(QStringLiteral("role"), QStringLiteral("sender or receiver"), QStringLiteral("role"));
  QCommandLineOption rootOption(QStringLiteral("root"), QStringLiteral("isolated peer root"), QStringLiteral("path"));
  QCommandLineOption portOption(
      QStringLiteral("discovery-port"), QStringLiteral("local discovery port"), QStringLiteral("port")
  );
  QCommandLineOption peerPortOption(
      QStringLiteral("peer-discovery-port"), QStringLiteral("peer discovery port"), QStringLiteral("port")
  );
  QCommandLineOption sourceOption(
      QStringLiteral("source"), QStringLiteral("sender source path"), QStringLiteral("path")
  );
  QCommandLineOption shaOption(
      QStringLiteral("expected-sha256"), QStringLiteral("expected receiver SHA-256"), QStringLiteral("hex")
  );
  QCommandLineOption resultOption(
      QStringLiteral("result"), QStringLiteral("terminal result JSON"), QStringLiteral("path")
  );
  QCommandLineOption scenarioOption(
      QStringLiteral("scenario"), QStringLiteral("complete, pause-resume, or cancel"), QStringLiteral("name")
  );
  parser.addOptions(
      {roleOption, scenarioOption, rootOption, portOption, peerPortOption, sourceOption, shaOption, resultOption}
  );
  parser.process(application);

  const auto roleText = parser.value(roleOption);
  const auto role = roleText == QStringLiteral("sender") ? Role::Sender
                  : roleText == QStringLiteral("receiver") ? Role::Receiver : Role::Sender;
  const auto scenarioText = parser.value(scenarioOption);
  const auto scenario = scenarioText == QStringLiteral("complete") ? Scenario::Complete
                      : scenarioText == QStringLiteral("pause-resume") ? Scenario::PauseResume
                      : scenarioText == QStringLiteral("cancel") ? Scenario::Cancel : Scenario::Complete;
  bool localPortOk = false;
  bool peerPortOk = false;
  const auto localPort = parser.value(portOption).toUShort(&localPortOk);
  const auto peerPort = parser.value(peerPortOption).toUShort(&peerPortOk);
  const auto expected = QByteArray::fromHex(parser.value(shaOption).toLatin1());
  if ((roleText != QStringLiteral("sender") && roleText != QStringLiteral("receiver")) ||
      (scenarioText != QStringLiteral("complete") && scenarioText != QStringLiteral("pause-resume") &&
       scenarioText != QStringLiteral("cancel")) || !localPortOk || !peerPortOk ||
      localPort == 0 || peerPort == 0 || parser.value(rootOption).isEmpty() || parser.value(resultOption).isEmpty() ||
      parser.value(sourceOption).isEmpty() || expected.size() != 32) {
    return 2;
  }
  Peer peer(
      role, scenario, parser.value(rootOption), localPort, peerPort, parser.value(sourceOption), expected,
      parser.value(resultOption)
  );
  QString error;
  if (!peer.start(&error)) {
    QSaveFile output(parser.value(resultOption));
    if (output.open(QIODevice::WriteOnly)) {
      output.write(QJsonDocument(QJsonObject{{QStringLiteral("passed"), false}, {QStringLiteral("error"), error}})
                       .toJson(QJsonDocument::Compact));
      output.commit();
    }
    qCritical().noquote() << error;
    return 3;
  }
  return application.exec();
}
