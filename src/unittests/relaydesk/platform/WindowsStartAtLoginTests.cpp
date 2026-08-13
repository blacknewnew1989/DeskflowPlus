/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/platform/WindowsStartAtLogin.h"

#include <QCoreApplication>
#include <QHash>
#include <QTest>
#include <QUuid>

using namespace deskflow::relaydesk;

namespace {

class FakeWindowsRunRegistry final : public IWindowsRunRegistry
{
public:
  WindowsRunRegistryReadResult read(const QString &valueName) const override
  {
    if (readError != StartAtLoginErrorCode::None) {
      return {.errorCode = readError, .nativeError = 5, .diagnostic = QStringLiteral("read failed")};
    }
    const auto iterator = values.constFind(valueName);
    if (iterator == values.cend()) {
      return {};
    }
    return {.found = true, .command = *iterator};
  }

  WindowsRunRegistryResult write(const QString &valueName, const QString &command) override
  {
    if (writeError != StartAtLoginErrorCode::None) {
      return {.errorCode = writeError, .nativeError = 5, .diagnostic = QStringLiteral("write failed")};
    }
    if (!ignoreWrites) {
      values.insert(valueName, command);
    }
    return {};
  }

  WindowsRunRegistryResult remove(const QString &valueName) override
  {
    if (removeError != StartAtLoginErrorCode::None) {
      return {.errorCode = removeError, .nativeError = 5, .diagnostic = QStringLiteral("remove failed")};
    }
    if (!ignoreRemoves) {
      values.remove(valueName);
    }
    return {};
  }

  mutable QHash<QString, QString> values;
  StartAtLoginErrorCode readError = StartAtLoginErrorCode::None;
  StartAtLoginErrorCode writeError = StartAtLoginErrorCode::None;
  StartAtLoginErrorCode removeError = StartAtLoginErrorCode::None;
  bool ignoreWrites = false;
  bool ignoreRemoves = false;
};

} // namespace

class WindowsStartAtLoginTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void commandLineQuotesExecutableAndArguments()
  {
    const auto command = WindowsStartAtLogin::buildCommand(
        QStringLiteral("C:/Program Files/RelayDesk/deskflow.exe"),
        {QStringLiteral("--start-in-tray"), QStringLiteral("value with spaces"), QStringLiteral("ends\\")}
    );
    QCOMPARE(
        command,
        QStringLiteral("\"C:\\Program Files\\RelayDesk\\deskflow.exe\" --start-in-tray \"value with spaces\" ends\\")
    );
    QCOMPARE(
        WindowsStartAtLogin::quoteCommandLineArgument(QStringLiteral("quoted\\\"value")),
        QStringLiteral("\"quoted\\\\\\\"value\"")
    );
    QCOMPARE(
        WindowsStartAtLogin::buildCommand(QStringLiteral("\\\\server\\share\\RelayDesk\\deskflow.exe")),
        QStringLiteral("\\\\server\\share\\RelayDesk\\deskflow.exe")
    );
  }

  void invalidExecutableIsRejected()
  {
    QCOMPARE(WindowsStartAtLogin::buildCommand({}), QString());
    QCOMPARE(WindowsStartAtLogin::buildCommand(QStringLiteral("relative/deskflow.exe")), QString());
    QCOMPARE(WindowsStartAtLogin::buildCommand(QStringLiteral("C:/bad\"path.exe")), QString());

    FakeWindowsRunRegistry registry;
    WindowsStartAtLogin startAtLogin(registry, QStringLiteral("RelayDesk"), QString());
    const auto result = startAtLogin.setEnabled(true);
    QCOMPARE(result.errorCode, StartAtLoginErrorCode::InvalidExecutablePath);
    QVERIFY(registry.values.isEmpty());

    QString valueNameWithNull = QStringLiteral("Relay");
    valueNameWithNull.append(QChar::Null);
    valueNameWithNull.append(QStringLiteral("Desk"));
    WindowsStartAtLogin invalidValueName(
        registry, valueNameWithNull, QStringLiteral("C:/RelayDesk/deskflow.exe")
    );
    QCOMPARE(invalidValueName.setEnabled(true).errorCode, StartAtLoginErrorCode::InvalidExecutablePath);
  }

  void enableQueryAndDisable()
  {
    FakeWindowsRunRegistry registry;
    WindowsStartAtLogin startAtLogin(
        registry, QStringLiteral("RelayDesk"), QStringLiteral("C:/Program Files/RelayDesk/deskflow.exe")
    );

    QCOMPARE(startAtLogin.query().state, StartAtLoginState::Disabled);
    const auto enabled = startAtLogin.setEnabled(true);
    QVERIFY(enabled.succeeded());
    QCOMPARE(enabled.state, StartAtLoginState::Enabled);
    QVERIFY(enabled.configuredCommand.contains(QStringLiteral("--start-in-tray")));

    const auto disabled = startAtLogin.setEnabled(false);
    QVERIFY(disabled.succeeded());
    QCOMPARE(disabled.state, StartAtLoginState::Disabled);
  }

  void staleUpgradePathIsReportedAndRepaired()
  {
    FakeWindowsRunRegistry registry;
    registry.values.insert(
        QStringLiteral("RelayDesk"), QStringLiteral("\"C:\\Old RelayDesk\\deskflow.exe\" --start-in-tray")
    );
    WindowsStartAtLogin startAtLogin(
        registry, QStringLiteral("RelayDesk"), QStringLiteral("C:/Program Files/RelayDesk/deskflow.exe")
    );

    QCOMPARE(startAtLogin.query().state, StartAtLoginState::Stale);
    const auto repaired = startAtLogin.setEnabled(true);
    QVERIFY(repaired.succeeded());
    QCOMPARE(repaired.state, StartAtLoginState::Enabled);
    QVERIFY(!repaired.configuredCommand.contains(QStringLiteral("Old RelayDesk")));
  }

  void adapterFailuresAreTyped()
  {
    FakeWindowsRunRegistry registry;
    WindowsStartAtLogin startAtLogin(registry, QStringLiteral("RelayDesk"), QStringLiteral("C:/RelayDesk/deskflow.exe"));

    registry.readError = StartAtLoginErrorCode::RegistryReadFailed;
    QCOMPARE(startAtLogin.query().errorCode, StartAtLoginErrorCode::RegistryReadFailed);
    registry.readError = StartAtLoginErrorCode::None;

    registry.writeError = StartAtLoginErrorCode::RegistryWriteFailed;
    QCOMPARE(startAtLogin.setEnabled(true).errorCode, StartAtLoginErrorCode::RegistryWriteFailed);
    registry.writeError = StartAtLoginErrorCode::None;

    registry.removeError = StartAtLoginErrorCode::RegistryDeleteFailed;
    QCOMPARE(startAtLogin.setEnabled(false).errorCode, StartAtLoginErrorCode::RegistryDeleteFailed);
  }

  void verificationFailuresAreTyped()
  {
    FakeWindowsRunRegistry registry;
    registry.ignoreWrites = true;
    WindowsStartAtLogin startAtLogin(registry, QStringLiteral("RelayDesk"), QStringLiteral("C:/RelayDesk/deskflow.exe"));
    QCOMPARE(startAtLogin.setEnabled(true).errorCode, StartAtLoginErrorCode::RegistryVerificationFailed);

    registry.ignoreWrites = false;
    QVERIFY(startAtLogin.setEnabled(true).succeeded());
    registry.ignoreRemoves = true;
    QCOMPARE(startAtLogin.setEnabled(false).errorCode, StartAtLoginErrorCode::RegistryVerificationFailed);
  }

#if defined(Q_OS_WIN)
  void nativeCurrentUserRegistryRoundTrip()
  {
    auto registry = makeNativeWindowsRunRegistry();
    const auto valueName = QStringLiteral("RelayDesk-WIN002-Test-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    WindowsStartAtLogin startAtLogin(*registry, valueName, QCoreApplication::applicationFilePath());

    QVERIFY(startAtLogin.setEnabled(false).succeeded());
    const auto enabled = startAtLogin.setEnabled(true);
    QVERIFY2(enabled.succeeded(), qPrintable(enabled.diagnostic));
    QCOMPARE(enabled.state, StartAtLoginState::Enabled);
    const auto disabled = startAtLogin.setEnabled(false);
    QVERIFY2(disabled.succeeded(), qPrintable(disabled.diagnostic));
    QCOMPARE(disabled.state, StartAtLoginState::Disabled);
  }
#endif
};

QTEST_GUILESS_MAIN(WindowsStartAtLoginTests)

#include "WindowsStartAtLoginTests.moc"
