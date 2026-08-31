/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QUdpSocket>
#include <QTest>

#include "relaydesk/transfer/TransferControlStateMachine.h"

namespace {

constexpr int kProcessDeadlineMs = 15'000;
constexpr int kProcessKillWaitMs = 2'000;

QJsonObject readResult(const QString &path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  const auto document = QJsonDocument::fromJson(file.readAll());
  return document.isObject() ? document.object() : QJsonObject{};
}

QString processOutput(QProcess &process)
{
  auto output = process.readAllStandardOutput();
  output += process.readAllStandardError();
  return QString::fromUtf8(output);
}

QString processEvidence(
    const QString &scenario, const QString &temporaryPath, const QString &role, QProcess &process,
    const QString &resultPath
)
{
  QFile result(resultPath);
  const auto resultJson = result.open(QIODevice::ReadOnly) ? QString::fromUtf8(result.readAll()) : QStringLiteral("<missing>");
  return QStringLiteral("scenario=%1 temp=%2 role=%3 error=%4 exitStatus=%5 exitCode=%6 result=%7 output=%8")
      .arg(
          scenario, temporaryPath, role, process.errorString(),
          process.exitStatus() == QProcess::NormalExit ? QStringLiteral("normal") : QStringLiteral("crash"),
          QString::number(process.exitCode()), resultJson, processOutput(process)
      );
}

bool stopProcess(QProcess &process)
{
  if (process.state() == QProcess::NotRunning) return true;
  process.kill();
  return process.waitForFinished(kProcessKillWaitMs);
}

bool containsOrderedStates(const QJsonArray &states, std::initializer_list<int> expected)
{
  qsizetype index = 0;
  for (const auto &value : states) {
    if (index < static_cast<qsizetype>(expected.size()) && value.toInt() == *(expected.begin() + index)) ++index;
  }
  return index == static_cast<qsizetype>(expected.size());
}

} // namespace

class TwoProcessRelayDeskRuntimeTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void discoveryPairingAndFileTransferUseTwoIndependentProcesses();
  void pauseAndResumeUseTwoIndependentProcesses();
  void cancelUsesTwoIndependentProcesses();
  void fileTreeUsesTwoIndependentProcesses();
  void listenerResumeUsesTwoIndependentProcesses();
  void receiverRelaunchResumesInTwoIndependentProcesses();

private:
  void runScenario(const QString &scenario);
  void runReceiverRelaunchScenario();
};

void TwoProcessRelayDeskRuntimeTests::discoveryPairingAndFileTransferUseTwoIndependentProcesses()
{
  runScenario(QStringLiteral("complete"));
}

void TwoProcessRelayDeskRuntimeTests::pauseAndResumeUseTwoIndependentProcesses()
{
  runScenario(QStringLiteral("pause-resume"));
}

void TwoProcessRelayDeskRuntimeTests::cancelUsesTwoIndependentProcesses()
{
  runScenario(QStringLiteral("cancel"));
}

void TwoProcessRelayDeskRuntimeTests::fileTreeUsesTwoIndependentProcesses()
{
  runScenario(QStringLiteral("file-tree"));
}

void TwoProcessRelayDeskRuntimeTests::listenerResumeUsesTwoIndependentProcesses()
{
  runScenario(QStringLiteral("listener-resume"));
}

void TwoProcessRelayDeskRuntimeTests::receiverRelaunchResumesInTwoIndependentProcesses()
{
  runReceiverRelaunchScenario();
}

void TwoProcessRelayDeskRuntimeTests::runReceiverRelaunchScenario()
{
  const QString scenario = QStringLiteral("receiver-process-recovery");
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const QByteArray sourceBytes(20 * 1024 * 1024 + 113, '\x6d');
  const QString sourcePath = temporary.filePath(QStringLiteral("process-recovery.bin"));
  QFile source(sourcePath);
  QVERIFY(source.open(QIODevice::WriteOnly));
  QCOMPARE(source.write(sourceBytes), qint64(sourceBytes.size()));
  source.close();
  const QByteArray expectedSha = QCryptographicHash::hash(sourceBytes, QCryptographicHash::Sha256).toHex();

  QUdpSocket senderReservation;
  QUdpSocket receiverReservation;
  QVERIFY(senderReservation.bind(QHostAddress::LocalHost, 0));
  QVERIFY(receiverReservation.bind(QHostAddress::LocalHost, 0));
  const quint16 senderPort = senderReservation.localPort();
  const quint16 receiverPort = receiverReservation.localPort();
  QVERIFY(senderPort != 0 && receiverPort != 0 && senderPort != receiverPort);
  senderReservation.close();
  receiverReservation.close();

  const QString senderRoot = temporary.filePath(QStringLiteral("sender"));
  const QString receiverRoot = temporary.filePath(QStringLiteral("receiver"));
  const QString senderResult = temporary.filePath(QStringLiteral("sender-result.json"));
  const QString receiverStageResult = temporary.filePath(QStringLiteral("receiver-stage.json"));
  const QString receiverFinalResult = temporary.filePath(QStringLiteral("receiver-final.json"));
  const QString peer = QString::fromUtf8(RELAYDESK_TWO_PROCESS_PEER_PATH);
  const auto arguments = [&](const QString &role, const QString &root, quint16 localPort, quint16 remotePort,
                             const QString &result, int generation) {
    return QStringList{
        QStringLiteral("--role"), role,
        QStringLiteral("--root"), root,
        QStringLiteral("--scenario"), scenario,
        QStringLiteral("--discovery-port"), QString::number(localPort),
        QStringLiteral("--peer-discovery-port"), QString::number(remotePort),
        QStringLiteral("--source"), sourcePath,
        QStringLiteral("--expected-sha256"), QString::fromLatin1(expectedSha),
        QStringLiteral("--result"), result,
        QStringLiteral("--restart-generation"), QString::number(generation),
    };
  };

  QProcess receiverFirst;
  receiverFirst.setProgram(peer);
  receiverFirst.setArguments(arguments(
      QStringLiteral("receiver"), receiverRoot, receiverPort, senderPort, receiverStageResult, 0
  ));
  QProcess receiverSecond;
  receiverSecond.setProgram(peer);
  receiverSecond.setArguments(arguments(
      QStringLiteral("receiver"), receiverRoot, receiverPort, senderPort, receiverFinalResult, 1
  ));
  QProcess sender;
  sender.setProgram(peer);
  sender.setArguments(arguments(QStringLiteral("sender"), senderRoot, senderPort, receiverPort, senderResult, 0));
  const auto stopAll = qScopeGuard([&] {
    (void)stopProcess(sender);
    (void)stopProcess(receiverFirst);
    (void)stopProcess(receiverSecond);
  });
  const auto preserveFailure = qScopeGuard([&] {
    if (QTest::currentTestFailed()) {
      temporary.setAutoRemove(false);
      qCritical().noquote() << processEvidence(
          scenario, temporary.path(), QStringLiteral("sender"), sender, senderResult
      );
      qCritical().noquote() << processEvidence(
          scenario, temporary.path(), QStringLiteral("receiver-first"), receiverFirst, receiverStageResult
      );
      qCritical().noquote() << processEvidence(
          scenario, temporary.path(), QStringLiteral("receiver-second"), receiverSecond, receiverFinalResult
      );
    }
  });
  QElapsedTimer deadline;
  deadline.start();
  const auto remaining = [&] { return qMax(0, 45'000 - static_cast<int>(deadline.elapsed())); };

  receiverFirst.start();
  QVERIFY2(receiverFirst.waitForStarted(qMin(5'000, remaining())), qPrintable(receiverFirst.errorString()));
  sender.start();
  QVERIFY2(sender.waitForStarted(qMin(5'000, remaining())), qPrintable(sender.errorString()));
  QVERIFY2(receiverFirst.waitForFinished(remaining()), qPrintable(processOutput(receiverFirst)));
  QCOMPARE(receiverFirst.exitStatus(), QProcess::NormalExit);
  QCOMPARE(receiverFirst.exitCode(), 0);
  const auto stageJson = readResult(receiverStageResult);
  QVERIFY(!stageJson.isEmpty());
  QVERIFY2(stageJson.value(QStringLiteral("passed")).toBool(), qPrintable(stageJson.value(QStringLiteral("error")).toString()));
  QCOMPARE(stageJson.value(QStringLiteral("phase")).toString(), QStringLiteral("checkpoint_ready"));
  QVERIFY(stageJson.value(QStringLiteral("durableOffset")).toVariant().toULongLong() > 0);
  QVERIFY(stageJson.value(QStringLiteral("partBytesBeforeRestart")).toVariant().toULongLong() >=
          stageJson.value(QStringLiteral("durableOffset")).toVariant().toULongLong());
  QVERIFY(stageJson.value(QStringLiteral("resumeStateExisted")).toBool());
  QVERIFY(!stageJson.value(QStringLiteral("transferId")).toString().isEmpty());

  receiverSecond.start();
  QVERIFY2(receiverSecond.waitForStarted(qMin(5'000, remaining())), qPrintable(receiverSecond.errorString()));
  QVERIFY2(receiverSecond.waitForFinished(remaining()), qPrintable(processOutput(receiverSecond)));
  QVERIFY2(sender.waitForFinished(remaining()), qPrintable(processOutput(sender)));
  QCOMPARE(receiverSecond.exitStatus(), QProcess::NormalExit);
  QCOMPARE(sender.exitStatus(), QProcess::NormalExit);
  QCOMPARE(receiverSecond.exitCode(), 0);
  QCOMPARE(sender.exitCode(), 0);
  const auto senderJson = readResult(senderResult);
  const auto receiverJson = readResult(receiverFinalResult);
  QVERIFY(senderJson.value(QStringLiteral("passed")).toBool());
  QVERIFY(receiverJson.value(QStringLiteral("passed")).toBool());
  QCOMPARE(senderJson.value(QStringLiteral("error")).toString(), QString{});
  QCOMPARE(receiverJson.value(QStringLiteral("error")).toString(), QString{});
  QCOMPARE(receiverJson.value(QStringLiteral("phase")).toString(), QStringLiteral("completed"));
  QCOMPARE(receiverJson.value(QStringLiteral("deviceId")).toString(), stageJson.value(QStringLiteral("deviceId")).toString());
  QCOMPARE(receiverJson.value(QStringLiteral("fingerprint")).toString(), stageJson.value(QStringLiteral("fingerprint")).toString());
  QCOMPARE(receiverJson.value(QStringLiteral("settingsFile")).toString(), stageJson.value(QStringLiteral("settingsFile")).toString());
  QCOMPARE(senderJson.value(QStringLiteral("transferId")).toString(), stageJson.value(QStringLiteral("transferId")).toString());
  QCOMPARE(receiverJson.value(QStringLiteral("transferId")).toString(), stageJson.value(QStringLiteral("transferId")).toString());
  QVERIFY(receiverJson.value(QStringLiteral("resumedFromNonZero")).toBool());
  QVERIFY(receiverJson.value(QStringLiteral("receiverFirstTransferringAfterInterruptCaptured")).toBool());
  QVERIFY(receiverJson.value(QStringLiteral("firstReceiverResumingBytes")).toVariant().toULongLong() >=
          stageJson.value(QStringLiteral("durableOffset")).toVariant().toULongLong());
  QCOMPARE(receiverJson.value(QStringLiteral("actualSha")).toString(), QString::fromLatin1(expectedSha));
  QVERIFY(!receiverJson.value(QStringLiteral("partFilesRemaining")).toBool());
  QVERIFY(!receiverJson.value(QStringLiteral("resumeStateRemaining")).toBool());
  QVERIFY(containsOrderedStates(
      senderJson.value(QStringLiteral("states")).toArray(),
      {static_cast<int>(::relaydesk::transfer::TransferState::Interrupted),
       static_cast<int>(::relaydesk::transfer::TransferState::Resuming),
       static_cast<int>(::relaydesk::transfer::TransferState::Completed)}
  ));
  QVERIFY(containsOrderedStates(
      receiverJson.value(QStringLiteral("states")).toArray(),
      {static_cast<int>(::relaydesk::transfer::TransferState::Interrupted),
       static_cast<int>(::relaydesk::transfer::TransferState::Completed)}
  ));
  QVERIFY(senderJson.value(QStringLiteral("senderFirstResumingCaptured")).toBool());
  QVERIFY(senderJson.value(QStringLiteral("firstResumingBytes")).toVariant().toULongLong() >=
          stageJson.value(QStringLiteral("durableOffset")).toVariant().toULongLong());
  QVERIFY(senderJson.value(QStringLiteral("expectedTransportErrorCount")).toInt() <= 1);
}

void TwoProcessRelayDeskRuntimeTests::runScenario(const QString &scenario)
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto sourcePath = temporary.filePath(QStringLiteral("payload.bin"));
  QStringList sourcePaths;
  QString expectedTreePath;
  quint64 expectedCompletedFiles = 1;
  quint64 expectedCompletedBytes = 0;
  QByteArray expectedSha;
  if (scenario == QStringLiteral("file-tree")) {
    const QDir sourceRoot(temporary.filePath(QStringLiteral("sources")));
    QVERIFY(QDir().mkpath(sourceRoot.filePath(QStringLiteral("bundle/nested/empty"))));
    const auto alphaPath = sourceRoot.filePath(QStringLiteral("alpha.txt"));
    const auto bundlePath = sourceRoot.filePath(QStringLiteral("bundle"));
    const auto leafPath = sourceRoot.filePath(QStringLiteral("bundle/nested/leaf.bin"));
    const QByteArray alphaBytes = QByteArrayLiteral("alpha relaydesk\n");
    const QByteArray leafBytes(4097, '\x4c');
    QFile alpha(alphaPath);
    QVERIFY(alpha.open(QIODevice::WriteOnly));
    QCOMPARE(alpha.write(alphaBytes), qint64(alphaBytes.size()));
    alpha.close();
    QFile leaf(leafPath);
    QVERIFY(leaf.open(QIODevice::WriteOnly));
    QCOMPARE(leaf.write(leafBytes), qint64(leafBytes.size()));
    leaf.close();
    sourcePaths = {alphaPath, bundlePath};
    expectedSha = QCryptographicHash::hash(alphaBytes, QCryptographicHash::Sha256).toHex();
    expectedCompletedFiles = 2;
    expectedCompletedBytes = static_cast<quint64>(alphaBytes.size() + leafBytes.size());
    expectedTreePath = temporary.filePath(QStringLiteral("expected-tree.json"));
    const QJsonObject expectedTree{
        {QStringLiteral("completedFiles"), static_cast<qint64>(expectedCompletedFiles)},
        {QStringLiteral("completedBytes"), static_cast<qint64>(expectedCompletedBytes)},
        {QStringLiteral("directories"), QJsonArray{
             QStringLiteral("bundle"), QStringLiteral("bundle/nested"), QStringLiteral("bundle/nested/empty")}},
        {QStringLiteral("files"), QJsonArray{
             QJsonObject{{QStringLiteral("path"), QStringLiteral("alpha.txt")},
                         {QStringLiteral("bytes"), static_cast<qint64>(alphaBytes.size())},
                         {QStringLiteral("sha256"), QString::fromLatin1(expectedSha)}},
             QJsonObject{{QStringLiteral("path"), QStringLiteral("bundle/nested/leaf.bin")},
                         {QStringLiteral("bytes"), static_cast<qint64>(leafBytes.size())},
                         {QStringLiteral("sha256"), QString::fromLatin1(
                              QCryptographicHash::hash(leafBytes, QCryptographicHash::Sha256).toHex())}}}},
    };
    const auto expectedTreeBytes = QJsonDocument(expectedTree).toJson(QJsonDocument::Compact);
    QFile expectedFile(expectedTreePath);
    QVERIFY(expectedFile.open(QIODevice::WriteOnly));
    QCOMPARE(expectedFile.write(expectedTreeBytes), qint64(expectedTreeBytes.size()));
    expectedFile.close();
  } else {
    const qsizetype sourceSize = scenario == QStringLiteral("complete") ? 1024 * 1024 + 37
                                : scenario == QStringLiteral("listener-resume") ? 20 * 1024 * 1024 + 113
                                                                             : 12 * 1024 * 1024 + 113;
    const QByteArray sourceBytes(sourceSize, '\x6a');
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write(sourceBytes), qint64(sourceBytes.size()));
    source.close();
    sourcePaths = {sourcePath};
    expectedSha = QCryptographicHash::hash(sourceBytes, QCryptographicHash::Sha256).toHex();
    expectedCompletedBytes = static_cast<quint64>(sourceBytes.size());
  }

  QUdpSocket senderReservation;
  QUdpSocket receiverReservation;
  QVERIFY(senderReservation.bind(QHostAddress::LocalHost, 0));
  QVERIFY(receiverReservation.bind(QHostAddress::LocalHost, 0));
  const auto senderPort = senderReservation.localPort();
  const auto receiverPort = receiverReservation.localPort();
  QVERIFY(senderPort != 0);
  QVERIFY(receiverPort != 0);
  QVERIFY(senderPort != receiverPort);
  senderReservation.close();
  receiverReservation.close();

  const auto senderRoot = temporary.filePath(QStringLiteral("sender"));
  const auto receiverRoot = temporary.filePath(QStringLiteral("receiver"));
  const auto senderResult = temporary.filePath(QStringLiteral("sender-result.json"));
  const auto receiverResult = temporary.filePath(QStringLiteral("receiver-result.json"));
  const QString peer = QString::fromUtf8(RELAYDESK_TWO_PROCESS_PEER_PATH);
  const auto peerArguments = [&](QString role, QString root, quint16 localPort, quint16 remotePort, QString result) {
    QStringList arguments{
        QStringLiteral("--role"), std::move(role), QStringLiteral("--root"), std::move(root),
        QStringLiteral("--scenario"), scenario, QStringLiteral("--discovery-port"), QString::number(localPort),
        QStringLiteral("--peer-discovery-port"), QString::number(remotePort), QStringLiteral("--expected-sha256"),
        QString::fromLatin1(expectedSha), QStringLiteral("--result"), std::move(result),
    };
    for (const auto &source : sourcePaths) arguments << QStringLiteral("--source") << source;
    if (!expectedTreePath.isEmpty()) arguments << QStringLiteral("--expected-tree") << expectedTreePath;
    return arguments;
  };

  QProcess receiver;
  receiver.setProgram(peer);
  receiver.setArguments(peerArguments(QStringLiteral("receiver"), receiverRoot, receiverPort, senderPort, receiverResult));
  QProcess sender;
  sender.setProgram(peer);
  sender.setArguments(peerArguments(QStringLiteral("sender"), senderRoot, senderPort, receiverPort, senderResult));
  QElapsedTimer deadline;
  deadline.start();
  const auto remaining = [&deadline] {
    return qMax(0, kProcessDeadlineMs - static_cast<int>(deadline.elapsed()));
  };
  receiver.start();
  const auto receiverStarted = receiver.waitForStarted(qMin(5'000, remaining()));
  bool senderStarted = false;
  if (receiverStarted && remaining() > 0) {
    sender.start();
    senderStarted = sender.waitForStarted(qMin(5'000, remaining()));
  }
  if (receiverStarted && senderStarted) {
    while ((sender.state() != QProcess::NotRunning || receiver.state() != QProcess::NotRunning) &&
           deadline.elapsed() < kProcessDeadlineMs) {
      if (sender.state() != QProcess::NotRunning) sender.waitForFinished(50);
      if (receiver.state() != QProcess::NotRunning) receiver.waitForFinished(50);
    }
  }
  const auto senderFinished = sender.state() == QProcess::NotRunning;
  const auto receiverFinished = receiver.state() == QProcess::NotRunning;
  const auto senderStopped = stopProcess(sender);
  const auto receiverStopped = stopProcess(receiver);
  if (!senderStopped) {
    temporary.setAutoRemove(false);
    QFAIL(qPrintable(processEvidence(scenario, temporary.path(), QStringLiteral("sender"), sender, senderResult)));
  }
  if (!receiverStopped) {
    temporary.setAutoRemove(false);
    QFAIL(qPrintable(processEvidence(scenario, temporary.path(), QStringLiteral("receiver"), receiver, receiverResult)));
  }
  if (!receiverStarted || !receiverFinished || receiver.exitStatus() != QProcess::NormalExit) {
    temporary.setAutoRemove(false);
    QFAIL(qPrintable(processEvidence(scenario, temporary.path(), QStringLiteral("receiver"), receiver, receiverResult)));
  }
  if (!senderStarted || !senderFinished || sender.exitStatus() != QProcess::NormalExit) {
    temporary.setAutoRemove(false);
    QFAIL(qPrintable(processEvidence(scenario, temporary.path(), QStringLiteral("sender"), sender, senderResult)));
  }

  const auto senderJson = readResult(senderResult);
  const auto receiverJson = readResult(receiverResult);
  const auto preserveValidationFailure = qScopeGuard([&] {
    if (QTest::currentTestFailed()) {
      temporary.setAutoRemove(false);
      qCritical().noquote() << processEvidence(
          scenario, temporary.path(), QStringLiteral("sender"), sender, senderResult
      );
      qCritical().noquote() << processEvidence(
          scenario, temporary.path(), QStringLiteral("receiver"), receiver, receiverResult
      );
    }
  });
  if (sender.exitCode() != 0 || senderJson.isEmpty()) {
    temporary.setAutoRemove(false);
    QFAIL(qPrintable(processEvidence(scenario, temporary.path(), QStringLiteral("sender"), sender, senderResult)));
  }
  if (receiver.exitCode() != 0 || receiverJson.isEmpty()) {
    temporary.setAutoRemove(false);
    QFAIL(qPrintable(processEvidence(scenario, temporary.path(), QStringLiteral("receiver"), receiver, receiverResult)));
  }
  QVERIFY(!senderJson.isEmpty());
  QVERIFY(!receiverJson.isEmpty());
  QVERIFY2(
      senderJson.value(QStringLiteral("passed")).toBool(),
      qPrintable(senderJson.value(QStringLiteral("error")).toString())
  );
  QVERIFY2(
      receiverJson.value(QStringLiteral("passed")).toBool(),
      qPrintable(receiverJson.value(QStringLiteral("error")).toString())
  );
  QCOMPARE(senderJson.value(QStringLiteral("error")).toString(), QString{});
  QCOMPARE(receiverJson.value(QStringLiteral("error")).toString(), QString{});
  QVERIFY(senderJson.value(QStringLiteral("discovered")).toBool());
  QVERIFY(receiverJson.value(QStringLiteral("discovered")).toBool());
  QVERIFY(senderJson.value(QStringLiteral("trusted")).toBool());
  QVERIFY(receiverJson.value(QStringLiteral("trusted")).toBool());
  QVERIFY(
      senderJson.value(QStringLiteral("deviceId")).toString() !=
      receiverJson.value(QStringLiteral("deviceId")).toString()
  );
  QVERIFY(
      senderJson.value(QStringLiteral("fingerprint")).toString() !=
      receiverJson.value(QStringLiteral("fingerprint")).toString()
  );
  QVERIFY(
      senderJson.value(QStringLiteral("settingsFile")).toString() !=
      receiverJson.value(QStringLiteral("settingsFile")).toString()
  );
  QVERIFY(QFile::exists(senderJson.value(QStringLiteral("settingsFile")).toString()));
  QVERIFY(QFile::exists(receiverJson.value(QStringLiteral("settingsFile")).toString()));
  if (scenario == QStringLiteral("pause-resume")) {
    QVERIFY(receiverJson.value(QStringLiteral("receiverControlled")).toBool());
    QVERIFY(receiverJson.value(QStringLiteral("pauseBytesStable")).toBool());
    QVERIFY(senderJson.value(QStringLiteral("senderObservedPause")).toBool());
  }
  if (scenario == QStringLiteral("cancel")) {
    QVERIFY(receiverJson.value(QStringLiteral("receiverControlled")).toBool());
    QVERIFY(senderJson.value(QStringLiteral("cancelled")).toBool());
    QVERIFY(receiverJson.value(QStringLiteral("cancelled")).toBool());
    QVERIFY(receiverJson.value(QStringLiteral("cancelCleanupValid")).toBool());
  }
  if (scenario == QStringLiteral("file-tree")) {
    QVERIFY(!senderJson.value(QStringLiteral("transferId")).toString().isEmpty());
    QVERIFY(!receiverJson.value(QStringLiteral("transferId")).toString().isEmpty());
    QCOMPARE(
        senderJson.value(QStringLiteral("transferId")).toString(),
        receiverJson.value(QStringLiteral("transferId")).toString()
    );
    QCOMPARE(senderJson.value(QStringLiteral("completedFiles")).toVariant().toULongLong(), expectedCompletedFiles);
    QCOMPARE(receiverJson.value(QStringLiteral("completedFiles")).toVariant().toULongLong(), expectedCompletedFiles);
    QCOMPARE(senderJson.value(QStringLiteral("completedBytes")).toVariant().toULongLong(), expectedCompletedBytes);
    QCOMPARE(receiverJson.value(QStringLiteral("completedBytes")).toVariant().toULongLong(), expectedCompletedBytes);
    QVERIFY(receiverJson.value(QStringLiteral("tree")).toBool());
    QVERIFY(!receiverJson.value(QStringLiteral("partFilesRemaining")).toBool());
  }
  if (scenario == QStringLiteral("listener-resume")) {
    QCOMPARE(senderJson.value(QStringLiteral("transferId")).toString(), receiverJson.value(QStringLiteral("transferId")).toString());
    QVERIFY(senderJson.value(QStringLiteral("interrupted")).toBool());
    QVERIFY(receiverJson.value(QStringLiteral("interrupted")).toBool());
    QVERIFY(receiverJson.value(QStringLiteral("resumeStateExisted")).toBool());
    QVERIFY(receiverJson.value(QStringLiteral("durableOffset")).toVariant().toULongLong() > 0);
    QVERIFY(receiverJson.value(QStringLiteral("partBytesBeforeRestart")).toVariant().toULongLong() >=
            receiverJson.value(QStringLiteral("durableOffset")).toVariant().toULongLong());
    QVERIFY(receiverJson.value(QStringLiteral("resumedFromNonZero")).toBool());
    QVERIFY(!receiverJson.value(QStringLiteral("partFilesRemaining")).toBool());
    QCOMPARE(receiverJson.value(QStringLiteral("actualSha")).toString(), QString::fromLatin1(expectedSha));
    QVERIFY(containsOrderedStates(
        senderJson.value(QStringLiteral("states")).toArray(),
        {static_cast<int>(::relaydesk::transfer::TransferState::Interrupted),
         static_cast<int>(::relaydesk::transfer::TransferState::Resuming),
         static_cast<int>(::relaydesk::transfer::TransferState::Completed)}
    ));
    QVERIFY(containsOrderedStates(
        receiverJson.value(QStringLiteral("states")).toArray(),
        {static_cast<int>(::relaydesk::transfer::TransferState::Interrupted),
         static_cast<int>(::relaydesk::transfer::TransferState::Completed)}
    ));
    QVERIFY(senderJson.value(QStringLiteral("senderFirstResumingCaptured")).toBool());
    QVERIFY(receiverJson.value(QStringLiteral("receiverFirstTransferringAfterInterruptCaptured")).toBool());
    QVERIFY(senderJson.value(QStringLiteral("firstResumingBytes")).toVariant().toULongLong() >=
            receiverJson.value(QStringLiteral("durableOffset")).toVariant().toULongLong());
    QVERIFY(receiverJson.value(QStringLiteral("firstReceiverResumingBytes")).toVariant().toULongLong() >=
            receiverJson.value(QStringLiteral("durableOffset")).toVariant().toULongLong());
    QVERIFY(!receiverJson.value(QStringLiteral("resumeStateRemaining")).toBool());
    QVERIFY(senderJson.value(QStringLiteral("expectedTransportErrorCount")).toInt() <= 1);
  }
}

QTEST_APPLESS_MAIN(TwoProcessRelayDeskRuntimeTests)

#include "TwoProcessRelayDeskRuntimeTests.moc"
