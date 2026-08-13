// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/TransferCompletionCodec.h"

#include <QCborMap>
#include <QCborParserError>
#include <QCborValue>
#include <QSet>

#include <limits>
#include <type_traits>
#include <utility>

namespace relaydesk::transfer {
namespace {

enum MessageKey : qint64
{
  TransferIdKey = 1,
  CompletedFilesOrResultCodeKey = 2,
  SkippedFilesOrDiagnosticKey = 3,
  TotalBytesKey = 4,
};

constexpr quint64 kMaximumTransferCompletionMetadataBytes = 1U * 1024U * 1024U;

QCborValue key(qint64 value)
{
  return QCborValue(value);
}

QCborValue valueFor(const QCborMap &map, qint64 value)
{
  return map.value(key(value));
}

void setError(QString *error, const QString &diagnostic)
{
  if (error != nullptr) {
    *error = diagnostic;
  }
}

TransferCompletionDecodeResult failure(TransferCompletionCodecError error, QString diagnostic)
{
  return {.error = error, .diagnostic = std::move(diagnostic)};
}

bool hasExactIntegerKeys(const QCborMap &map, const QSet<qint64> &expected)
{
  if (map.size() != expected.size()) {
    return false;
  }
  for (auto iterator = map.constBegin(); iterator != map.constEnd(); ++iterator) {
    if (!iterator.key().isInteger() || !expected.contains(iterator.key().toInteger())) {
      return false;
    }
  }
  return true;
}

std::optional<TransferId> readTransferId(const QCborValue &value)
{
  if (!value.isByteArray() || value.toByteArray().size() != kUuidBytes) {
    return std::nullopt;
  }
  const TransferId id = QUuid::fromRfc4122(value.toByteArray());
  return id.isNull() ? std::nullopt : std::optional<TransferId>{id};
}

std::optional<quint64> readWireInteger(const QCborValue &value)
{
  if (!value.isInteger() || value.toInteger() < 0) {
    return std::nullopt;
  }
  return static_cast<quint64>(value.toInteger());
}

bool knownResultCode(TransferResultCode code)
{
  switch (code) {
  case TransferResultCode::Ok:
  case TransferResultCode::Partial:
  case TransferResultCode::Cancelled:
  case TransferResultCode::Failed:
    return true;
  }
  return false;
}

QByteArray encodeComplete(const TransferCompleteMessage &message, QString *error)
{
  if (message.transferId.isNull()) {
    setError(error, QStringLiteral("TRANSFER_COMPLETE requires a non-null transfer ID"));
    return {};
  }
  if (message.completedFiles > kMaximumCompletedTransferFiles ||
      message.skippedFiles > kMaximumCompletedTransferFiles ||
      message.completedFiles + message.skippedFiles > kMaximumCompletedTransferFiles) {
    setError(error, QStringLiteral("TRANSFER_COMPLETE file counts exceed the protocol limit"));
    return {};
  }
  if (message.totalBytes > static_cast<quint64>(std::numeric_limits<qint64>::max())) {
    setError(error, QStringLiteral("TRANSFER_COMPLETE total bytes exceed the wire integer range"));
    return {};
  }
  const QCborMap map = {
      {key(TransferIdKey), message.transferId.toRfc4122()},
      {key(CompletedFilesOrResultCodeKey), static_cast<qint64>(message.completedFiles)},
      {key(SkippedFilesOrDiagnosticKey), static_cast<qint64>(message.skippedFiles)},
      {key(TotalBytesKey), static_cast<qint64>(message.totalBytes)},
  };
  return QCborValue(map).toCbor(QCborValue::EncodingOption::SortKeysInMaps);
}

QByteArray encodeResult(const TransferResultMessage &message, QString *error)
{
  if (message.transferId.isNull()) {
    setError(error, QStringLiteral("TRANSFER_RESULT requires a non-null transfer ID"));
    return {};
  }
  if (!knownResultCode(message.code)) {
    setError(error, QStringLiteral("TRANSFER_RESULT code is unknown"));
    return {};
  }
  const bool successful = message.code == TransferResultCode::Ok;
  const qsizetype diagnosticBytes = message.diagnostic.toUtf8().size();
  if ((successful && !message.diagnostic.isEmpty()) ||
      (!successful &&
       (message.diagnostic.isEmpty() || diagnosticBytes > kMaximumTransferResultDiagnosticUtf8Bytes))) {
    setError(error, QStringLiteral("TRANSFER_RESULT code and diagnostic disagree"));
    return {};
  }

  QCborMap map = {
      {key(TransferIdKey), message.transferId.toRfc4122()},
      {key(CompletedFilesOrResultCodeKey), static_cast<qint64>(message.code)},
  };
  if (!successful) {
    map.insert(key(SkippedFilesOrDiagnosticKey), message.diagnostic);
  }
  return QCborValue(map).toCbor(QCborValue::EncodingOption::SortKeysInMaps);
}

} // namespace

QByteArray TransferCompletionCodec::encode(const TransferCompletionMessage &message, QString *error)
{
  if (error != nullptr) {
    error->clear();
  }
  return std::visit(
      [error](const auto &typed) -> QByteArray {
        using Message = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<Message, TransferCompleteMessage>) {
          return encodeComplete(typed, error);
        } else {
          return encodeResult(typed, error);
        }
      },
      message
  );
}

TransferCompletionDecodeResult
TransferCompletionCodec::decode(quint16 protocolVersion, MessageType type, QByteArrayView metadata)
{
  if (protocolVersion != kProtocolMajorVersion) {
    return failure(
        TransferCompletionCodecError::UnsupportedVersion,
        QStringLiteral("transfer completion version is unsupported")
    );
  }
  if (type != MessageType::TransferComplete && type != MessageType::TransferResult) {
    return failure(
        TransferCompletionCodecError::UnsupportedMessageType,
        QStringLiteral("frame is not a transfer completion message")
    );
  }
  if (metadata.isEmpty() || static_cast<quint64>(metadata.size()) > kMaximumTransferCompletionMetadataBytes) {
    return failure(
        TransferCompletionCodecError::TooLarge,
        QStringLiteral("transfer completion metadata is empty or too large")
    );
  }

  QCborParserError parserError;
  const QCborValue value = QCborValue::fromCbor(metadata.toByteArray(), &parserError);
  if (parserError.error != QCborError::NoError || parserError.offset != metadata.size() || !value.isMap()) {
    return failure(
        TransferCompletionCodecError::MalformedCbor,
        QStringLiteral("transfer completion metadata is not one CBOR map")
    );
  }
  const QCborMap map = value.toMap();
  const QSet<qint64> expected = type == MessageType::TransferComplete
                                    ? QSet<qint64>{TransferIdKey, CompletedFilesOrResultCodeKey,
                                                  SkippedFilesOrDiagnosticKey, TotalBytesKey}
                                    : QSet<qint64>{TransferIdKey, CompletedFilesOrResultCodeKey};
  const QSet<qint64> failedResult = {
      TransferIdKey, CompletedFilesOrResultCodeKey, SkippedFilesOrDiagnosticKey};
  if (!hasExactIntegerKeys(map, expected) &&
      !(type == MessageType::TransferResult && hasExactIntegerKeys(map, failedResult))) {
    return failure(
        TransferCompletionCodecError::InvalidFields,
        QStringLiteral("transfer completion contains missing, duplicate, or unknown fields")
    );
  }

  const auto transferId = readTransferId(valueFor(map, TransferIdKey));
  if (!transferId.has_value()) {
    return failure(
        TransferCompletionCodecError::InvalidTransferId,
        QStringLiteral("transfer completion transfer ID is invalid")
    );
  }
  if (type == MessageType::TransferComplete) {
    const auto completedFiles = readWireInteger(valueFor(map, CompletedFilesOrResultCodeKey));
    const auto skippedFiles = readWireInteger(valueFor(map, SkippedFilesOrDiagnosticKey));
    if (!completedFiles.has_value() || !skippedFiles.has_value() ||
        *completedFiles > kMaximumCompletedTransferFiles || *skippedFiles > kMaximumCompletedTransferFiles ||
        *completedFiles + *skippedFiles > kMaximumCompletedTransferFiles) {
      return failure(
          TransferCompletionCodecError::InvalidFileCount,
          QStringLiteral("TRANSFER_COMPLETE file counts exceed the protocol limit")
      );
    }
    const auto totalBytes = readWireInteger(valueFor(map, TotalBytesKey));
    if (!totalBytes.has_value()) {
      return failure(
          TransferCompletionCodecError::InvalidTotalBytes,
          QStringLiteral("TRANSFER_COMPLETE total bytes are invalid")
      );
    }
    return {
        .message =
            TransferCompleteMessage{
                .transferId = *transferId,
                .completedFiles = *completedFiles,
                .skippedFiles = *skippedFiles,
                .totalBytes = *totalBytes,
            },
    };
  }

  const auto encodedCode = readWireInteger(valueFor(map, CompletedFilesOrResultCodeKey));
  if (!encodedCode.has_value() || *encodedCode > std::numeric_limits<quint32>::max() ||
      !knownResultCode(static_cast<TransferResultCode>(*encodedCode))) {
    return failure(
        TransferCompletionCodecError::InvalidResultCode, QStringLiteral("TRANSFER_RESULT code is unknown")
    );
  }
  const auto code = static_cast<TransferResultCode>(*encodedCode);
  QString diagnostic;
  if (map.contains(key(SkippedFilesOrDiagnosticKey))) {
    const QCborValue encodedDiagnostic = valueFor(map, SkippedFilesOrDiagnosticKey);
    if (!encodedDiagnostic.isString() || encodedDiagnostic.toString().isEmpty() ||
        encodedDiagnostic.toString().toUtf8().size() > kMaximumTransferResultDiagnosticUtf8Bytes) {
      return failure(
          TransferCompletionCodecError::InvalidDiagnostic,
          QStringLiteral("TRANSFER_RESULT diagnostic is invalid")
      );
    }
    diagnostic = encodedDiagnostic.toString();
  }
  if ((code == TransferResultCode::Ok) != diagnostic.isEmpty()) {
    return failure(
        TransferCompletionCodecError::InvalidDiagnostic,
        QStringLiteral("TRANSFER_RESULT code and diagnostic disagree")
    );
  }
  return {
      .message =
          TransferResultMessage{
              .transferId = *transferId,
              .code = code,
              .diagnostic = diagnostic,
          },
  };
}

MessageType messageType(const TransferCompletionMessage &message) noexcept
{
  return std::holds_alternative<TransferCompleteMessage>(message) ? MessageType::TransferComplete
                                                                  : MessageType::TransferResult;
}

} // namespace relaydesk::transfer
