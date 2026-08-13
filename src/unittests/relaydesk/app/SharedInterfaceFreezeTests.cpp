/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/FileTransferRuntime.h"

#include "relaydesk/device/DeviceInfo.h"
#include "relaydesk/device/DeviceSnapshot.h"
#include "relaydesk/platform/PermissionSnapshot.h"
#include "relaydesk/transfer/TransferHistoryStore.h"

#include <QMetaMethod>
#include <QtTest>

#include <type_traits>

using namespace deskflow::relaydesk;
using namespace relaydesk::transfer;

namespace {

using SendMethod = TransferId (IFileTransferService::*)(
    const DeviceId &, const QList<QUrl> &, const SendOptions &
);
using AcceptMethod = void (IFileTransferService::*)(const TransferId &, const ReceiveOptions &);
using RejectMethod = void (IFileTransferService::*)(const TransferId &, RejectReason);
using TransferIdMethod = void (IFileTransferService::*)(const TransferId &);
using CancelMethod = void (IFileTransferService::*)(const TransferId &, TransferCancelReason, bool);
using ActiveTransfersMethod = QList<TransferSnapshot> (IFileTransferService::*)() const;
using IncomingOfferSignal = void (IFileTransferService::*)(IncomingOffer);
using TransferSnapshotSignal = void (IFileTransferService::*)(TransferSnapshot);
using TransferRemovedSignal = void (IFileTransferService::*)(TransferId);

static_assert(std::is_abstract_v<IFileTransferService>);
static_assert(std::is_base_of_v<IFileTransferService, FileTransferRuntime>);
static_assert(std::is_final_v<FileTransferRuntime>);
static_assert(!std::is_copy_constructible_v<IFileTransferService>);
static_assert(!std::is_copy_constructible_v<FileTransferRuntime>);

static_assert(std::is_same_v<decltype(&IFileTransferService::send), SendMethod>);
static_assert(std::is_same_v<decltype(&IFileTransferService::accept), AcceptMethod>);
static_assert(std::is_same_v<decltype(&IFileTransferService::reject), RejectMethod>);
static_assert(std::is_same_v<decltype(&IFileTransferService::pause), TransferIdMethod>);
static_assert(std::is_same_v<decltype(&IFileTransferService::resume), TransferIdMethod>);
static_assert(std::is_same_v<decltype(&IFileTransferService::cancel), CancelMethod>);
static_assert(std::is_same_v<decltype(&IFileTransferService::retry), TransferIdMethod>);
static_assert(std::is_same_v<decltype(&IFileTransferService::activeTransfers), ActiveTransfersMethod>);
static_assert(std::is_same_v<decltype(&IFileTransferService::incomingOffer), IncomingOfferSignal>);
static_assert(std::is_same_v<decltype(&IFileTransferService::transferAdded), TransferSnapshotSignal>);
static_assert(std::is_same_v<decltype(&IFileTransferService::transferChanged), TransferSnapshotSignal>);
static_assert(std::is_same_v<decltype(&IFileTransferService::transferRemoved), TransferRemovedSignal>);

static_assert(std::is_same_v<std::underlying_type_t<RejectReason>, quint32>);
static_assert(static_cast<quint32>(RejectReason::UserDeclined) == 1);
static_assert(static_cast<quint32>(RejectReason::NotTrusted) == 2);
static_assert(static_cast<quint32>(RejectReason::PolicyDenied) == 3);
static_assert(static_cast<quint32>(RejectReason::InsufficientSpace) == 4);
static_assert(static_cast<quint32>(RejectReason::TooManyFiles) == 5);
static_assert(static_cast<quint32>(RejectReason::PathInvalid) == 6);
static_assert(static_cast<quint32>(RejectReason::UnsupportedCapability) == 7);
static_assert(static_cast<quint32>(RejectReason::Busy) == 8);
static_assert(static_cast<quint32>(RejectReason::InternalError) == 9);
static_assert(std::is_same_v<std::underlying_type_t<TransferCancelReason>, quint32>);
static_assert(static_cast<quint32>(TransferCancelReason::UserRequested) == 1);
static_assert(static_cast<quint32>(TransferCancelReason::ApplicationShutdown) == 2);

static_assert(std::is_copy_constructible_v<DeviceId>);
static_assert(std::is_copy_constructible_v<DeviceInfo>);
static_assert(std::is_copy_constructible_v<DeviceSnapshot>);
static_assert(std::is_copy_constructible_v<IncomingOffer>);
static_assert(std::is_copy_constructible_v<TransferSnapshot>);
static_assert(std::is_copy_constructible_v<TransferHistoryRecord>);
static_assert(std::is_copy_constructible_v<SendOptions>);
static_assert(std::is_copy_constructible_v<ReceiveOptions>);
static_assert(std::is_copy_constructible_v<PermissionSnapshot>);

} // namespace

class SharedInterfaceFreezeTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void freezesServiceSignalsAndMetaTypes();
  void defaultRuntimeCapabilitiesAreHonest();
};

void SharedInterfaceFreezeTests::freezesServiceSignalsAndMetaTypes()
{
  QVERIFY(QMetaMethod::fromSignal(&IFileTransferService::incomingOffer).isValid());
  QVERIFY(QMetaMethod::fromSignal(&IFileTransferService::transferAdded).isValid());
  QVERIFY(QMetaMethod::fromSignal(&IFileTransferService::transferChanged).isValid());
  QVERIFY(QMetaMethod::fromSignal(&IFileTransferService::transferRemoved).isValid());

  QVERIFY(QMetaType::fromType<DeviceId>().isValid());
  QVERIFY(QMetaType::fromType<DeviceCapabilities>().isValid());
  QVERIFY(QMetaType::fromType<DeviceInfo>().isValid());
  QVERIFY(QMetaType::fromType<DeviceSnapshot>().isValid());
  QVERIFY(QMetaType::fromType<IncomingOffer>().isValid());
  QVERIFY(QMetaType::fromType<TransferSnapshot>().isValid());
  QVERIFY(QMetaType::fromType<TransferHistoryRecord>().isValid());
  QVERIFY(QMetaType::fromType<SendOptions>().isValid());
  QVERIFY(QMetaType::fromType<ReceiveOptions>().isValid());
  QVERIFY(QMetaType::fromType<RejectReason>().isValid());
  QVERIFY(QMetaType::fromType<TransferCancelReason>().isValid());
  QVERIFY(QMetaType::fromType<PermissionSnapshot>().isValid());
}

void SharedInterfaceFreezeTests::defaultRuntimeCapabilitiesAreHonest()
{
  const FileTransferRuntimeOptions options;
  QCOMPARE(options.localCapabilities.features, QStringList({QStringLiteral("file.v1"), QStringLiteral("sha256")}));
  QCOMPARE(
      options.localCapabilities.conflictPolicies, QList<ConflictPolicy>({ConflictPolicy::AutoRename})
  );
}

QTEST_GUILESS_MAIN(SharedInterfaceFreezeTests)
#include "SharedInterfaceFreezeTests.moc"
