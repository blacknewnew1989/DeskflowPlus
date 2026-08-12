/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/platform/PermissionSnapshot.h"

#include <QMetaType>
#include <QTest>

using namespace deskflow::relaydesk;

class PermissionSnapshotTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void isCopyableAndKeepsProbeOnlyDiagnostic();
  void registersSharedValueTypes();
};

void PermissionSnapshotTests::isCopyableAndKeepsProbeOnlyDiagnostic()
{
  const PermissionSnapshot snapshot{
      .platform = PermissionPlatform::MacOS,
      .entries = {{
          .kind = PermissionKind::MacAccessibility,
          .state = PermissionState::Denied,
          .errorCode = static_cast<int>(PermissionErrorCode::MacAccessibilityDenied),
          .canOpenSettings = true,
          .diagnostic = QStringLiteral("private platform diagnostic"),
      }},
      .checkedAtUtc = QDateTime::fromString(QStringLiteral("2026-08-12T12:00:00Z"), Qt::ISODate),
  };

  const auto copy = snapshot;
  QCOMPARE(copy, snapshot);
  QCOMPARE(copy.entries.constFirst().diagnostic, QStringLiteral("private platform diagnostic"));
}

void PermissionSnapshotTests::registersSharedValueTypes()
{
  QVERIFY(QMetaType::fromType<PermissionKind>().isValid());
  QVERIFY(QMetaType::fromType<PermissionState>().isValid());
  QVERIFY(QMetaType::fromType<PermissionProbeEntry>().isValid());
  QVERIFY(QMetaType::fromType<PermissionSnapshot>().isValid());
}

QTEST_GUILESS_MAIN(PermissionSnapshotTests)

#include "PermissionSnapshotTests.moc"
