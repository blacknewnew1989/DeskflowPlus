/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
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

void TwoProcessRelayDeskRuntimeTests::runScenario(const QString &scenario)
{
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto sourcePath = temporary.filePath(QStringLiteral("payload.bin"));
  const qsizetype sourceSize =
      scenario == QStringLiteral("complete") ? 1024 * 1024 + 37 : 12 * 1024 * 1024 + 113;
  const QByteArray sourceBytes(sourceSize, '\x6a');
  QFile source(sourcePath);
  QVERIFY(source.open(QIODevice::WriteOnly));
  QCOMPARE(source.write(sourceBytes), qint64(sourceBytes.size()));
  source.close();
  const auto expectedSha = QCryptographicHash::hash(sourceBytes, QCryptographicHash::Sha256).toHex();

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

  QProcess receiver;
  receiver.setProgram(peer);
  receiver.setArguments({
      QStringLiteral("--role"), QStringLiteral("receiver"), QStringLiteral("--root"), receiverRoot,
      QStringLiteral("--scenario"), scenario,
      QStringLiteral("--discovery-port"), QString::number(receiverPort), QStringLiteral("--peer-discovery-port"),
      QString::number(senderPort), QStringLiteral("--source"), sourcePath, QStringLiteral("--expected-sha256"),
      QString::fromLatin1(expectedSha), QStringLiteral("--result"), receiverResult,
  });
  QProcess sender;
  sender.setProgram(peer);
  sender.setArguments({
      QStringLiteral("--role"), QStringLiteral("sender"), QStringLiteral("--root"), senderRoot,
      QStringLiteral("--scenario"), scenario,
      QStringLiteral("--discovery-port"), QString::number(senderPort), QStringLiteral("--peer-discovery-port"),
      QString::number(receiverPort), QStringLiteral("--source"), sourcePath, QStringLiteral("--expected-sha256"),
      QString::fromLatin1(expectedSha), QStringLiteral("--result"), senderResult,
  });
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
  QVERIFY2(senderStopped, qPrintable(QStringLiteral("sender did not exit after kill")));
  QVERIFY2(receiverStopped, qPrintable(QStringLiteral("receiver did not exit after kill")));
  QVERIFY2(receiverStarted, qPrintable(receiver.errorString()));
  QVERIFY2(senderStarted, qPrintable(sender.errorString()));
  QVERIFY2(senderFinished, qPrintable(processOutput(sender)));
  QVERIFY2(receiverFinished, qPrintable(processOutput(receiver)));
  QCOMPARE(sender.exitStatus(), QProcess::NormalExit);
  QCOMPARE(receiver.exitStatus(), QProcess::NormalExit);

  const auto senderJson = readResult(senderResult);
  const auto receiverJson = readResult(receiverResult);
  QVERIFY2(
      sender.exitCode() == 0,
      qPrintable(
          QStringLiteral("sender exit %1: %2")
              .arg(sender.exitCode())
              .arg(senderJson.value(QStringLiteral("error")).toString())
      )
  );
  QVERIFY2(
      receiver.exitCode() == 0,
      qPrintable(
          QStringLiteral("receiver exit %1: %2")
              .arg(receiver.exitCode())
              .arg(receiverJson.value(QStringLiteral("error")).toString())
      )
  );
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
}

QTEST_APPLESS_MAIN(TwoProcessRelayDeskRuntimeTests)

#include "TwoProcessRelayDeskRuntimeTests.moc"
