/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/trust/TlsIdentityAdapter.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace deskflow::relaydesk;

namespace {

const auto kCertificatePem = QByteArrayLiteral("-----BEGIN CERTIFICATE-----\n"
                                               "MIICxDCCAaygAwIBAgIIXTCCuzAC28owDQYJKoZIhvcNAQELBQAwIjEgMB4GA1UE\n"
                                               "AxMXUmVsYXlEZXNrIFRlc3QgSWRlbnRpdHkwHhcNMjYwODExMTQzNzQ3WhcNMzEw\n"
                                               "ODEyMTQzNzQ3WjAiMSAwHgYDVQQDExdSZWxheURlc2sgVGVzdCBJZGVudGl0eTCC\n"
                                               "ASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAMtEGlVN6DYEMW2oSbBXC/aa\n"
                                               "GkzH2sddhutYBJ9jYtbGeJLFrzFgo8iwYO1pLkQi8e6zZaFJjLcizuVgNOCKQOwQ\n"
                                               "22E61vFysTp58vElpm/Zim0iGfZKdbQM49ZzlyG+8n6NcJ0bW/LFNdTxyStICQXL\n"
                                               "gZWFiB+/6xvPpzrXIXOVwd3WSyVCSLApXY7ogEVlwLEj+r1RSYSfxpMXfbNG1GQv\n"
                                               "U6L4wK9d6Jj6aCHCQEDy2jghsSMhTjpzxPUodn+KHZPb15vB8yc3Z50kupHMpeHz\n"
                                               "2TkhPNk5BlUP9o6ardLyobUanRtjz0LBXRycQigkbwNba//kdYR58RoDx4hA6c0C\n"
                                               "AwEAATANBgkqhkiG9w0BAQsFAAOCAQEAJbKHz4VMud9QoACvy72zTyc7m2E5rJkd\n"
                                               "gf6oC9Va9hzP96tiecZ6K6kMs3ETLkp1QC+KwlZLlEXt8qvdiwkz92R6jg0erlZA\n"
                                               "6mKZfzaOQ4GSY8u9itr6AcIz7K73Eu3yxUdtey1Jns0hzv7OLR83zhVIswhLjWbp\n"
                                               "lApnuRGKbGVo1272s5H5hnsLgSfRIY/Pu92zxEo7cLJASZeaXHuH5DbLCONZf62c\n"
                                               "tr7nh4Qbz32orYHVdKA/ZuM3n+IXLC1FhRejWp2n1HA+bY8Pw32KKKMTKt3mB9fY\n"
                                               "eCeWLqLNP/PXiOAYCVNFv9BvTZiM0eUYtY3p2XK6YlUqABD00tzrGA==\n"
                                               "-----END CERTIFICATE-----\n");

QString writeCertificate(const QTemporaryDir &directory, QByteArrayView contents = kCertificatePem)
{
  const QString path = directory.filePath(QStringLiteral("deskflow.pem"));
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(contents.data(), contents.size()) != contents.size()) {
    return {};
  }
  return path;
}

} // namespace

class TlsIdentityAdapterTests final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void readsDeskflowCertificateIdentity();
  void rejectsMissingAndMalformedCertificate();
  void rejectsUnexpectedKeySize();
};

void TlsIdentityAdapterTests::readsDeskflowCertificateIdentity()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = writeCertificate(directory);
  QVERIFY(!path.isEmpty());

  const auto identity = TlsIdentityAdapter::inspect(path, 2048);

  QVERIFY2(identity.ok(), qPrintable(identity.diagnostic));
  QCOMPARE(identity.publicKeyBits, 2048);
  QCOMPARE(
      identity.fingerprintSha256.toHex(),
      QByteArrayLiteral("8a7fbfae04b4090475991372c78e7053c774f42d95b9daebf41ce5a6c13beaf3")
  );
  QCOMPARE(identity.certificatePath, QFileInfo(path).canonicalFilePath());
}

void TlsIdentityAdapterTests::rejectsMissingAndMalformedCertificate()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto missing = TlsIdentityAdapter::inspect(directory.filePath(QStringLiteral("missing.pem")));
  QVERIFY(!missing.ok());
  QCOMPARE(missing.error, TlsIdentityError::CertificateNotFound);

  const QString malformedPath = writeCertificate(directory, QByteArrayLiteral("not a certificate"));
  const auto malformed = TlsIdentityAdapter::inspect(malformedPath);
  QVERIFY(!malformed.ok());
  QCOMPARE(malformed.error, TlsIdentityError::CertificateInvalid);
}

void TlsIdentityAdapterTests::rejectsUnexpectedKeySize()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = writeCertificate(directory);

  const auto identity = TlsIdentityAdapter::inspect(path, 4096);

  QVERIFY(!identity.ok());
  QCOMPARE(identity.error, TlsIdentityError::PublicKeySizeMismatch);
  QVERIFY(identity.diagnostic.contains(QStringLiteral("2048")));
  QVERIFY(identity.diagnostic.contains(QStringLiteral("4096")));
}

QTEST_MAIN(TlsIdentityAdapterTests)

#include "TlsIdentityAdapterTests.moc"
