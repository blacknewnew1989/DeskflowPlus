/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/app/PairingTrustRuntime.h"
#include "relaydesk/model/PairingWizardModel.h"
#include "relaydesk/pairing/IPairingService.h"
#include "relaydesk/pairing/PairingService.h"
#include "relaydesk/pairing/PairingStateMachine.h"

#include <QMetaType>
#include <QTest>

#include <concepts>
#include <optional>
#include <type_traits>

using namespace deskflow::relaydesk;
using namespace deskflow::relaydesk::model;

namespace {

using StartMember = PairingOperationResult (IPairingService::*)(const DeviceId &);
using ConfirmMember = PairingOperationResult (IPairingService::*)(const QUuid &);
using SubmitMember = PairingOperationResult (IPairingService::*)(const QUuid &, const QString &);
using CancelMember = PairingOperationResult (IPairingService::*)(const QUuid &);
using RevokeMember = PairingOperationResult (IPairingService::*)(const DeviceId &);
using SnapshotMember = std::optional<PairingSnapshot> (IPairingService::*)() const;
using PendingFingerprintMember = std::optional<QByteArray> (IPairingService::*)(const QUuid &) const;
using PairingChangedSignal = void (IPairingService::*)(PairingSnapshot);
using OperationFailedSignal = void (IPairingService::*)(PairingOperationResult);
using RuntimeStartMember = PairingOperationResult (PairingTrustRuntime::*)(const DeviceId &);
using BindMember = void (PairingWizardModel::*)(IPairingService &);

static_assert(std::is_abstract_v<IPairingService>);
static_assert(std::derived_from<PairingTrustRuntime, IPairingService>);
static_assert(!std::derived_from<PairingService, IPairingService>);
static_assert(std::same_as<decltype(&IPairingService::startPairing), StartMember>);
static_assert(std::same_as<decltype(&IPairingService::confirmMatchingSas), ConfirmMember>);
static_assert(std::same_as<decltype(&IPairingService::submitDisplayedSas), SubmitMember>);
static_assert(std::same_as<decltype(&IPairingService::cancel), CancelMember>);
static_assert(std::same_as<decltype(&IPairingService::revoke), RevokeMember>);
static_assert(std::same_as<decltype(&IPairingService::snapshot), SnapshotMember>);
static_assert(std::same_as<decltype(&IPairingService::pendingFingerprint), PendingFingerprintMember>);
static_assert(std::same_as<decltype(&IPairingService::pairingChanged), PairingChangedSignal>);
static_assert(std::same_as<decltype(&IPairingService::operationFailed), OperationFailedSignal>);
static_assert(std::same_as<decltype(&PairingTrustRuntime::startPairing), RuntimeStartMember>);
static_assert(std::same_as<decltype(&PairingWizardModel::bindService), BindMember>);
static_assert(std::is_constructible_v<PairingWizardModel, IPairingService &, QObject *>);
static_assert(!std::is_constructible_v<PairingWizardModel, PairingStateMachine &, QObject *>);
static_assert(std::is_copy_constructible_v<PairingOperationResult>);
static_assert(std::same_as<std::underlying_type_t<PairingFailureReason>, quint32>);
static_assert(static_cast<quint32>(PairingFailureReason::None) == 0);
static_assert(static_cast<quint32>(PairingFailureReason::Cancelled) == 1);
static_assert(static_cast<quint32>(PairingFailureReason::CodeMismatch) == 2);
static_assert(static_cast<quint32>(PairingFailureReason::Expired) == 3);
static_assert(static_cast<quint32>(PairingFailureReason::TooManyAttempts) == 4);
static_assert(static_cast<quint32>(PairingFailureReason::TransportFailed) == 5);
static_assert(static_cast<quint32>(PairingFailureReason::TrustStoreWriteFailed) == 6);
static_assert(static_cast<quint32>(PairingFailureReason::CertificateChanged) == 7);
static_assert(static_cast<quint32>(PairingFailureReason::DirectConnectionRequired) == 8);

template <class T>
concept ExposesTransportService = requires(T &value) { value.service(); };

template <class T>
concept StartsFromCallerSnapshot = requires(T &value, const DeviceSnapshot &snapshot) {
  value.startPairing(snapshot);
};

static_assert(!ExposesTransportService<PairingTrustRuntime>);
static_assert(!StartsFromCallerSnapshot<IPairingService>);
static_assert(!StartsFromCallerSnapshot<PairingTrustRuntime>);

} // namespace

class PairingInterfaceFreezeTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void pinsQtMetaObjectContract();
};

void PairingInterfaceFreezeTests::pinsQtMetaObjectContract()
{
  const auto &metaObject = IPairingService::staticMetaObject;
  QVERIFY(metaObject.indexOfSignal("pairingChanged(deskflow::relaydesk::PairingSnapshot)") >= 0);
  QVERIFY(metaObject.indexOfSignal("operationFailed(deskflow::relaydesk::PairingOperationResult)") >= 0);
  QVERIFY(QMetaType::fromType<DeviceId>().isValid());
  QVERIFY(QMetaType::fromType<PairingFailureReason>().isValid());
  QVERIFY(QMetaType::fromType<PairingSnapshot>().isValid());
  QVERIFY(QMetaType::fromType<PairingOperationResult>().isValid());
}

QTEST_GUILESS_MAIN(PairingInterfaceFreezeTests)

#include "PairingInterfaceFreezeTests.moc"
