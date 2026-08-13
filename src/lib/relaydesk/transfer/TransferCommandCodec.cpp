// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/TransferCommandCodec.h"

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
  ReasonKey = 2,
  KeepPartialKey = 3,
};

constexpr quint64 kMaximumTransferCommandMetadataBytes = 1U * 1024U * 1024U;

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

TransferCommandDecodeResult failure(TransferCommandCodecError error, QString diagnostic)
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
  return TransferId::fromBytes(value.toByteArray());
}

bool validCancelReason(TransferCancelReason reason)
{
  return reason == TransferCancelReason::UserRequested || reason == TransferCancelReason::ApplicationShutdown;
}

QByteArray encodeTransferId(const TransferId &transferId, QString *error)
{
  return QCborValue(QCborMap{{key(TransferIdKey), transferId.toBytes()}})
      .toCbor(QCborValue::EncodingOption::SortKeysInMaps);
}

} // namespace

QByteArray TransferCommandCodec::encode(const TransferCommandMessage &message, QString *error)
{
  if (error != nullptr) {
    error->clear();
  }
  return std::visit(
      [error](const auto &typed) -> QByteArray {
        using Message = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<Message, TransferPauseMessage> ||
                      std::is_same_v<Message, TransferResumeMessage>) {
          return encodeTransferId(typed.transferId, error);
        } else {
          if (!validCancelReason(typed.reason)) {
            setError(error, QStringLiteral("TRANSFER_CANCEL reason is unknown"));
            return {};
          }
          const QCborMap map = {
              {key(TransferIdKey), typed.transferId.toBytes()},
              {key(ReasonKey), static_cast<qint64>(typed.reason)},
              {key(KeepPartialKey), typed.keepPartial},
          };
          return QCborValue(map).toCbor(QCborValue::EncodingOption::SortKeysInMaps);
        }
      },
      message
  );
}

TransferCommandDecodeResult
TransferCommandCodec::decode(quint16 protocolVersion, MessageType type, QByteArrayView metadata)
{
  if (protocolVersion != kProtocolMajorVersion) {
    return failure(
        TransferCommandCodecError::UnsupportedVersion, QStringLiteral("transfer command version is unsupported")
    );
  }
  if (type != MessageType::TransferPause && type != MessageType::TransferResume &&
      type != MessageType::TransferCancel) {
    return failure(
        TransferCommandCodecError::UnsupportedMessageType, QStringLiteral("frame is not a transfer command")
    );
  }
  if (metadata.isEmpty() || static_cast<quint64>(metadata.size()) > kMaximumTransferCommandMetadataBytes) {
    return failure(
        TransferCommandCodecError::TooLarge, QStringLiteral("transfer command metadata is empty or too large")
    );
  }

  QCborParserError parserError;
  const QCborValue value = QCborValue::fromCbor(metadata.toByteArray(), &parserError);
  if (parserError.error != QCborError::NoError || parserError.offset != metadata.size() || !value.isMap()) {
    return failure(
        TransferCommandCodecError::MalformedCbor, QStringLiteral("transfer command metadata is not one CBOR map")
    );
  }
  const QCborMap map = value.toMap();
  const QSet<qint64> expected = type == MessageType::TransferCancel
                                    ? QSet<qint64>{TransferIdKey, ReasonKey, KeepPartialKey}
                                    : QSet<qint64>{TransferIdKey};
  if (!hasExactIntegerKeys(map, expected)) {
    return failure(
        TransferCommandCodecError::InvalidFields,
        QStringLiteral("transfer command contains missing, duplicate, or unknown fields")
    );
  }

  const auto transferId = readTransferId(valueFor(map, TransferIdKey));
  if (!transferId.has_value()) {
    return failure(
        TransferCommandCodecError::InvalidTransferId, QStringLiteral("transfer command transfer ID is invalid")
    );
  }
  if (type == MessageType::TransferPause) {
    return {.message = TransferPauseMessage{.transferId = *transferId}};
  }
  if (type == MessageType::TransferResume) {
    return {.message = TransferResumeMessage{.transferId = *transferId}};
  }

  const QCborValue reason = valueFor(map, ReasonKey);
  if (!reason.isInteger() || reason.toInteger() < 0 ||
      reason.toInteger() > std::numeric_limits<quint32>::max() ||
      !validCancelReason(static_cast<TransferCancelReason>(reason.toInteger()))) {
    return failure(TransferCommandCodecError::InvalidReason, QStringLiteral("TRANSFER_CANCEL reason is unknown"));
  }
  const QCborValue keepPartial = valueFor(map, KeepPartialKey);
  if (!keepPartial.isBool()) {
    return failure(
        TransferCommandCodecError::InvalidKeepPartial,
        QStringLiteral("TRANSFER_CANCEL keepPartial is not boolean")
    );
  }
  return {
      .message =
          TransferCancelMessage{
              .transferId = *transferId,
              .reason = static_cast<TransferCancelReason>(reason.toInteger()),
              .keepPartial = keepPartial.toBool(),
          },
  };
}

MessageType messageType(const TransferCommandMessage &message) noexcept
{
  if (std::holds_alternative<TransferPauseMessage>(message)) {
    return MessageType::TransferPause;
  }
  if (std::holds_alternative<TransferResumeMessage>(message)) {
    return MessageType::TransferResume;
  }
  return MessageType::TransferCancel;
}

} // namespace relaydesk::transfer
