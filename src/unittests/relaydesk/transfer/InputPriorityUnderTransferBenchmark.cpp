// SPDX-FileCopyrightText: 2026 RelayDesk Contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

#include "relaydesk/transfer/TransferSender.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <deque>
#include <functional>
#include <utility>
#include <vector>

using namespace relaydesk::transfer;

namespace {

class DeterministicPriorityScheduler final
{
public:
  using Task = std::function<void(quint64)>;

  void enqueueInput(Task task)
  {
    m_input.push_back({m_tick, std::move(task)});
  }

  void enqueueTransfer(Task task)
  {
    m_transfer.push_back({m_tick, std::move(task)});
  }

  bool runOne()
  {
    auto &queue = m_input.empty() ? m_transfer : m_input;
    if (queue.empty()) {
      return false;
    }
    QueuedTask task = std::move(queue.front());
    queue.pop_front();
    task.callback(m_tick - task.enqueuedTick);
    ++m_tick;
    return true;
  }

  [[nodiscard]] qsizetype pendingTransferTasks() const noexcept
  {
    return static_cast<qsizetype>(m_transfer.size());
  }

private:
  struct QueuedTask
  {
    quint64 enqueuedTick = 0;
    Task callback;
  };

  std::deque<QueuedTask> m_input;
  std::deque<QueuedTask> m_transfer;
  quint64 m_tick = 0;
};

class ProbedSenderWorker final
{
public:
  explicit ProbedSenderWorker(TransferSenderRequest request) : sender(std::move(request))
  {
  }

  void workerStep()
  {
    const quint64 before = sender.bytesProduced();
    SenderFrameResult produced = sender.nextFrame();
    const quint64 after = sender.bytesProduced();
    if (after > before) {
      ++workerReadHashSteps;
      workerBytesProduced += after - before;
    }
    if (produced.ready()) {
      networkQueue.push_back(std::move(*produced.frame));
    } else if (produced.status == SenderFrameStatus::Failed) {
      failed = true;
      diagnostic = std::move(produced.diagnostic);
    }
  }

  // This models the network/control callback boundary: it can consume a
  // worker-produced frame, but cannot call nextFrame or access QFile/hash.
  void networkCallback()
  {
    const quint64 before = sender.bytesProduced();
    if (!networkQueue.empty()) {
      Frame frame = std::move(networkQueue.front());
      networkQueue.pop_front();
      callbackPayloadBytes += static_cast<quint64>(frame.payload.size());
      ++callbackFrames;
    }
    callbackMutatedSender = callbackMutatedSender || sender.bytesProduced() != before;
  }

  TransferSender sender;
  std::deque<Frame> networkQueue;
  quint64 workerReadHashSteps = 0;
  quint64 workerBytesProduced = 0;
  quint64 callbackFrames = 0;
  quint64 callbackPayloadBytes = 0;
  bool callbackMutatedSender = false;
  bool failed = false;
  QString diagnostic;
};

quint64 percentile95(std::vector<quint64> values)
{
  std::sort(values.begin(), values.end());
  const size_t index = (values.size() * 95 + 99) / 100 - 1;
  return values.at(index);
}

} // namespace

class InputPriorityUnderTransferBenchmark final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void inputTasksPreemptSaturatedTransferWorker();
};

void InputPriorityUnderTransferBenchmark::inputTasksPreemptSaturatedTransferWorker()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  constexpr quint64 logicalBytes = 256ULL * 1024ULL * 1024ULL;
  constexpr quint32 chunkBytes = 1U * 1024U * 1024U;
  const QString path = directory.filePath(QStringLiteral("priority-sparse.bin"));
  QFile source(path);
  QVERIFY(source.open(QIODevice::WriteOnly));
  QVERIFY(source.resize(static_cast<qint64>(logicalBytes)));
  source.close();
  QFileInfo info(path);
  info.refresh();

  ProbedSenderWorker worker({
      .transferId = TransferId::generate(),
      .source =
          {
              .canonicalSourcePath = info.canonicalFilePath(),
              .protocolCollisionKey = QStringLiteral("priority-sparse.bin"),
              .entry =
                  {
                      .id = FileId::generate(),
                      .relativeProtocolPath = QStringLiteral("priority-sparse.bin"),
                      .type = ManifestEntryType::File,
                      .size = logicalBytes,
                      .modifiedUtc = info.lastModified(),
                      .sha256 = QByteArray(kSha256Bytes, '\0'),
                  },
          },
      .streamId = 91,
      .chunkBytes = chunkBytes,
  });
  DeterministicPriorityScheduler scheduler;
  constexpr int transferSteps = 128;
  for (int step = 0; step < transferSteps; ++step) {
    scheduler.enqueueTransfer([&](quint64) { worker.workerStep(); });
  }
  QCOMPARE(scheduler.pendingTransferTasks(), transferSteps);

  std::vector<quint64> inputDispatchLatencyTicks;
  inputDispatchLatencyTicks.reserve(transferSteps);
  QElapsedTimer elapsed;
  elapsed.start();
  for (int step = 0; step < transferSteps; ++step) {
    scheduler.enqueueInput([&](quint64 latencyTicks) {
      inputDispatchLatencyTicks.push_back(latencyTicks);
      worker.networkCallback();
    });
    QVERIFY(scheduler.runOne()); // input/control always wins this dispatch
    QVERIFY(scheduler.runOne()); // one bounded worker read/hash step
  }
  scheduler.enqueueInput([&](quint64 latencyTicks) {
    inputDispatchLatencyTicks.push_back(latencyTicks);
    worker.networkCallback();
  });
  QVERIFY(scheduler.runOne());
  const qint64 elapsedMs = elapsed.elapsed();

  QVERIFY2(!worker.failed, qPrintable(worker.diagnostic));
  QVERIFY(worker.workerReadHashSteps > 0);
  QVERIFY(worker.workerBytesProduced > 0);
  QVERIFY(worker.callbackFrames > 0);
  QVERIFY(!worker.callbackMutatedSender);
  QCOMPARE(scheduler.pendingTransferTasks(), 0);
  QCOMPARE(inputDispatchLatencyTicks.size(), size_t{transferSteps + 1});
  const quint64 maximumLatencyTicks =
      *std::max_element(inputDispatchLatencyTicks.cbegin(), inputDispatchLatencyTicks.cend());
  const quint64 p95LatencyTicks = percentile95(inputDispatchLatencyTicks);
  QCOMPARE(maximumLatencyTicks, quint64{0});
  QCOMPARE(p95LatencyTicks, quint64{0});

  qInfo().nospace() << "TEST-003 transferTasks=" << transferSteps
                    << " workerReadHashSteps=" << worker.workerReadHashSteps
                    << " workerBytesProduced=" << worker.workerBytesProduced
                    << " callbackFrames=" << worker.callbackFrames
                    << " callbackPayloadBytes=" << worker.callbackPayloadBytes
                    << " inputP95DispatchTicks=" << p95LatencyTicks << " inputMaxDispatchTicks=" << maximumLatencyTicks
                    << " elapsedMs=" << elapsedMs;
}

QTEST_MAIN(InputPriorityUnderTransferBenchmark)

#include "InputPriorityUnderTransferBenchmark.moc"
