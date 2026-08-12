# 18 共享接口契约

> 这是多代理并行开发的接口边界。A1 必须先映射到真实 Deskflow 类型；A0 指定唯一 owner 后才能改变。代码示例是契约草案，不应未经核查直接成为第二套重复模型。

## 1. 标识类型

不要在各模块随意传 `QString`：

```cpp
struct DeviceId {
    QUuid value;
    bool operator==(const DeviceId&) const = default;
};

struct TransferId {
    QUuid value;
    bool operator==(const TransferId&) const = default;
};

struct FileId {
    QUuid value;
    bool operator==(const FileId&) const = default;
};
```

要求：

- 构造时校验非空；
- 日志默认只显示短摘要；
- wire 使用 16-byte；
- JSON/config 可使用标准 UUID 文本；
- 提供 `qHash`；
- 不混用 Deskflow screen/computer name 作为安全 ID。

Owner：A2（DeviceId），A6（TransferId/FileId）。

## 2. DeviceSnapshot

UI 只读：

```cpp
enum class DevicePresence {
    Offline,
    Discovered,
    Pairing,
    Online,
    TrustViolation
};

struct DeviceCapabilities {
    bool input = false;
    bool clipboardText = false;
    bool clipboardImage = false;
    bool fileV1 = false;
    bool folderV1 = false;
    bool resumeV1 = false;
};

struct DeviceSnapshot {
    DeviceId id;
    QString displayName;
    QString alias;
    QString platform;
    QString architecture;
    DevicePresence presence;
    bool trusted = false;
    bool autoAcceptFiles = false;
    int latencyMs = -1;
    QList<QHostAddress> addresses;
    DeviceCapabilities capabilities;
    QByteArray pinnedFingerprint;
    QDateTime lastSeenUtc;
};
```

规则：

- snapshot 可复制、不可让 UI 修改 service 内部对象；
- IP 不作为 identity；
- displayName 不可信，UI 转义；
- fingerprint 原始 bytes，仅高级 UI 格式化。

Owner：A2。

## 3. Pairing

```cpp
enum class PairingState {
    Idle,
    Requesting,
    ExchangingTranscript,
    AwaitingUserComparison,
    Confirming,
    Completed,
    Expired,
    Rejected,
    Failed
};

struct PairingSnapshot {
    QUuid pairingSessionId;
    DeviceSnapshot peer;
    PairingState state;
    QString sixDigitSas;
    QDateTime expiresAtUtc;
    int attemptsRemaining;
    QString errorMessageKey;
};

class IPairingService : public QObject {
    Q_OBJECT
public:
    virtual void startPairing(const DeviceId&) = 0;
    virtual void confirmMatchingSas(const QUuid& sessionId) = 0;
    virtual void submitDisplayedSas(
        const QUuid& sessionId, const QString& sixDigits) = 0;
    virtual void cancel(const QUuid& sessionId) = 0;
    virtual void revoke(const DeviceId&) = 0;

signals:
    void pairingChanged(PairingSnapshot);
};
```

`submitDisplayedSas` 必须本地比较；低熵数字不作为网络认证密钥。

Owner：A2。

## 4. Transfer manifest

```cpp
enum class ManifestEntryType : quint8 {
    File,
    Directory
};

struct ManifestEntry {
    FileId id;
    QString relativeProtocolPath;
    ManifestEntryType type;
    quint64 size = 0;
    QDateTime modifiedUtc;
    QByteArray sha256;
    quint32 flags = 0;
};

struct TransferManifestSummary {
    TransferId id;
    QString displayName;
    quint64 totalBytes = 0;
    quint64 fileCount = 0;
    quint64 directoryCount = 0;
    QByteArray canonicalSha256;
};
```

完整 manifest 不应复制到每个 UI snapshot。UI 只看 summary 和分页详情。

Owner：A6。

## 5. Transfer 状态

```cpp
enum class TransferDirection { Sending, Receiving };

enum class TransferState {
    Preparing,
    Offered,
    WaitingForAcceptance,
    Queued,
    Transferring,
    Paused,
    Interrupted,
    Resuming,
    Verifying,
    Committing,
    Completed,
    Rejected,
    Cancelling,
    Cancelled,
    Failed
};

struct TransferProgress {
    quint64 completedBytes = 0;
    quint64 totalBytes = 0;
    quint64 completedFiles = 0;
    quint64 totalFiles = 0;
    double bytesPerSecond = 0.0;
    std::optional<std::chrono::seconds> estimatedRemaining;
};

struct TransferSnapshot {
    TransferId id;
    DeviceId peerId;
    QString peerDisplayName;
    QString displayName;
    TransferDirection direction;
    TransferState state;
    TransferProgress progress;
    QString currentRelativeDisplayPath;
    QString errorMessageKey;
    int errorCode = 0;
    bool canPause = false;
    bool canResume = false;
    bool canCancel = false;
    bool canRetry = false;
    QDateTime createdUtc;
    QDateTime finishedUtc;
};
```

规则：

- progress total 永不因整数溢出变小；
- speed/ETA 由 service 计算，UI 不重复实现；
- current path 是安全显示值，不是落盘决策；
- terminal state 不回退；
- Interrupted 不是 Failed，允许重连；
- snapshot 发布最多 5 Hz，状态跃迁立即发布。

Owner：A6。

## 6. IncomingOffer

```cpp
struct IncomingOffer {
    TransferId id;
    DeviceId peerId;
    QString peerDisplayName;
    TransferManifestSummary summary;
    bool peerTrusted = false;
    bool mayAutoAccept = false;
    QString requestedConflictPolicy;
};
```

只有 trusted peer 才能到达普通 UI。非信任请求应在网络层拒绝。

Owner：A6，信任判断由 A2。

## 7. FileTransfer service

```cpp
struct SendOptions {
    QString conflictPolicy = QStringLiteral("auto-rename");
};

struct ReceiveOptions {
    QString destinationRoot;
    QString conflictPolicy = QStringLiteral("auto-rename");
    bool keepPartialOnFailure = true;
};

class IFileTransferService : public QObject {
    Q_OBJECT
public:
    virtual TransferId send(
        const DeviceId& target,
        const QList<QUrl>& localItems,
        const SendOptions&) = 0;

    virtual void accept(
        const TransferId&,
        const ReceiveOptions&) = 0;
    virtual void reject(const TransferId&, int reasonCode) = 0;
    virtual void pause(const TransferId&) = 0;
    virtual void resume(const TransferId&) = 0;
    virtual void cancel(const TransferId&, bool keepPartial) = 0;
    virtual void retry(const TransferId&) = 0;

    virtual QList<TransferSnapshot> activeTransfers() const = 0;

signals:
    void incomingOffer(IncomingOffer);
    void transferAdded(TransferSnapshot);
    void transferChanged(TransferSnapshot);
    void transferRemoved(TransferId);
};
```

Owner：A6。A3 不增加 socket/path 参数绕过 service。

## 8. Error contract

```cpp
enum class ErrorCategory {
    Protocol,
    Trust,
    Discovery,
    Path,
    Storage,
    Source,
    Network,
    State,
    Resource,
    Internal
};

struct ProductError {
    int code;
    ErrorCategory category;
    bool retryable;
    QString userMessageKey;
    QString diagnostic;
};
```

- code 稳定，可写历史；
- `userMessageKey` 本地化；
- diagnostic 不直接展示为 HTML；
- 远端 message 只作为脱敏诊断；
- 不用一个 `QString error` 贯穿所有层。

Owner：A2/A6 按区段，A0 管错误码注册表。

## 9. Settings

建议 schema：

```cpp
struct DiscoverySettings;
struct PairingSettings;
struct TransferSettings;
struct UiSettings;
```

规则：

- service 收到 immutable settings snapshot；
- 更新统一写入 Settings owner；
- 每个设置有默认值、范围和迁移；
- 不让各模块直接使用相同 QSettings key；
- secret/key 不放普通 QSettings；
- 产品 key 使用 `relaydesk/...` namespace。

Owner：A1/A2。

## 10. 平台接口

```cpp
class IPlatformFileSafety {
public:
    virtual ~IPlatformFileSafety() = default;
    virtual ProductError verifyReceiveRoot(const QString&) = 0;
    virtual ProductError verifyNoLinkTraversal(
        const QString& root,
        const QString& relative) = 0;
    virtual ProductError commitStagedFile(
        const QString& staging,
        const QString& destination,
        bool replace) = 0;
};

class IPlatformPermissions {
public:
    virtual PermissionSnapshot current() const = 0;
    virtual void openSystemSettings(PermissionKind) = 0;
};
```

Windows/macOS 只实现接口，不各写一套传输状态机。

Owner：A4/A5，实现契约由 A6/A3 定义。

## 11. 变更规则

更改共享契约必须：

1. 任务 ID；
2. owner 提交；
3. 编译受影响消费者；
4. 兼容/迁移说明；
5. A0 更新本文件；
6. 不由两个代理各自创建相似类型。
