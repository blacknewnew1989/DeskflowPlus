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
#include "relaydesk/transfer/ResumeStore.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>

using namespace deskflow::relaydesk;
using namespace deskflow::relaydesk::model;
using namespace ::relaydesk::transfer;

namespace {

enum class Role { Sender, Receiver };
enum class Scenario { Complete, PauseResume, Cancel, FileTree, ListenerResume };

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

QJsonArray stateArray(const QList<int> &states)
{
  QJsonArray result;
  for (const auto state : states) result.append(state);
  return result;
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
       QStringList sourcePaths, QByteArray expectedSha256, QString expectedTreePath, QString resultPath,
       QObject *parent = nullptr)
      : QObject(parent),
        m_role(role),
        m_scenario(scenario),
        m_root(std::move(root)),
        m_discoveryPort(discoveryPort),
        m_peerDiscoveryPort(peerDiscoveryPort),
        m_sourcePaths(std::move(sourcePaths)),
        m_sourcePath(m_sourcePaths.first()),
        m_expectedSha256(std::move(expectedSha256)),
        m_expectedTreePath(std::move(expectedTreePath)),
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
        [this](const DeviceSnapshot &snapshot) {
          observe(snapshot);
          reconnectAfterListenerRestart(snapshot);
        }
    );
    connect(m_pairing.get(), &IPairingService::pairingChanged, this, [this](const PairingSnapshot &snapshot) {
      pairingChanged(snapshot);
    });
    connect(m_pairing.get(), &IPairingService::operationFailed, this, [this](const PairingOperationResult &result) {
      finish(false, result.diagnostic);
    });
    connect(m_files.get(), &FileTransferRuntime::peerReady, this, [this](const DeviceId &peer, const auto &) {
      if (m_scenario == Scenario::ListenerResume && peer == m_peerId) m_senderInterruptionEligible = false;
      if (m_role != Role::Sender || peer != m_peerId || m_sendStarted) return;
      m_sendStarted = true;
      QList<QUrl> sources;
      for (const auto &source : m_sourcePaths) sources.append(QUrl::fromLocalFile(source));
      const auto started = m_files->send(m_peerId, sources, {});
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
    connect(m_files.get(), &FileTransferRuntime::errorOccurred, this, [this](FileTransferRuntimeError error, auto, const QString &diagnostic) {
      const bool expectedListenerDisconnect = m_scenario == Scenario::ListenerResume &&
          error == FileTransferRuntimeError::TransportFailed &&
          ((m_role == Role::Receiver && m_receiverInterruptionWindow) ||
           (m_role == Role::Sender && m_senderInterruptionEligible && !m_senderResuming &&
            m_expectedTransportErrorCount == 0));
      if (expectedListenerDisconnect) {
        ++m_expectedTransportErrorCount;
      } else if (!m_finished) {
        finish(false, diagnostic);
      }
    });

    m_probeTimer.setInterval(100);
    connect(&m_probeTimer, &QTimer::timeout, this, [this] {
      if ((m_peerSeen && !m_senderAwaitingEndpointRefresh) || m_finished) return;
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
    if (snapshot.state == TransferState::Failed ||
        (snapshot.state == TransferState::Interrupted && m_scenario != Scenario::ListenerResume)) {
      finish(false, QStringLiteral("transfer did not complete"));
      return;
    }
    if (snapshot.state == TransferState::Completed) {
      m_lastTransferId = snapshot.id.toString();
      m_completedFiles = snapshot.progress.completedFiles;
      m_completedBytes = snapshot.progress.completedBytes;
    }
    if (snapshot.direction == TransferDirection::Sending && m_role == Role::Sender) {
      if (m_scenario == Scenario::ListenerResume) {
        m_senderStates.append(static_cast<int>(snapshot.state));
        if (snapshot.state == TransferState::Transferring &&
            snapshot.progress.completedBytes >= kControlThresholdBytes) {
          m_senderInterruptionEligible = true;
        }
        if (snapshot.state == TransferState::Interrupted) {
          m_senderInterrupted = true;
          m_senderAwaitingEndpointRefresh = true;
          QTimer::singleShot(0, this, [this] {
            if (!m_finished) {
              QString ignored;
              (void)m_discovery->service().probePeer(QHostAddress::LocalHost, m_peerDiscoveryPort, &ignored);
            }
          });
        }
        if (snapshot.state == TransferState::Resuming) {
          m_senderResuming = true;
          m_senderInterruptionEligible = false;
          if (!m_senderFirstResumingCaptured) {
            m_senderFirstResumingCaptured = true;
            m_firstResumingBytes = snapshot.progress.completedBytes;
          }
        }
        if (snapshot.state == TransferState::Completed) {
          finish(m_senderInterrupted && m_senderResuming, QStringLiteral("sender listener resume states missing"));
        }
        return;
      }
      if (m_scenario == Scenario::FileTree) {
        if (snapshot.state == TransferState::Completed) finish(true, {});
        return;
      }
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
        if (snapshot.state == TransferState::Cancelled) {
          m_senderObservedCancelled = true;
          finish(true, {});
        }
        return;
      }
      if (snapshot.state == TransferState::Completed) finish(true, {});
      return;
    }
    if (snapshot.direction != TransferDirection::Receiving || m_role != Role::Receiver) return;
    if (m_scenario == Scenario::ListenerResume) {
      m_receiverStates.append(static_cast<int>(snapshot.state));
      if (snapshot.state == TransferState::Interrupted) m_receiverInterrupted = true;
      if (m_receiverInterrupted && snapshot.state == TransferState::Transferring &&
          !m_receiverFirstTransferringAfterInterruptCaptured) {
        m_receiverFirstTransferringAfterInterruptCaptured = true;
        m_firstReceiverResumingBytes = snapshot.progress.completedBytes;
        m_resumedFromNonZero = m_durableOffset > 0 && m_firstReceiverResumingBytes >= m_durableOffset;
      }
      if (!m_listenerRestartQueued && snapshot.state == TransferState::Transferring &&
          snapshot.progress.completedBytes >= kControlThresholdBytes &&
          snapshot.progress.completedBytes < snapshot.progress.totalBytes) {
        m_listenerRestartQueued = true;
        QTimer::singleShot(0, this, [this, id = snapshot.id, total = snapshot.progress.totalBytes] {
          restartListenerAtCheckpoint(id, total);
        });
      }
      if (snapshot.state == TransferState::Completed) {
        QTimer::singleShot(0, this, [this, transferId = snapshot.id] { validateListenerResumeCompletion(transferId); });
      }
      return;
    }
    if (m_scenario == Scenario::FileTree) {
      if (snapshot.state == TransferState::Completed) {
        m_treeValid = validateTree(snapshot);
        if (!m_treeValid) {
          finish(false, QStringLiteral("file tree completion validation failed"));
        } else {
          m_receiverTransferCompleted = true;
          finishReceiverIfComplete();
        }
      }
      return;
    }
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

  void restartListenerAtCheckpoint(const TransferId &transferId, quint64 totalBytes)
  {
    const auto receiveRoot = QDir(m_root).filePath(QStringLiteral("receive"));
    ::relaydesk::transfer::ResumeStore store(
        QDir(receiveRoot).filePath(QStringLiteral(".incoming/resume-active"))
    );
    const auto loaded = store.load(transferId);
    if (!loaded.ok() || loaded.state->files.isEmpty()) {
      m_listenerRestartQueued = false;
      return;
    }
    const auto &file = loaded.state->files.first();
    const auto partPath = QDir(receiveRoot).filePath(file.partRelativePath);
    const auto partSize = QFileInfo(partPath).size();
    if (file.durableOffset == 0 || file.durableOffset >= totalBytes || partSize < static_cast<qint64>(file.durableOffset)) {
      m_listenerRestartQueued = false;
      return;
    }
    m_durableOffset = file.durableOffset;
    m_partBytesBeforeRestart = static_cast<quint64>(partSize);
    m_resumeStateExisted = true;
    writeListenerStage(QStringLiteral("checkpoint-loaded"));
    m_receiverInterruptionWindow = true;
    writeListenerStage(QStringLiteral("stop-begin"));
    m_files->stop();
    writeListenerStage(QStringLiteral("stop-end"));
    QTimer::singleShot(0, this, [this] {
      QString diagnostic;
      writeListenerStage(QStringLiteral("start-begin"));
      if (!m_files->start(&diagnostic)) finish(false, diagnostic);
      writeListenerStage(QStringLiteral("start-end"));
      m_receiverInterruptionWindow = false;
    });
  }

  void validateListenerResumeCompletion(const TransferId &transferId)
  {
    const auto receiveRoot = QDir(m_root).filePath(QStringLiteral("receive"));
    m_actualSha = fileSha256(QDir(receiveRoot).filePath(QFileInfo(m_sourcePath).fileName())).toHex();
    const auto sidecar = QDir(receiveRoot).filePath(
        QStringLiteral(".incoming/resume-active/%1.resume.cbor").arg(transferId.toString())
    );
    m_resumeStateRemaining = QFileInfo::exists(sidecar);
    const bool valid = m_receiverInterrupted && m_durableOffset > 0 && m_partBytesBeforeRestart >= m_durableOffset &&
                       m_resumedFromNonZero && m_actualSha == m_expectedSha256.toHex() &&
                       !containsPartFile(receiveRoot) && !m_resumeStateRemaining;
    finish(valid, valid ? QString{} : QStringLiteral("listener resume validation failed"));
  }

  void writeListenerStage(const QString &stage) const
  {
    QFile file(QDir(m_root).filePath(QStringLiteral("listener-stage.txt")));
    if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
      file.write(stage.toUtf8() + '\n');
    }
  }

  void reconnectAfterListenerRestart(const DeviceSnapshot &snapshot)
  {
    if (m_role != Role::Sender || m_scenario != Scenario::ListenerResume || !m_senderAwaitingEndpointRefresh ||
        snapshot.id != m_peerId) {
      return;
    }
    const auto info = m_discovery->registry().deviceInfo(snapshot.id);
    if (!info.has_value() || info->filePort == 0) return;
    m_senderAwaitingEndpointRefresh = false;
    QTimer::singleShot(0, this, [this, peer = snapshot.id] {
      QString diagnostic;
      if (!m_finished && !m_files->connectPeer(peer, &diagnostic)) finish(false, diagnostic);
    });
  }

  bool validateTree(const TransferSnapshot &snapshot)
  {
    QFile expectedFile(m_expectedTreePath);
    if (!expectedFile.open(QIODevice::ReadOnly)) return false;
    const auto document = QJsonDocument::fromJson(expectedFile.readAll());
    if (!document.isObject()) return false;
    const auto expected = document.object();
    const auto expectedFiles = expected.value(QStringLiteral("files"));
    const auto expectedDirectories = expected.value(QStringLiteral("directories"));
    const auto expectedBytes = expected.value(QStringLiteral("completedBytes"));
    const auto expectedFileCount = expected.value(QStringLiteral("completedFiles"));
    if (!expectedFiles.isArray() || !expectedDirectories.isArray() || !expectedBytes.isDouble() ||
        !expectedFileCount.isDouble() || snapshot.progress.completedBytes != expectedBytes.toVariant().toULongLong() ||
        snapshot.progress.completedFiles != expectedFileCount.toVariant().toULongLong()) {
      return false;
    }
    const QDir receiveRoot(QDir(m_root).filePath(QStringLiteral("receive")));
    QSet<QString> expectedEntries;
    for (const auto &directory : expectedDirectories.toArray()) {
      if (!directory.isString()) return false;
      const auto info = QFileInfo(receiveRoot.filePath(directory.toString()));
      if (!info.isDir()) return false;
      expectedEntries.insert(QStringLiteral("dir:") + directory.toString());
    }
    for (const auto &fileValue : expectedFiles.toArray()) {
      if (!fileValue.isObject()) return false;
      const auto file = fileValue.toObject();
      const auto path = file.value(QStringLiteral("path"));
      const auto sha256 = file.value(QStringLiteral("sha256"));
      const auto bytes = file.value(QStringLiteral("bytes"));
      if (!path.isString() || !sha256.isString() || !bytes.isDouble()) return false;
      const auto filePath = receiveRoot.filePath(path.toString());
      const auto info = QFileInfo(filePath);
      if (!info.isFile() || info.size() != bytes.toVariant().toLongLong() ||
          fileSha256(filePath).toHex() != sha256.toString().toLatin1()) {
        return false;
      }
      expectedEntries.insert(QStringLiteral("file:") + path.toString());
    }
    QSet<QString> actualEntries;
    QDirIterator iterator(
        receiveRoot.absolutePath(), QDir::AllEntries | QDir::NoDotAndDotDot, QDirIterator::Subdirectories
    );
    while (iterator.hasNext()) {
      iterator.next();
      const auto relativePath = QDir::fromNativeSeparators(receiveRoot.relativeFilePath(iterator.filePath()));
      if (relativePath == QStringLiteral(".incoming") || relativePath.startsWith(QStringLiteral(".incoming/"))) {
        continue;
      }
      const auto info = iterator.fileInfo();
      if (!info.isFile() && !info.isDir()) return false;
      actualEntries.insert((info.isDir() ? QStringLiteral("dir:") : QStringLiteral("file:")) + relativePath);
    }
    return actualEntries == expectedEntries && !containsPartFile(receiveRoot.absolutePath());
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
        {QStringLiteral("transferId"), m_lastTransferId},
        {QStringLiteral("completedFiles"), static_cast<qint64>(m_completedFiles)},
        {QStringLiteral("completedBytes"), static_cast<qint64>(m_completedBytes)},
        {QStringLiteral("tree"), m_treeValid},
        {QStringLiteral("partFilesRemaining"), containsPartFile(QDir(m_root).filePath(QStringLiteral("receive")))},
        {QStringLiteral("interrupted"), m_role == Role::Sender ? m_senderInterrupted : m_receiverInterrupted},
        {QStringLiteral("durableOffset"), static_cast<qint64>(m_durableOffset)},
        {QStringLiteral("partBytesBeforeRestart"), static_cast<qint64>(m_partBytesBeforeRestart)},
        {QStringLiteral("resumeStateExisted"), m_resumeStateExisted},
        {QStringLiteral("resumedFromNonZero"), m_resumedFromNonZero},
        {QStringLiteral("resumeStateRemaining"), m_resumeStateRemaining},
        {QStringLiteral("states"), stateArray(m_role == Role::Sender ? m_senderStates : m_receiverStates)},
        {QStringLiteral("actualSha"), QString::fromLatin1(m_actualSha)},
        {QStringLiteral("firstResumingBytes"), static_cast<qint64>(m_firstResumingBytes)},
        {QStringLiteral("senderFirstResumingCaptured"), m_senderFirstResumingCaptured},
        {QStringLiteral("firstReceiverResumingBytes"), static_cast<qint64>(m_firstReceiverResumingBytes)},
        {QStringLiteral("receiverFirstTransferringAfterInterruptCaptured"), m_receiverFirstTransferringAfterInterruptCaptured},
        {QStringLiteral("expectedTransportErrorCount"), m_expectedTransportErrorCount},
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
  QStringList m_sourcePaths;
  QString m_sourcePath;
  QByteArray m_expectedSha256;
  QString m_expectedTreePath;
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
  QString m_lastTransferId;
  quint64 m_completedFiles = 0;
  quint64 m_completedBytes = 0;
  bool m_treeValid = false;
  bool m_listenerRestartQueued = false;
  bool m_senderInterrupted = false;
  bool m_senderResuming = false;
  bool m_senderAwaitingEndpointRefresh = false;
  bool m_receiverInterrupted = false;
  bool m_resumeStateExisted = false;
  quint64 m_durableOffset = 0;
  quint64 m_partBytesBeforeRestart = 0;
  QList<int> m_senderStates;
  QList<int> m_receiverStates;
  QByteArray m_actualSha;
  bool m_senderInterruptionEligible = false;
  bool m_receiverInterruptionWindow = false;
  bool m_resumedFromNonZero = false;
  bool m_resumeStateRemaining = true;
  quint64 m_firstResumingBytes = 0;
  bool m_senderFirstResumingCaptured = false;
  quint64 m_firstReceiverResumingBytes = 0;
  bool m_receiverFirstTransferringAfterInterruptCaptured = false;
  int m_expectedTransportErrorCount = 0;
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
      QStringLiteral("scenario"), QStringLiteral("complete, pause-resume, cancel, file-tree, or listener-resume"), QStringLiteral("name")
  );
  QCommandLineOption expectedTreeOption(
      QStringLiteral("expected-tree"), QStringLiteral("file-tree expected JSON"), QStringLiteral("path")
  );
  parser.addOptions({
      roleOption, scenarioOption, rootOption, portOption, peerPortOption, sourceOption, shaOption, expectedTreeOption,
      resultOption
  });
  parser.process(application);

  const auto roleText = parser.value(roleOption);
  const auto role = roleText == QStringLiteral("sender") ? Role::Sender
                  : roleText == QStringLiteral("receiver") ? Role::Receiver : Role::Sender;
  const auto scenarioText = parser.value(scenarioOption);
  const auto scenario = scenarioText == QStringLiteral("complete") ? Scenario::Complete
                      : scenarioText == QStringLiteral("pause-resume") ? Scenario::PauseResume
                      : scenarioText == QStringLiteral("cancel") ? Scenario::Cancel
                      : scenarioText == QStringLiteral("file-tree") ? Scenario::FileTree
                      : scenarioText == QStringLiteral("listener-resume") ? Scenario::ListenerResume : Scenario::Complete;
  bool localPortOk = false;
  bool peerPortOk = false;
  const auto localPort = parser.value(portOption).toUShort(&localPortOk);
  const auto peerPort = parser.value(peerPortOption).toUShort(&peerPortOk);
  const auto expected = QByteArray::fromHex(parser.value(shaOption).toLatin1());
  if ((roleText != QStringLiteral("sender") && roleText != QStringLiteral("receiver")) ||
      (scenarioText != QStringLiteral("complete") && scenarioText != QStringLiteral("pause-resume") &&
       scenarioText != QStringLiteral("cancel") && scenarioText != QStringLiteral("file-tree") &&
       scenarioText != QStringLiteral("listener-resume")) || !localPortOk || !peerPortOk ||
      localPort == 0 || peerPort == 0 || parser.value(rootOption).isEmpty() || parser.value(resultOption).isEmpty() ||
      parser.values(sourceOption).isEmpty() || expected.size() != 32 ||
      (scenario == Scenario::FileTree && parser.value(expectedTreeOption).isEmpty())) {
    return 2;
  }
  Peer peer(
      role, scenario, parser.value(rootOption), localPort, peerPort, parser.values(sourceOption), expected,
      parser.value(expectedTreeOption), parser.value(resultOption)
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
