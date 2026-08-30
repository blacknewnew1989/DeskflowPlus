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
#include <QTemporaryDir>
#include <QUdpSocket>
#include <QTest>

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

QString processEvidence(const QString &scenario, const QString &role, QProcess &process, const QString &resultPath)
{
  QFile result(resultPath);
  const auto resultJson = result.open(QIODevice::ReadOnly) ? QString::fromUtf8(result.readAll()) : QStringLiteral("<missing>");
  return QStringLiteral("scenario=%1 role=%2 error=%3 exitStatus=%4 exitCode=%5 result=%6 output=%7")
      .arg(
          scenario, role, process.errorString(),
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

} // namespace

class TwoProcessRelayDeskRuntimeTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void discoveryPairingAndFileTransferUseTwoIndependentProcesses();
  void pauseAndResumeUseTwoIndependentProcesses();
  void cancelUsesTwoIndependentProcesses();
  void fileTreeUsesTwoIndependentProcesses();

private:
  void runScenario(const QString &scenario);
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
    const qsizetype sourceSize =
        scenario == QStringLiteral("complete") ? 1024 * 1024 + 37 : 12 * 1024 * 1024 + 113;
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
    QFAIL(qPrintable(processEvidence(scenario, QStringLiteral("sender"), sender, senderResult)));
  }
  if (!receiverStopped) {
    temporary.setAutoRemove(false);
    QFAIL(qPrintable(processEvidence(scenario, QStringLiteral("receiver"), receiver, receiverResult)));
  }
  if (!receiverStarted || !receiverFinished || receiver.exitStatus() != QProcess::NormalExit) {
    temporary.setAutoRemove(false);
    QFAIL(qPrintable(processEvidence(scenario, QStringLiteral("receiver"), receiver, receiverResult)));
  }
  if (!senderStarted || !senderFinished || sender.exitStatus() != QProcess::NormalExit) {
    temporary.setAutoRemove(false);
    QFAIL(qPrintable(processEvidence(scenario, QStringLiteral("sender"), sender, senderResult)));
  }

  const auto senderJson = readResult(senderResult);
  const auto receiverJson = readResult(receiverResult);
  if (sender.exitCode() != 0 || senderJson.isEmpty()) {
    temporary.setAutoRemove(false);
    QFAIL(qPrintable(processEvidence(scenario, QStringLiteral("sender"), sender, senderResult)));
  }
  if (receiver.exitCode() != 0 || receiverJson.isEmpty()) {
    temporary.setAutoRemove(false);
    QFAIL(qPrintable(processEvidence(scenario, QStringLiteral("receiver"), receiver, receiverResult)));
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
}

QTEST_APPLESS_MAIN(TwoProcessRelayDeskRuntimeTests)

#include "TwoProcessRelayDeskRuntimeTests.moc"
