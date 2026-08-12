/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/widgets/TransferHistoryDetailsDialog.h"

#include "relaydesk/i18n/ProductStrings.h"

#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include <utility>

namespace deskflow::relaydesk::widgets {
namespace {

using i18n::Text;
using namespace ::relaydesk::transfer;

QString directionText(HistoryDirection direction)
{
  return i18n::translate(
      direction == HistoryDirection::Sending ? Text::TransferDirectionSending : Text::TransferDirectionReceiving
  );
}

QString statusText(HistoryStatus status)
{
  switch (status) {
  case HistoryStatus::Completed:
    return i18n::translate(Text::TransferStateCompleted);
  case HistoryStatus::Rejected:
    return i18n::translate(Text::TransferStateRejected);
  case HistoryStatus::Cancelled:
    return i18n::translate(Text::TransferStateCanceled);
  case HistoryStatus::Failed:
    return i18n::translate(Text::TransferStateFailed);
  }
  return i18n::translate(Text::TransferStateFailed);
}

QString safeErrorText(const QString &errorMessageKey)
{
  if (errorMessageKey.endsWith(QStringLiteral("disk_full")))
    return i18n::translate(Text::TransferErrorDiskFull);
  if (errorMessageKey.endsWith(QStringLiteral("unsafe_path")) ||
      errorMessageKey.endsWith(QStringLiteral("path_invalid"))) {
    return i18n::translate(Text::TransferErrorUnsafePath);
  }
  if (errorMessageKey.endsWith(QStringLiteral("unreadable")) ||
      errorMessageKey.endsWith(QStringLiteral("source_changed"))) {
    return i18n::translate(Text::TransferErrorUnreadable);
  }
  if (errorMessageKey.endsWith(QStringLiteral("connection_lost")))
    return i18n::translate(Text::TransferErrorConnectionLost);
  if (errorMessageKey.endsWith(QStringLiteral("checksum_mismatch")) ||
      errorMessageKey.endsWith(QStringLiteral("hash_mismatch"))) {
    return i18n::translate(Text::TransferErrorChecksumMismatch);
  }
  return i18n::translate(Text::TransferErrorUnknown);
}

QString formattedSize(quint64 bytes)
{
  return bytes <= static_cast<quint64>(std::numeric_limits<qint64>::max())
             ? QLocale().formattedDataSize(static_cast<qint64>(bytes))
             : QLocale().toString(bytes) + QStringLiteral(" B");
}

int pluralCount(quint64 count)
{
  return static_cast<int>(std::min<quint64>(count, static_cast<quint64>(std::numeric_limits<int>::max())));
}

QString itemCountText(quint64 count)
{
  return i18n::translatePlural(Text::TransferHistoryItems, pluralCount(count)).arg(QLocale().toString(count));
}

QString durationText(const QDateTime &startedUtc, const QDateTime &finishedUtc)
{
  const auto milliseconds = std::max<qint64>(0, startedUtc.msecsTo(finishedUtc));
  const auto seconds = static_cast<quint64>(milliseconds / 1000 + (milliseconds % 1000 != 0 ? 1 : 0));
  if (seconds < 60) {
    return i18n::translatePlural(Text::TransferHistoryDurationSeconds, pluralCount(seconds))
        .arg(QLocale().toString(seconds));
  }
  if (seconds < 3600) {
    const auto minutes = (seconds + 59) / 60;
    return i18n::translatePlural(Text::TransferHistoryDurationMinutes, pluralCount(minutes))
        .arg(QLocale().toString(minutes));
  }
  const auto hours = (seconds + 3599) / 3600;
  return i18n::translatePlural(Text::TransferHistoryDurationHours, pluralCount(hours))
      .arg(QLocale().toString(hours));
}

void configureValue(QLabel *value, const QString &objectName)
{
  value->setObjectName(objectName);
  value->setTextInteractionFlags(Qt::TextSelectableByMouse);
  value->setWordWrap(true);
}

} // namespace

TransferHistoryDetailsDialog::TransferHistoryDetailsDialog(TransferHistoryRecord record, QWidget *parent)
    : QDialog(parent), m_record(std::move(record))
{
  setObjectName(QStringLiteral("relaydeskTransferHistoryDetailsDialog"));
  setWindowModality(Qt::NonModal);
  setSizeGripEnabled(true);
  setMinimumWidth(420);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(18, 18, 18, 14);
  layout->setSpacing(14);
  auto *form = new QFormLayout();
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  form->setRowWrapPolicy(QFormLayout::WrapLongRows);
  form->setHorizontalSpacing(18);
  form->setVerticalSpacing(9);

  const auto addRow = [this, form](
                          QLabel *&caption, QLabel *&value, const QString &captionObjectName,
                          const QString &valueObjectName
                      ) {
    caption = new QLabel(this);
    caption->setObjectName(captionObjectName);
    value = new QLabel(this);
    configureValue(value, valueObjectName);
    form->addRow(caption, value);
  };
  addRow(m_nameCaption, m_nameValue, QStringLiteral("relaydeskTransferHistoryNameCaption"),
         QStringLiteral("relaydeskTransferHistoryNameValue"));
  addRow(m_peerCaption, m_peerValue, QStringLiteral("relaydeskTransferHistoryPeerCaption"),
         QStringLiteral("relaydeskTransferHistoryPeerValue"));
  addRow(m_directionCaption, m_directionValue, QStringLiteral("relaydeskTransferHistoryDirectionCaption"),
         QStringLiteral("relaydeskTransferHistoryDirectionValue"));
  addRow(m_statusCaption, m_statusValue, QStringLiteral("relaydeskTransferHistoryStatusCaption"),
         QStringLiteral("relaydeskTransferHistoryStatusValue"));
  addRow(m_itemsCaption, m_itemsValue, QStringLiteral("relaydeskTransferHistoryItemsCaption"),
         QStringLiteral("relaydeskTransferHistoryItemsValue"));
  addRow(m_sizeCaption, m_sizeValue, QStringLiteral("relaydeskTransferHistorySizeCaption"),
         QStringLiteral("relaydeskTransferHistorySizeValue"));
  addRow(m_startedCaption, m_startedValue, QStringLiteral("relaydeskTransferHistoryStartedCaption"),
         QStringLiteral("relaydeskTransferHistoryStartedValue"));
  addRow(m_finishedCaption, m_finishedValue, QStringLiteral("relaydeskTransferHistoryFinishedCaption"),
         QStringLiteral("relaydeskTransferHistoryFinishedValue"));
  addRow(m_durationCaption, m_durationValue, QStringLiteral("relaydeskTransferHistoryDurationCaption"),
         QStringLiteral("relaydeskTransferHistoryDurationValue"));
  addRow(m_errorCaption, m_errorValue, QStringLiteral("relaydeskTransferHistoryErrorCaption"),
         QStringLiteral("relaydeskTransferHistoryErrorValue"));
  layout->addLayout(form);

  auto *buttons = new QDialogButtonBox(this);
  m_closeButton = new QPushButton(buttons);
  m_closeButton->setObjectName(QStringLiteral("relaydeskTransferHistoryCloseButton"));
  m_closeButton->setDefault(true);
  buttons->addButton(m_closeButton, QDialogButtonBox::RejectRole);
  connect(m_closeButton, &QPushButton::clicked, this, &QDialog::reject);
  layout->addWidget(buttons);

  updateText();
}

const TransferHistoryRecord &TransferHistoryDetailsDialog::record() const noexcept
{
  return m_record;
}

void TransferHistoryDetailsDialog::changeEvent(QEvent *event)
{
  QDialog::changeEvent(event);
  if (event->type() == QEvent::LanguageChange)
    updateText();
}

void TransferHistoryDetailsDialog::updateText()
{
  setWindowTitle(i18n::translate(Text::TransferHistoryDetailsTitle));
  setAccessibleName(windowTitle());

  m_nameCaption->setText(i18n::translate(Text::TransferHistoryNameLabel));
  m_peerCaption->setText(i18n::translate(Text::TransferHistoryPeerLabel));
  m_directionCaption->setText(i18n::translate(Text::TransferHistoryDirectionLabel));
  m_statusCaption->setText(i18n::translate(Text::TransferHistoryStatusLabel));
  m_itemsCaption->setText(i18n::translate(Text::TransferHistoryItemsLabel));
  m_sizeCaption->setText(i18n::translate(Text::TransferHistorySizeLabel));
  m_startedCaption->setText(i18n::translate(Text::TransferHistoryStartedLabel));
  m_finishedCaption->setText(i18n::translate(Text::TransferHistoryFinishedLabel));
  m_durationCaption->setText(i18n::translate(Text::TransferHistoryDurationLabel));
  m_errorCaption->setText(i18n::translate(Text::TransferHistoryErrorLabel));

  m_nameValue->setText(m_record.displayName);
  m_peerValue->setText(
      m_record.peerDisplayName.isEmpty() ? i18n::translate(Text::TransferIncomingUnknownDevice)
                                         : m_record.peerDisplayName
  );
  m_directionValue->setText(directionText(m_record.direction));
  m_statusValue->setText(statusText(m_record.status));
  m_itemsValue->setText(itemCountText(m_record.fileCount));
  m_sizeValue->setText(formattedSize(m_record.totalBytes));
  m_startedValue->setText(QLocale().toString(m_record.startedUtc.toLocalTime(), QLocale::ShortFormat));
  m_finishedValue->setText(QLocale().toString(m_record.finishedUtc.toLocalTime(), QLocale::ShortFormat));
  m_durationValue->setText(durationText(m_record.startedUtc, m_record.finishedUtc));

  const auto failed = m_record.status == HistoryStatus::Failed;
  m_errorCaption->setVisible(failed);
  m_errorValue->setVisible(failed);
  m_errorValue->setText(failed ? safeErrorText(m_record.errorMessageKey) : QString());

  const auto setAccessibleLabel = [](QLabel *value, const QLabel *caption) {
    value->setAccessibleName(caption->text());
  };
  setAccessibleLabel(m_nameValue, m_nameCaption);
  setAccessibleLabel(m_peerValue, m_peerCaption);
  setAccessibleLabel(m_directionValue, m_directionCaption);
  setAccessibleLabel(m_statusValue, m_statusCaption);
  setAccessibleLabel(m_itemsValue, m_itemsCaption);
  setAccessibleLabel(m_sizeValue, m_sizeCaption);
  setAccessibleLabel(m_startedValue, m_startedCaption);
  setAccessibleLabel(m_finishedValue, m_finishedCaption);
  setAccessibleLabel(m_durationValue, m_durationCaption);
  setAccessibleLabel(m_errorValue, m_errorCaption);

  m_closeButton->setText(i18n::translate(Text::TransferActionClose));
  m_closeButton->setAccessibleName(m_closeButton->text());
}

} // namespace deskflow::relaydesk::widgets
