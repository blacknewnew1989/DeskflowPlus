/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/TransferRuntimeComposition.h"

#include "relaydesk/model/DeviceHomeModel.h"
#include "relaydesk/model/PairingWizardModel.h"
#include "relaydesk/model/PermissionStatusModel.h"
#include "relaydesk/model/TransferCenterModel.h"
#include "relaydesk/transfer/IFileTransferService.h"
#include "relaydesk/widgets/DevicesDock.h"
#include "relaydesk/widgets/TransferCenterDock.h"

#include "../FakePairingService.h"

#include <QTest>

using namespace deskflow::relaydesk;
using namespace deskflow::relaydesk::model;
using namespace deskflow::relaydesk::test;
using namespace deskflow::relaydesk::widgets;
using namespace relaydesk::transfer;

namespace {

class FakeFileTransferService final : public IFileTransferService
{
public:
  TransferStartResult send(const DeviceId &, const QList<QUrl> &, const SendOptions &) override
  {
    ++sendCalls;
    return {.transferId = TransferId::generate()};
  }
  void accept(const TransferId &, const ReceiveOptions &) override
  {
  }
  void reject(const TransferId &, RejectReason) override
  {
  }
  void pause(const TransferId &) override
  {
  }
  void resume(const TransferId &) override
  {
  }
  void cancel(const TransferId &, const TransferCancelOptions &) override
  {
  }
  void retry(const TransferId &) override
  {
  }
  QList<TransferSnapshot> activeTransfers() const override
  {
    return {};
  }

  int sendCalls = 0;
};

struct Fixture
{
  FakePairingService pairingService;
  DeviceHomeModel devices;
  PairingWizardModel pairing{pairingService};
  PermissionStatusModel permissions{PermissionPlatform::Other};
  DevicesDock devicesDock{devices, pairing, permissions};
  TransferCenterModel transfers;
  TransferCenterDock transferDock{transfers};
};

} // namespace

class TransferRuntimeCompositionTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void ownsTypedServiceBindingAndLifecycle();
  void reportsUnavailableOrFailedStartupWithoutStopping();
};

void TransferRuntimeCompositionTests::ownsTypedServiceBindingAndLifecycle()
{
  Fixture fixture;
  auto service = std::make_unique<FakeFileTransferService>();
  auto *serviceObserver = service.get();
  int starts = 0;
  int stops = 0;
  {
    TransferRuntimeComposition composition(
        std::move(service),
        {
            .start = [&](QString *) {
              ++starts;
              return true;
            },
            .stop = [&] { ++stops; },
        },
        fixture.devicesDock, fixture.transferDock,
        {
            .destinationRoot = QStringLiteral("Downloads/RelayDesk"),
            .availableBytes = 4096,
        }
    );

    QVERIFY(!composition.isRunning());
    QVERIFY(composition.start());
    QVERIFY(composition.start());
    QVERIFY(composition.isRunning());
    QCOMPARE(starts, 1);

    Q_EMIT fixture.devicesDock.sendItemsRequested(
        DeviceId::generate(), {QUrl::fromLocalFile(QStringLiteral("C:/source/file.txt"))}, {}
    );
    QCOMPARE(serviceObserver->sendCalls, 1);
  }
  QCOMPARE(stops, 1);
}

void TransferRuntimeCompositionTests::reportsUnavailableOrFailedStartupWithoutStopping()
{
  Fixture fixture;
  int stops = 0;
  TransferRuntimeComposition composition(
      std::make_unique<FakeFileTransferService>(),
      {
          .start = [](QString *diagnostic) {
            if (diagnostic != nullptr) {
              *diagnostic = QStringLiteral("expected startup failure");
            }
            return false;
          },
          .stop = [&] { ++stops; },
      },
      fixture.devicesDock, fixture.transferDock,
      {
          .destinationRoot = QStringLiteral("Downloads/RelayDesk"),
          .availableBytes = 4096,
      }
  );

  QString diagnostic;
  QVERIFY(!composition.start(&diagnostic));
  QCOMPARE(diagnostic, QStringLiteral("expected startup failure"));
  QVERIFY(!composition.isRunning());
  composition.stop();
  QCOMPARE(stops, 0);
}

QTEST_MAIN(TransferRuntimeCompositionTests)

#include "TransferRuntimeCompositionTests.moc"
