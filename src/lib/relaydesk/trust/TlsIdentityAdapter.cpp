/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/trust/TlsIdentityAdapter.h"

#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslSocket>

namespace deskflow::relaydesk {
namespace {

TlsIdentitySnapshot fail(const QString &path, TlsIdentityError error, QString diagnostic)
{
  return {
      .certificatePath = path,
      .error = error,
      .diagnostic = std::move(diagnostic),
  };
}

} // namespace

TlsIdentitySnapshot TlsIdentityAdapter::inspect(const QString &certificatePath, int expectedKeyBits)
{
  const QFileInfo info(certificatePath);
  if (!info.isFile()) {
    return fail(
        certificatePath, TlsIdentityError::CertificateNotFound,
        QStringLiteral("Deskflow TLS certificate does not exist: %1").arg(certificatePath)
    );
  }

  const QList<QSslCertificate> certificates = QSslCertificate::fromPath(certificatePath, QSsl::Pem);
  if (certificates.isEmpty() || certificates.first().isNull()) {
    return fail(
        certificatePath, TlsIdentityError::CertificateInvalid,
        QStringLiteral("Deskflow TLS certificate could not be parsed: %1").arg(certificatePath)
    );
  }

  const QSslCertificate &certificate = certificates.first();
  const QSslKey key = certificate.publicKey();
  if (key.isNull()) {
    return fail(
        certificatePath, TlsIdentityError::PublicKeyMissing,
        QStringLiteral("Deskflow TLS certificate has no readable public key")
    );
  }
  if (key.algorithm() != QSsl::Rsa) {
    return fail(
        certificatePath, TlsIdentityError::PublicKeyNotRsa,
        QStringLiteral("Deskflow TLS certificate public key is not RSA")
    );
  }
  if (expectedKeyBits > 0 && key.length() != expectedKeyBits) {
    return fail(
        certificatePath, TlsIdentityError::PublicKeySizeMismatch,
        QStringLiteral("Deskflow TLS certificate key has %1 bits; expected %2").arg(key.length()).arg(expectedKeyBits)
    );
  }

  return {
      .certificatePath = info.canonicalFilePath(),
      .fingerprintSha256 = certificate.digest(QCryptographicHash::Sha256),
      .publicKeyBits = key.length(),
  };
}

std::optional<QSslConfiguration>
TlsIdentityAdapter::loadConfiguration(const QString &certificatePath, QString *diagnostic)
{
  if (diagnostic != nullptr) {
    diagnostic->clear();
  }
  const TlsIdentitySnapshot identity = inspect(certificatePath);
  if (!identity.ok()) {
    if (diagnostic != nullptr) {
      *diagnostic = identity.diagnostic;
    }
    return std::nullopt;
  }

  QFile pem(certificatePath);
  if (!pem.open(QIODevice::ReadOnly)) {
    if (diagnostic != nullptr) {
      *diagnostic = QStringLiteral("Deskflow TLS identity could not be opened: %1").arg(pem.errorString());
    }
    return std::nullopt;
  }
  const QByteArray contents = pem.readAll();
  const QList<QSslCertificate> certificates = QSslCertificate::fromData(contents, QSsl::Pem);
  const QSslKey privateKey(contents, QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey);
  if (privateKey.isNull()) {
    if (diagnostic != nullptr) {
      *diagnostic = QStringLiteral("Deskflow combined PEM does not contain an RSA private key");
    }
    return std::nullopt;
  }

  QSslConfiguration configuration = QSslConfiguration::defaultConfiguration();
  configuration.setLocalCertificateChain(certificates);
  configuration.setPrivateKey(privateKey);
  configuration.setProtocol(QSsl::TlsV1_2OrLater);
  // Both file peers present the shared Deskflow identity. CA failures for the
  // expected self-signed deployment are handled by the transport, then the
  // exact paired fingerprint is enforced before RDFT frames are released.
  configuration.setPeerVerifyMode(QSslSocket::VerifyPeer);
  return configuration;
}

} // namespace deskflow::relaydesk
