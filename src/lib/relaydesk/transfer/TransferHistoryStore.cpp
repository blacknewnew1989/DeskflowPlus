// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/TransferHistoryStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

namespace relaydesk::transfer {
namespace {

constexpr qsizetype kMaximumDisplayNameUtf8Bytes = 4'096;
constexpr qsizetype kMaximumErrorKeyUtf8Bytes = 512;

TransferHistoryOperationResult operationFailure(TransferHistoryError error, QString diagnostic)
{
  return {.error = error, .diagnostic = std::move(diagnostic)};
}

QString directionName(HistoryDirection direction)
{
  switch (direction) {
  case HistoryDirection::Sending:
    return QStringLiteral("send");
  case HistoryDirection::Receiving:
    return QStringLiteral("receive");
  }
  return {};
}

std::optional<HistoryDirection> parseDirection(const QString &direction)
{
  if (direction == QStringLiteral("send")) {
    return HistoryDirection::Sending;
  }
  if (direction == QStringLiteral("receive")) {
    return HistoryDirection::Receiving;
  }
  return std::nullopt;
}

QString statusName(HistoryStatus status)
{
  switch (status) {
  case HistoryStatus::Completed:
    return QStringLiteral("completed");
  case HistoryStatus::Rejected:
    return QStringLiteral("rejected");
  case HistoryStatus::Cancelled:
    return QStringLiteral("cancelled");
  case HistoryStatus::Failed:
    return QStringLiteral("failed");
  }
  return {};
}

std::optional<HistoryStatus> parseStatus(const QString &status)
{
  if (status == QStringLiteral("completed")) {
    return HistoryStatus::Completed;
  }
  if (status == QStringLiteral("rejected")) {
    return HistoryStatus::Rejected;
  }
  if (status == QStringLiteral("cancelled")) {
    return HistoryStatus::Cancelled;
  }
  if (status == QStringLiteral("failed")) {
    return HistoryStatus::Failed;
  }
  return std::nullopt;
}

bool validLimits(const TransferHistoryLimits &limits)
{
  return limits.maximumEntries > 0 && limits.maximumAge.count() > 0 && limits.maximumFileBytes > 0 &&
         limits.maximumLineBytes > 0 && static_cast<quint64>(limits.maximumLineBytes) <= limits.maximumFileBytes;
}

bool validRecord(const TransferHistoryRecord &record)
{
  if (record.transferId.isNull() || record.peerDeviceId.value().isNull() || record.displayName.isEmpty() ||
      record.displayName.toUtf8().size() > kMaximumDisplayNameUtf8Bytes ||
      record.peerDisplayName.toUtf8().size() > kMaximumDisplayNameUtf8Bytes || !record.startedUtc.isValid() ||
      !record.finishedUtc.isValid() || record.startedUtc.toMSecsSinceEpoch() <= 0 ||
      record.finishedUtc < record.startedUtc || directionName(record.direction).isEmpty() ||
      statusName(record.status).isEmpty() || record.errorMessageKey.toUtf8().size() > kMaximumErrorKeyUtf8Bytes) {
    return false;
  }
  if (record.totalBytes > static_cast<quint64>(std::numeric_limits<qint64>::max()) ||
      record.fileCount > static_cast<quint64>(std::numeric_limits<qint64>::max())) {
    return false;
  }
  if (record.status == HistoryStatus::Completed) {
    return record.errorCode == 0 && record.errorMessageKey.isEmpty();
  }
  if (record.status == HistoryStatus::Failed) {
    return record.errorCode > 0 && !record.errorMessageKey.isEmpty();
  }
  return record.errorCode >= 0;
}

QByteArray encodeRecord(const TransferHistoryRecord &record)
{
  const QJsonObject object{
      {QStringLiteral("schemaVersion"), static_cast<qint64>(kTransferHistorySchemaVersion)},
      {QStringLiteral("transferId"), record.transferId.toString(QUuid::WithoutBraces)},
      {QStringLiteral("peerDeviceId"), record.peerDeviceId.toString()},
      {QStringLiteral("peerDisplayName"), record.peerDisplayName},
      {QStringLiteral("displayName"), record.displayName},
      {QStringLiteral("direction"), directionName(record.direction)},
      {QStringLiteral("fileCount"), static_cast<qint64>(record.fileCount)},
      {QStringLiteral("totalBytes"), static_cast<qint64>(record.totalBytes)},
      {QStringLiteral("startedAtMs"), record.startedUtc.toMSecsSinceEpoch()},
      {QStringLiteral("finishedAtMs"), record.finishedUtc.toMSecsSinceEpoch()},
      {QStringLiteral("status"), statusName(record.status)},
      {QStringLiteral("errorCode"), record.errorCode},
      {QStringLiteral("errorMessageKey"), record.errorMessageKey},
  };
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

std::optional<qint64> integerField(const QJsonObject &object, const QString &name)
{
  const auto value = object.value(name);
  if (!value.isDouble()) {
    return std::nullopt;
  }
  constexpr qint64 kInvalid = std::numeric_limits<qint64>::min();
  const qint64 number = value.toInteger(kInvalid);
  if (number == kInvalid) {
    return std::nullopt;
  }
  return number;
}

std::optional<TransferHistoryRecord> decodeRecord(QByteArrayView line)
{
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(line.toByteArray(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    return std::nullopt;
  }
  const QJsonObject object = document.object();
  const QSet<QString> required{
      QStringLiteral("schemaVersion"),   QStringLiteral("transferId"),  QStringLiteral("peerDeviceId"),
      QStringLiteral("peerDisplayName"), QStringLiteral("displayName"), QStringLiteral("direction"),
      QStringLiteral("fileCount"),       QStringLiteral("totalBytes"),  QStringLiteral("startedAtMs"),
      QStringLiteral("finishedAtMs"),    QStringLiteral("status"),      QStringLiteral("errorCode"),
      QStringLiteral("errorMessageKey"),
  };
  if (object.keys().size() != required.size()) {
    return std::nullopt;
  }
  for (const auto &name : required) {
    if (!object.contains(name)) {
      return std::nullopt;
    }
  }

  const auto schemaVersion = integerField(object, QStringLiteral("schemaVersion"));
  const auto fileCount = integerField(object, QStringLiteral("fileCount"));
  const auto totalBytes = integerField(object, QStringLiteral("totalBytes"));
  const auto startedAt = integerField(object, QStringLiteral("startedAtMs"));
  const auto finishedAt = integerField(object, QStringLiteral("finishedAtMs"));
  const auto errorCode = integerField(object, QStringLiteral("errorCode"));
  if (!schemaVersion.has_value() || *schemaVersion != static_cast<qint64>(kTransferHistorySchemaVersion) ||
      !fileCount.has_value() || *fileCount < 0 || !totalBytes.has_value() || *totalBytes < 0 ||
      !startedAt.has_value() || *startedAt <= 0 || !finishedAt.has_value() || *finishedAt <= 0 ||
      !errorCode.has_value() || *errorCode < 0 || *errorCode > std::numeric_limits<int>::max()) {
    return std::nullopt;
  }

  const auto transferId = QUuid(object.value(QStringLiteral("transferId")).toString());
  const auto peerDeviceId =
      deskflow::relaydesk::DeviceId::fromString(object.value(QStringLiteral("peerDeviceId")).toString());
  const auto direction = parseDirection(object.value(QStringLiteral("direction")).toString());
  const auto status = parseStatus(object.value(QStringLiteral("status")).toString());
  if (transferId.isNull() || !peerDeviceId.has_value() || !direction.has_value() || !status.has_value()) {
    return std::nullopt;
  }

  TransferHistoryRecord record{
      .transferId = transferId,
      .peerDeviceId = *peerDeviceId,
      .peerDisplayName = object.value(QStringLiteral("peerDisplayName")).toString(),
      .displayName = object.value(QStringLiteral("displayName")).toString(),
      .direction = *direction,
      .fileCount = static_cast<quint64>(*fileCount),
      .totalBytes = static_cast<quint64>(*totalBytes),
      .startedUtc = QDateTime::fromMSecsSinceEpoch(*startedAt, Qt::UTC),
      .finishedUtc = QDateTime::fromMSecsSinceEpoch(*finishedAt, Qt::UTC),
      .status = *status,
      .errorCode = static_cast<int>(*errorCode),
      .errorMessageKey = object.value(QStringLiteral("errorMessageKey")).toString(),
  };
  return validRecord(record) ? std::optional<TransferHistoryRecord>{std::move(record)} : std::nullopt;
}

bool newestFirst(const TransferHistoryRecord &left, const TransferHistoryRecord &right)
{
  if (left.finishedUtc != right.finishedUtc) {
    return left.finishedUtc > right.finishedUtc;
  }
  return left.transferId.toRfc4122() < right.transferId.toRfc4122();
}

} // namespace

struct TransferHistoryStore::LoadAllResult
{
  QList<TransferHistoryRecord> records;
  QList<TransferHistoryIssue> issues;
  TransferHistoryError error = TransferHistoryError::None;
  QString diagnostic;
};

TransferHistoryStore::TransferHistoryStore(QString historyPath, TransferHistoryLimits limits, Clock clock)
    : m_historyPath(QDir::cleanPath(std::move(historyPath))),
      m_limits(limits),
      m_clock(clock ? std::move(clock) : [] { return QDateTime::currentDateTimeUtc(); })
{
}

TransferHistoryOperationResult TransferHistoryStore::append(const TransferHistoryRecord &record) const
{
  if (!QFileInfo(m_historyPath).isAbsolute()) {
    return operationFailure(TransferHistoryError::InvalidStorePath, QStringLiteral("history path must be absolute"));
  }
  if (!validLimits(m_limits)) {
    return operationFailure(TransferHistoryError::InvalidLimits, QStringLiteral("history limits are invalid"));
  }
  if (!validRecord(record)) {
    return operationFailure(TransferHistoryError::InvalidRecord, QStringLiteral("history record is invalid"));
  }

  LoadAllResult loaded = loadAll();
  if (loaded.error != TransferHistoryError::None) {
    return operationFailure(loaded.error, std::move(loaded.diagnostic));
  }
  loaded.records.erase(
      std::remove_if(
          loaded.records.begin(), loaded.records.end(),
          [&record](const TransferHistoryRecord &existing) { return existing.transferId == record.transferId; }
      ),
      loaded.records.end()
  );
  loaded.records.append(record);

  const QDateTime now = m_clock().toUTC();
  const QDateTime cutoff = now.isValid() ? now.addDays(-m_limits.maximumAge.count()) : QDateTime{};
  if (cutoff.isValid()) {
    loaded.records.erase(
        std::remove_if(
            loaded.records.begin(), loaded.records.end(),
            [&cutoff](const TransferHistoryRecord &existing) { return existing.finishedUtc < cutoff; }
        ),
        loaded.records.end()
    );
  }
  std::sort(loaded.records.begin(), loaded.records.end(), newestFirst);
  if (loaded.records.size() > m_limits.maximumEntries) {
    loaded.records.erase(loaded.records.begin() + m_limits.maximumEntries, loaded.records.end());
  }
  return writeAll(loaded.records);
}

TransferHistoryPageResult TransferHistoryStore::page(qsizetype offset, qsizetype limit) const
{
  if (!QFileInfo(m_historyPath).isAbsolute()) {
    return {
        .error = TransferHistoryError::InvalidStorePath,
        .diagnostic = QStringLiteral("history path must be absolute"),
    };
  }
  if (!validLimits(m_limits) || offset < 0 || limit <= 0 || limit > m_limits.maximumEntries) {
    return {
        .error = TransferHistoryError::InvalidLimits,
        .diagnostic = QStringLiteral("history limits or page range are invalid"),
    };
  }
  LoadAllResult loaded = loadAll();
  if (loaded.error != TransferHistoryError::None) {
    return {.error = loaded.error, .diagnostic = std::move(loaded.diagnostic)};
  }

  const QDateTime now = m_clock().toUTC();
  const QDateTime cutoff = now.isValid() ? now.addDays(-m_limits.maximumAge.count()) : QDateTime{};
  if (cutoff.isValid()) {
    loaded.records.erase(
        std::remove_if(
            loaded.records.begin(), loaded.records.end(),
            [&cutoff](const TransferHistoryRecord &existing) { return existing.finishedUtc < cutoff; }
        ),
        loaded.records.end()
    );
  }
  std::sort(loaded.records.begin(), loaded.records.end(), newestFirst);
  const qsizetype total = loaded.records.size();
  const qsizetype begin = std::min(offset, total);
  const qsizetype end = std::min(total, begin + limit);
  return {
      .page =
          TransferHistoryPage{
              .records = loaded.records.sliced(begin, end - begin),
              .issues = std::move(loaded.issues),
              .totalValidEntries = total,
          },
  };
}

TransferHistoryOperationResult TransferHistoryStore::clear() const
{
  if (!QFileInfo(m_historyPath).isAbsolute()) {
    return operationFailure(TransferHistoryError::InvalidStorePath, QStringLiteral("history path must be absolute"));
  }
  if (!QFileInfo::exists(m_historyPath)) {
    return {};
  }
  QFile history(m_historyPath);
  if (!history.remove()) {
    return operationFailure(TransferHistoryError::RemoveFailed, history.errorString());
  }
  return {};
}

QString TransferHistoryStore::historyPath() const
{
  return m_historyPath;
}

TransferHistoryStore::LoadAllResult TransferHistoryStore::loadAll() const
{
  LoadAllResult result;
  QFile input(m_historyPath);
  if (!input.exists()) {
    return result;
  }
  if (m_limits.maximumFileBytes == 0 || input.size() < 0 ||
      static_cast<quint64>(input.size()) > m_limits.maximumFileBytes) {
    result.error = TransferHistoryError::FileTooLarge;
    result.diagnostic = QStringLiteral("history file exceeds the configured limit");
    return result;
  }
  if (!input.open(QIODevice::ReadOnly)) {
    result.error = TransferHistoryError::OpenFailed;
    result.diagnostic = input.errorString();
    return result;
  }
  const QByteArray contents = input.readAll();
  if (input.error() != QFileDevice::NoError) {
    result.error = TransferHistoryError::ReadFailed;
    result.diagnostic = input.errorString();
    return result;
  }

  const QList<QByteArray> lines = contents.split('\n');
  result.records.reserve(lines.size());
  for (qsizetype index = 0; index < lines.size(); ++index) {
    QByteArray line = lines.at(index);
    if (line.endsWith('\r')) {
      line.chop(1);
    }
    if (line.isEmpty()) {
      continue;
    }
    if (line.size() > m_limits.maximumLineBytes) {
      result.issues.append({.line = index + 1, .diagnostic = QStringLiteral("history row exceeds the limit")});
      continue;
    }
    auto record = decodeRecord(line);
    if (!record.has_value()) {
      result.issues.append({.line = index + 1, .diagnostic = QStringLiteral("history row is invalid")});
      continue;
    }
    result.records.append(std::move(*record));
  }
  return result;
}

TransferHistoryOperationResult TransferHistoryStore::writeAll(const QList<TransferHistoryRecord> &records) const
{
  QByteArray contents;
  for (const auto &record : records) {
    const QByteArray line = encodeRecord(record);
    if (line.size() > m_limits.maximumLineBytes) {
      return operationFailure(TransferHistoryError::InvalidRecord, QStringLiteral("encoded history row is too large"));
    }
    if (static_cast<quint64>(contents.size()) + static_cast<quint64>(line.size()) + 1 > m_limits.maximumFileBytes) {
      return operationFailure(TransferHistoryError::FileTooLarge, QStringLiteral("encoded history exceeds the limit"));
    }
    contents.append(line);
    contents.append('\n');
  }

  const QString directory = QFileInfo(m_historyPath).absolutePath();
  if (!QDir().mkpath(directory)) {
    return operationFailure(
        TransferHistoryError::DirectoryCreateFailed, QStringLiteral("could not create the history directory")
    );
  }
  QSaveFile output(m_historyPath);
  output.setDirectWriteFallback(false);
  if (!output.open(QIODevice::WriteOnly)) {
    return operationFailure(TransferHistoryError::OpenFailed, output.errorString());
  }
  if (output.write(contents) != contents.size()) {
    output.cancelWriting();
    return operationFailure(TransferHistoryError::WriteFailed, output.errorString());
  }
  if (!output.commit()) {
    return operationFailure(TransferHistoryError::CommitFailed, output.errorString());
  }
  return {};
}

} // namespace relaydesk::transfer
