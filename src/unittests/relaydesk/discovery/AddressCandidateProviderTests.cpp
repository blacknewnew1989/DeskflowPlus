/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/discovery/AddressCandidateProvider.h"

#include <QHash>
#include <QSignalSpy>
#include <QTest>

using namespace deskflow::relaydesk;

namespace {
AddressCandidateResult resultFrom(const QSignalSpy &spy)
{
  return spy.first().first().value<AddressCandidateResult>();
}
} // namespace

class AddressCandidateProviderTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void ordersRecentDiscoveredAndManualCandidates();
  void discoveryDisabledStillResolvesManualFallback();
  void dnsFailureIsReportedWithoutBlockingOtherCandidates();
  void invalidInputAndPortAreDiagnosable();
  void asyncCallbacksKeepManualSettingsOrder();
  void asyncDnsFailuresKeepManualSettingsOrder();
  void literalAddressesDoNotInvokeDns();
  void defaultQtResolverResolvesLocalhostAsynchronously();
};

void AddressCandidateProviderTests::ordersRecentDiscoveredAndManualCandidates()
{
  AddressCandidateProvider provider(
      [](const QString &host, AddressCandidateProvider::HostResolutionCallback callback) {
        QCOMPARE(host, QStringLiteral("manual.local"));
        callback(
            {
                QHostAddress(QStringLiteral("2001:db8::2")),
                QHostAddress(QStringLiteral("192.168.1.8")),
                QHostAddress(QStringLiteral("10.0.0.3")),
                QHostAddress(QStringLiteral("192.168.1.8")),
            },
            {}
        );
      }
  );
  QSignalSpy resolved(&provider, &AddressCandidateProvider::candidatesResolved);
  provider.resolveCandidates({
      .settings = {.enabled = true, .manualAddresses = {*parseManualAddress(QStringLiteral("manual.local"))}},
      .recentSuccessfulAddresses = {
          QHostAddress(QStringLiteral("10.0.0.1")),
          QHostAddress(QStringLiteral("10.0.0.2")),
      },
      .discoveredAddresses = {
          QHostAddress(QStringLiteral("10.0.0.2")),
          QHostAddress(QStringLiteral("10.0.0.3")),
      },
  });

  QTRY_COMPARE_WITH_TIMEOUT(resolved.size(), 1, 2000);
  const auto result = resultFrom(resolved);
  QCOMPARE(result.candidates.size(), 5);
  QCOMPARE(result.candidates.at(0).address, QHostAddress(QStringLiteral("10.0.0.1")));
  QCOMPARE(result.candidates.at(0).source, AddressCandidateSource::RecentSuccessful);
  QCOMPARE(result.candidates.at(1).address, QHostAddress(QStringLiteral("10.0.0.2")));
  QCOMPARE(result.candidates.at(2).address, QHostAddress(QStringLiteral("10.0.0.3")));
  QCOMPARE(result.candidates.at(2).source, AddressCandidateSource::Discovered);
  QCOMPARE(result.candidates.at(3).address, QHostAddress(QStringLiteral("192.168.1.8")));
  QCOMPARE(result.candidates.at(4).address, QHostAddress(QStringLiteral("2001:db8::2")));
  QCOMPARE(result.candidates.at(4).source, AddressCandidateSource::Manual);
  QVERIFY(result.unresolvedHosts.isEmpty());
}

void AddressCandidateProviderTests::discoveryDisabledStillResolvesManualFallback()
{
  int lookups = 0;
  AddressCandidateProvider provider(
      [&lookups](const QString &, AddressCandidateProvider::HostResolutionCallback callback) {
        ++lookups;
        callback({QHostAddress(QStringLiteral("192.168.50.20"))}, {});
      }
  );
  QSignalSpy resolved(&provider, &AddressCandidateProvider::candidatesResolved);
  provider.resolveCandidates({
      .settings = {.enabled = false, .manualAddresses = {*parseManualAddress(QStringLiteral("fallback.local"))}},
      .discoveredAddresses = {QHostAddress(QStringLiteral("10.0.0.30"))},
  });

  QTRY_COMPARE_WITH_TIMEOUT(resolved.size(), 1, 2000);
  const auto result = resultFrom(resolved);
  QCOMPARE(lookups, 1);
  QCOMPARE(result.candidates.size(), 1);
  QCOMPARE(result.candidates.first().address, QHostAddress(QStringLiteral("192.168.50.20")));
  QCOMPARE(result.candidates.first().source, AddressCandidateSource::Manual);
}

void AddressCandidateProviderTests::dnsFailureIsReportedWithoutBlockingOtherCandidates()
{
  AddressCandidateProvider provider(
      [](const QString &, AddressCandidateProvider::HostResolutionCallback callback) {
        callback({}, QStringLiteral("host not found"));
      }
  );
  QSignalSpy resolved(&provider, &AddressCandidateProvider::candidatesResolved);
  provider.resolveCandidates({
      .settings = {.manualAddresses = {*parseManualAddress(QStringLiteral("missing.local"))}},
      .recentSuccessfulAddresses = {QHostAddress(QStringLiteral("10.0.0.4"))},
  });

  QTRY_COMPARE_WITH_TIMEOUT(resolved.size(), 1, 2000);
  const auto result = resultFrom(resolved);
  QCOMPARE(result.candidates.size(), 1);
  QCOMPARE(result.unresolvedHosts, QStringList({QStringLiteral("missing.local")}));
  QCOMPARE(result.diagnostics.size(), 1);
  QVERIFY(result.diagnostics.first().contains(QStringLiteral("host not found")));
}

void AddressCandidateProviderTests::invalidInputAndPortAreDiagnosable()
{
  AddressCandidateProvider provider(
      [](const QString &, AddressCandidateProvider::HostResolutionCallback) { QFAIL("DNS must not be called"); }
  );
  QSignalSpy resolved(&provider, &AddressCandidateProvider::candidatesResolved);
  QSignalSpy failed(&provider, &AddressCandidateProvider::resolutionFailed);
  provider.resolveCandidates({
      .settings = {.manualAddresses = {{.host = QStringLiteral("https://bad"), .inputPort = 24800, .filePort = 24801}}},
  });
  QTRY_COMPARE_WITH_TIMEOUT(resolved.size(), 1, 2000);
  QCOMPARE(resultFrom(resolved).candidates.size(), 0);
  QCOMPARE(resultFrom(resolved).unresolvedHosts.size(), 1);

  provider.resolveCandidates({.inputPort = 0, .filePort = 24801});
  QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 2000);
  QVERIFY(failed.first().first().toString().contains(QStringLiteral("1..65535")));
}

void AddressCandidateProviderTests::asyncCallbacksKeepManualSettingsOrder()
{
  QHash<QString, AddressCandidateProvider::HostResolutionCallback> callbacks;
  AddressCandidateProvider provider(
      [&callbacks](const QString &host, AddressCandidateProvider::HostResolutionCallback callback) {
        callbacks.insert(host, std::move(callback));
      }
  );
  QSignalSpy resolved(&provider, &AddressCandidateProvider::candidatesResolved);
  provider.resolveCandidates({
      .settings = {
          .manualAddresses = {
              *parseManualAddress(QStringLiteral("first.local")),
              *parseManualAddress(QStringLiteral("second.local")),
          },
      },
  });
  QCOMPARE(callbacks.size(), 2);
  callbacks.value(QStringLiteral("second.local"))({QHostAddress(QStringLiteral("10.0.0.2"))}, {});
  QCOMPARE(resolved.size(), 0);
  callbacks.value(QStringLiteral("first.local"))({QHostAddress(QStringLiteral("10.0.0.1"))}, {});

  QTRY_COMPARE_WITH_TIMEOUT(resolved.size(), 1, 2000);
  const auto result = resultFrom(resolved);
  QCOMPARE(result.candidates.at(0).originHost, QStringLiteral("first.local"));
  QCOMPARE(result.candidates.at(1).originHost, QStringLiteral("second.local"));
}

void AddressCandidateProviderTests::asyncDnsFailuresKeepManualSettingsOrder()
{
  QHash<QString, AddressCandidateProvider::HostResolutionCallback> callbacks;
  AddressCandidateProvider provider(
      [&callbacks](const QString &host, AddressCandidateProvider::HostResolutionCallback callback) {
        callbacks.insert(host, std::move(callback));
      }
  );
  QSignalSpy resolved(&provider, &AddressCandidateProvider::candidatesResolved);
  provider.resolveCandidates({
      .settings = {
          .manualAddresses = {
              *parseManualAddress(QStringLiteral("first-missing.local")),
              *parseManualAddress(QStringLiteral("second-missing.local")),
          },
      },
  });
  callbacks.value(QStringLiteral("second-missing.local"))({}, QStringLiteral("second failed first"));
  callbacks.value(QStringLiteral("first-missing.local"))({}, QStringLiteral("first failed second"));

  QTRY_COMPARE_WITH_TIMEOUT(resolved.size(), 1, 2000);
  const auto result = resultFrom(resolved);
  QCOMPARE(
      result.unresolvedHosts,
      QStringList({QStringLiteral("first-missing.local"), QStringLiteral("second-missing.local")})
  );
  QVERIFY(result.diagnostics.at(0).contains(QStringLiteral("first failed second")));
  QVERIFY(result.diagnostics.at(1).contains(QStringLiteral("second failed first")));
}

void AddressCandidateProviderTests::literalAddressesDoNotInvokeDns()
{
  AddressCandidateProvider provider(
      [](const QString &, AddressCandidateProvider::HostResolutionCallback) { QFAIL("DNS must not be called"); }
  );
  QSignalSpy resolved(&provider, &AddressCandidateProvider::candidatesResolved);
  provider.resolveCandidates({
      .settings = {
          .manualAddresses = {
              *parseManualAddress(QStringLiteral("192.168.1.20")),
              *parseManualAddress(QStringLiteral("2001:db8::20")),
          },
      },
  });
  QCOMPARE(resolved.size(), 0);
  QTRY_COMPARE_WITH_TIMEOUT(resolved.size(), 1, 2000);
  QCOMPARE(resultFrom(resolved).candidates.size(), 2);
}

void AddressCandidateProviderTests::defaultQtResolverResolvesLocalhostAsynchronously()
{
  AddressCandidateProvider provider;
  QSignalSpy resolved(&provider, &AddressCandidateProvider::candidatesResolved);
  provider.resolveCandidates({
      .settings = {.enabled = false, .manualAddresses = {*parseManualAddress(QStringLiteral("localhost"))}},
  });
  QCOMPARE(resolved.size(), 0);
  QTRY_COMPARE_WITH_TIMEOUT(resolved.size(), 1, 5000);
  QVERIFY(!resultFrom(resolved).candidates.isEmpty());
}

QTEST_MAIN(AddressCandidateProviderTests)

#include "AddressCandidateProviderTests.moc"
