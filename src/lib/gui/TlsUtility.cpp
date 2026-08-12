/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2024 Symless Ltd.
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "TlsUtility.h"

#include "common/Settings.h"
#include "net/SecureUtils.h"
#include "relaydesk/trust/TlsIdentityAdapter.h"

#include <QString>

namespace deskflow::gui::TlsUtility {

bool isEnabled()
{
  return Settings::value(Settings::Security::TlsEnabled).toBool();
}

bool isCertValid(const QString &certPath)
{
  const auto identity =
      deskflow::relaydesk::TlsIdentityAdapter::inspect(certPath, Settings::value(Settings::Security::KeySize).toInt());
  if (!identity.ok()) {
    qDebug().noquote() << identity.diagnostic;
  }
  return identity.ok();
}

int getCertKeyLength(const QString &certPath)
{
  const auto identity = deskflow::relaydesk::TlsIdentityAdapter::inspect(certPath);
  return identity.ok() ? identity.publicKeyBits : -1;
}

QByteArray certFingerprint(const QString &certPath)
{
  const auto identity = deskflow::relaydesk::TlsIdentityAdapter::inspect(certPath);
  return identity.ok() ? identity.fingerprintSha256 : QByteArray{};
}

bool generateCertificate()
{
  qDebug("generating tls certificate, "
         "all clients must trust the new fingerprint");

  const auto keyLength = std::max(2048, Settings::value(Settings::Security::KeySize).toInt());
  const auto certPath = Settings::value(Settings::Security::Certificate).toString();

  QFileInfo info(certPath);
  if (QDir dir(info.absolutePath()); !dir.exists() && !dir.mkpath(".")) {
    qCritical("failed to create directory for tls certificate");
    return false;
  }

  try {
    deskflow::generatePemSelfSignedCert(certPath, keyLength);
  } catch (const std::exception &e) {
    qCritical() << "failed to generate self-signed pem cert: " << e.what();
    return false;
  }
  qDebug("tls certificate generated");
  return true;
}

} // namespace deskflow::gui::TlsUtility
