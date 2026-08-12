/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QByteArray>
#include <QSslConfiguration>
#include <QString>

#include <optional>

namespace deskflow::relaydesk {

enum class TlsIdentityError
{
  None,
  CertificateNotFound,
  CertificateInvalid,
  PublicKeyMissing,
  PublicKeyNotRsa,
  PublicKeySizeMismatch,
};

struct TlsIdentitySnapshot
{
  QString certificatePath;
  QByteArray fingerprintSha256;
  int publicKeyBits = -1;
  TlsIdentityError error = TlsIdentityError::None;
  QString diagnostic;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == TlsIdentityError::None && fingerprintSha256.size() == 32 && publicKeyBits > 0;
  }
};

class TlsIdentityAdapter final
{
public:
  // Reads the same combined PEM identity configured for Deskflow's existing
  // secure sockets. It deliberately does not create a second certificate store.
  [[nodiscard]] static TlsIdentitySnapshot inspect(const QString &certificatePath, int expectedKeyBits = 0);

  // Loads that same combined PEM into a Qt TLS configuration. The caller owns
  // peer pinning; certificate-authority validation is deliberately not a
  // replacement for RelayDesk's paired fingerprint mapping.
  [[nodiscard]] static std::optional<QSslConfiguration>
  loadConfiguration(const QString &certificatePath, QString *diagnostic = nullptr);
};

} // namespace deskflow::relaydesk
