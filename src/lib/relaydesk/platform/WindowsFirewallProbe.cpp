/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/platform/WindowsFirewallProbe.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QProcess>
#include <QStringList>
#include <QtConcurrentRun>

#include <algorithm>
#include <utility>

#if defined(Q_OS_WIN)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <initguid.h>
#include <iphlpapi.h>
#include <netfw.h>
#include <objbase.h>
#include <oleauto.h>
#endif

namespace deskflow::relaydesk {
namespace {

PermissionProbeEntry firewallEntry(const WindowsFirewallInspection &inspection)
{
  PermissionProbeEntry entry{
      .kind = PermissionKind::WindowsFirewall,
      .diagnostic = inspection.firewallDiagnostic,
  };
  switch (inspection.firewall) {
  case WindowsFirewallRuleStatus::Allowed:
    entry.state = PermissionState::Granted;
    break;
  case WindowsFirewallRuleStatus::Blocked:
    entry.state = PermissionState::Denied;
    entry.errorCode = PermissionErrorCode::WindowsFirewallBlocked;
    entry.canOpenSettings = true;
    break;
  case WindowsFirewallRuleStatus::MissingAllowRule:
    entry.state = PermissionState::NeedsAction;
    entry.errorCode = PermissionErrorCode::WindowsFirewallBlocked;
    entry.canOpenSettings = true;
    break;
  case WindowsFirewallRuleStatus::NotRequired:
    entry.state = PermissionState::NotRequired;
    break;
  case WindowsFirewallRuleStatus::Unavailable:
    entry.state = PermissionState::Unknown;
    entry.errorCode = PermissionErrorCode::ProbeUnavailable;
#if defined(Q_OS_WIN)
    entry.canOpenSettings = true;
#endif
    break;
  }
  return entry;
}

PermissionProbeEntry listeningPortEntry(const WindowsFirewallInspection &inspection)
{
  PermissionProbeEntry entry{
      .kind = PermissionKind::WindowsListeningPort,
      .diagnostic = inspection.listeningPortDiagnostic,
  };
  switch (inspection.listeningPort) {
  case WindowsListeningPortStatus::Listening:
    entry.state = PermissionState::Granted;
    break;
  case WindowsListeningPortStatus::NotListening:
    entry.state = PermissionState::NeedsAction;
    entry.errorCode = PermissionErrorCode::WindowsPortUnavailable;
    break;
  case WindowsListeningPortStatus::NotRequired:
    entry.state = PermissionState::NotRequired;
    break;
  case WindowsListeningPortStatus::Unavailable:
    entry.state = PermissionState::Unknown;
    entry.errorCode = PermissionErrorCode::ProbeUnavailable;
    break;
  }
  return entry;
}

#if defined(Q_OS_WIN)

// INetFwRule::Protocol uses 256 for Any. Older MinGW icftypes.h exposes only
// the TCP/UDP enum names even though the COM property still returns 256.
constexpr LONG kNetFwIpProtocolAny = 256;

template <typename T> class ComPtr final
{
public:
  ComPtr() = default;
  ~ComPtr()
  {
    reset();
  }
  ComPtr(const ComPtr &) = delete;
  ComPtr &operator=(const ComPtr &) = delete;

  [[nodiscard]] T *get() const noexcept
  {
    return m_value;
  }
  [[nodiscard]] T **put() noexcept
  {
    reset();
    return &m_value;
  }
  T *operator->() const noexcept
  {
    return m_value;
  }
  void reset(T *value = nullptr) noexcept
  {
    if (m_value != nullptr) {
      m_value->Release();
    }
    m_value = value;
  }

private:
  T *m_value = nullptr;
};

class ComApartment final
{
public:
  ComApartment()
  {
    m_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    m_uninitialize = SUCCEEDED(m_result);
  }
  ~ComApartment()
  {
    if (m_uninitialize) {
      CoUninitialize();
    }
  }
  [[nodiscard]] bool available() const noexcept
  {
    return SUCCEEDED(m_result) || m_result == RPC_E_CHANGED_MODE;
  }
  [[nodiscard]] HRESULT result() const noexcept
  {
    return m_result;
  }

private:
  HRESULT m_result = E_FAIL;
  bool m_uninitialize = false;
};

QString hexResult(HRESULT result)
{
  return QStringLiteral("0x%1").arg(quint32(result), 8, 16, QLatin1Char('0'));
}

QString expandedWindowsPath(QString path)
{
  if (path.isEmpty()) {
    return {};
  }
  const auto native = QDir::toNativeSeparators(path);
  const DWORD needed = ExpandEnvironmentStringsW(
      reinterpret_cast<LPCWSTR>(native.utf16()), nullptr, 0
  );
  if (needed > 1) {
    std::wstring expanded(needed, L'\0');
    if (ExpandEnvironmentStringsW(
            reinterpret_cast<LPCWSTR>(native.utf16()), expanded.data(), needed
        ) > 0) {
      path = QString::fromWCharArray(expanded.c_str());
    }
  }
  return QDir::cleanPath(QFileInfo(path).absoluteFilePath()).toCaseFolded();
}

QString fromBstr(BSTR value)
{
  return value == nullptr ? QString() : QString::fromWCharArray(value, int(SysStringLen(value)));
}

bool portsCover(const QString &specification, const QList<quint16> &ports)
{
  if (ports.isEmpty()) {
    return true;
  }
  const auto trimmed = specification.trimmed();
  if (trimmed.isEmpty() || trimmed == QStringLiteral("*")) {
    return true;
  }

  for (const auto port : ports) {
    bool covered = false;
    for (const auto &component : trimmed.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
      const auto token = component.trimmed();
      bool singleOk = false;
      const auto single = token.toUInt(&singleOk);
      if (singleOk && single == port) {
        covered = true;
        break;
      }
      const auto bounds = token.split(QLatin1Char('-'));
      if (bounds.size() == 2) {
        bool lowerOk = false;
        bool upperOk = false;
        const auto lower = bounds.at(0).trimmed().toUInt(&lowerOk);
        const auto upper = bounds.at(1).trimmed().toUInt(&upperOk);
        if (lowerOk && upperOk && lower <= port && port <= upper) {
          covered = true;
          break;
        }
      }
    }
    if (!covered) {
      return false;
    }
  }
  return true;
}

WindowsFirewallRuleStatus inspectFirewallRules(
    const WindowsFirewallProbeRequest &request, QString *diagnostic
)
{
  ComApartment apartment;
  if (!apartment.available()) {
    *diagnostic = QStringLiteral("COM initialization failed: %1").arg(hexResult(apartment.result()));
    return WindowsFirewallRuleStatus::Unavailable;
  }

  ComPtr<INetFwPolicy2> policy;
  const auto created = CoCreateInstance(
      CLSID_NetFwPolicy2, nullptr, CLSCTX_INPROC_SERVER, IID_INetFwPolicy2,
      reinterpret_cast<void **>(policy.put())
  );
  if (FAILED(created)) {
    *diagnostic = QStringLiteral("Windows Firewall policy is unavailable: %1").arg(hexResult(created));
    return WindowsFirewallRuleStatus::Unavailable;
  }

  LONG currentProfiles = 0;
  const auto profilesResult = policy->get_CurrentProfileTypes(&currentProfiles);
  if (FAILED(profilesResult)) {
    *diagnostic = QStringLiteral("could not read active firewall profiles: %1").arg(hexResult(profilesResult));
    return WindowsFirewallRuleStatus::Unavailable;
  }
  const LONG knownProfiles = NET_FW_PROFILE2_DOMAIN | NET_FW_PROFILE2_PRIVATE | NET_FW_PROFILE2_PUBLIC;
  currentProfiles &= knownProfiles;
  if (currentProfiles == 0) {
    *diagnostic = QStringLiteral("Windows reported no active firewall profile");
    return WindowsFirewallRuleStatus::Unavailable;
  }

  LONG enabledProfiles = 0;
  for (const LONG profile : {LONG(NET_FW_PROFILE2_DOMAIN), LONG(NET_FW_PROFILE2_PRIVATE),
                             LONG(NET_FW_PROFILE2_PUBLIC)}) {
    if ((currentProfiles & profile) == 0) {
      continue;
    }
    VARIANT_BOOL enabled = VARIANT_FALSE;
    const auto enabledResult = policy->get_FirewallEnabled(NET_FW_PROFILE_TYPE2(profile), &enabled);
    if (FAILED(enabledResult)) {
      *diagnostic = QStringLiteral("could not read firewall profile state: %1").arg(hexResult(enabledResult));
      return WindowsFirewallRuleStatus::Unavailable;
    }
    if (enabled == VARIANT_TRUE) {
      enabledProfiles |= profile;
    }
  }
  if (enabledProfiles == 0) {
    *diagnostic = QStringLiteral("Windows Firewall is disabled for the active network profiles");
    return WindowsFirewallRuleStatus::NotRequired;
  }

  ComPtr<INetFwRules> rules;
  const auto rulesResult = policy->get_Rules(rules.put());
  if (FAILED(rulesResult)) {
    *diagnostic = QStringLiteral("could not enumerate firewall rules: %1").arg(hexResult(rulesResult));
    return WindowsFirewallRuleStatus::Unavailable;
  }
  ComPtr<IUnknown> unknownEnumerator;
  if (FAILED(rules->get__NewEnum(unknownEnumerator.put()))) {
    *diagnostic = QStringLiteral("Windows Firewall rule enumerator is unavailable");
    return WindowsFirewallRuleStatus::Unavailable;
  }
  ComPtr<IEnumVARIANT> enumerator;
  if (FAILED(unknownEnumerator->QueryInterface(IID_IEnumVARIANT, reinterpret_cast<void **>(enumerator.put())))) {
    *diagnostic = QStringLiteral("Windows Firewall rule enumerator has an unsupported interface");
    return WindowsFirewallRuleStatus::Unavailable;
  }

  const auto requestedPath = expandedWindowsPath(request.executablePath);
  bool allowMatched = false;
  VARIANT value;
  VariantInit(&value);
  ULONG fetched = 0;
  while (enumerator->Next(1, &value, &fetched) == S_OK && fetched == 1) {
    ComPtr<INetFwRule> rule;
    if (value.vt == VT_DISPATCH && value.pdispVal != nullptr) {
      (void)value.pdispVal->QueryInterface(IID_INetFwRule, reinterpret_cast<void **>(rule.put()));
    }
    VariantClear(&value);
    if (rule.get() == nullptr) {
      continue;
    }

    VARIANT_BOOL enabled = VARIANT_FALSE;
    NET_FW_RULE_DIRECTION direction = NET_FW_RULE_DIR_MAX;
    LONG profiles = 0;
    LONG protocol = 0;
    NET_FW_ACTION action = NET_FW_ACTION_MAX;
    BSTR application = nullptr;
    BSTR localPorts = nullptr;
    const bool propertiesOk = SUCCEEDED(rule->get_Enabled(&enabled)) &&
                              SUCCEEDED(rule->get_Direction(&direction)) &&
                              SUCCEEDED(rule->get_Profiles(&profiles)) &&
                              SUCCEEDED(rule->get_Protocol(&protocol)) &&
                              SUCCEEDED(rule->get_Action(&action)) &&
                              SUCCEEDED(rule->get_ApplicationName(&application));
    if (protocol == NET_FW_IP_PROTOCOL_TCP || protocol == kNetFwIpProtocolAny) {
      (void)rule->get_LocalPorts(&localPorts);
    }
    const auto applicationPath = expandedWindowsPath(fromBstr(application));
    const auto portSpecification = fromBstr(localPorts);
    SysFreeString(application);
    SysFreeString(localPorts);

    if (!propertiesOk || enabled != VARIANT_TRUE || direction != NET_FW_RULE_DIR_IN ||
        (profiles & enabledProfiles) == 0 ||
        (protocol != NET_FW_IP_PROTOCOL_TCP && protocol != kNetFwIpProtocolAny)) {
      continue;
    }
    const bool appMatches = !requestedPath.isEmpty() && applicationPath == requestedPath;
    const bool portOnlyRuleMatches = applicationPath.isEmpty() && !request.expectedTcpPorts.isEmpty();
    if ((!appMatches && !portOnlyRuleMatches) || !portsCover(portSpecification, request.expectedTcpPorts)) {
      continue;
    }
    if (action == NET_FW_ACTION_BLOCK) {
      *diagnostic = QStringLiteral("an enabled inbound block rule applies to the executable or requested ports");
      return WindowsFirewallRuleStatus::Blocked;
    }
    if (action == NET_FW_ACTION_ALLOW) {
      allowMatched = true;
    }
  }

  if (allowMatched) {
    *diagnostic = QStringLiteral("an enabled inbound allow rule covers the executable and requested TCP ports");
    return WindowsFirewallRuleStatus::Allowed;
  }
  *diagnostic = QStringLiteral("no enabled inbound allow rule covers the executable and requested TCP ports");
  return WindowsFirewallRuleStatus::MissingAllowRule;
}

bool appendListeningPorts(int addressFamily, quint32 processId, QList<quint16> *ports, QString *diagnostic)
{
  ULONG size = 0;
  DWORD result = GetExtendedTcpTable(
      nullptr, &size, FALSE, ULONG(addressFamily), TCP_TABLE_OWNER_PID_LISTENER, 0
  );
  if (result != ERROR_INSUFFICIENT_BUFFER) {
    *diagnostic = QStringLiteral("TCP table size query failed with Win32 error %1").arg(result);
    return false;
  }
  QByteArray storage(int(size), Qt::Uninitialized);
  result = GetExtendedTcpTable(
      storage.data(), &size, FALSE, ULONG(addressFamily), TCP_TABLE_OWNER_PID_LISTENER, 0
  );
  if (result != NO_ERROR) {
    *diagnostic = QStringLiteral("TCP listener query failed with Win32 error %1").arg(result);
    return false;
  }

  if (addressFamily == AF_INET) {
    const auto *table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID *>(storage.constData());
    for (DWORD index = 0; index < table->dwNumEntries; ++index) {
      const auto &row = table->table[index];
      if (row.dwOwningPid == processId && row.dwState == MIB_TCP_STATE_LISTEN) {
        ports->append(ntohs(u_short(row.dwLocalPort)));
      }
    }
  } else {
    const auto *table = reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID *>(storage.constData());
    for (DWORD index = 0; index < table->dwNumEntries; ++index) {
      const auto &row = table->table[index];
      if (row.dwOwningPid == processId && row.dwState == MIB_TCP_STATE_LISTEN) {
        ports->append(ntohs(u_short(row.dwLocalPort)));
      }
    }
  }
  return true;
}

WindowsListeningPortStatus inspectListeningPorts(
    const WindowsFirewallProbeRequest &request, QString *diagnostic
)
{
  if (request.expectedTcpPorts.isEmpty()) {
    *diagnostic = QStringLiteral("no TCP listener ports were requested");
    return WindowsListeningPortStatus::NotRequired;
  }
  const quint32 processId = request.processId == 0 ? GetCurrentProcessId() : request.processId;
  QList<quint16> listeningPorts;
  QString ipv4Diagnostic;
  QString ipv6Diagnostic;
  const bool ipv4Ok = appendListeningPorts(AF_INET, processId, &listeningPorts, &ipv4Diagnostic);
  const bool ipv6Ok = appendListeningPorts(AF_INET6, processId, &listeningPorts, &ipv6Diagnostic);
  if (!ipv4Ok && !ipv6Ok) {
    *diagnostic = QStringLiteral("IPv4: %1; IPv6: %2").arg(ipv4Diagnostic, ipv6Diagnostic);
    return WindowsListeningPortStatus::Unavailable;
  }

  QList<quint16> missing;
  for (const auto port : request.expectedTcpPorts) {
    if (!listeningPorts.contains(port) && !missing.contains(port)) {
      missing.append(port);
    }
  }
  if (!missing.isEmpty()) {
    QStringList text;
    for (const auto port : std::as_const(missing)) {
      text.append(QString::number(port));
    }
    *diagnostic = QStringLiteral("process %1 is not listening on TCP port(s): %2")
                      .arg(processId)
                      .arg(text.join(QStringLiteral(", ")));
    return WindowsListeningPortStatus::NotListening;
  }
  *diagnostic = QStringLiteral("process %1 owns all requested TCP listeners").arg(processId);
  return WindowsListeningPortStatus::Listening;
}

#endif

} // namespace

WindowsFirewallProbe::WindowsFirewallProbe(
    Inspector inspector, Clock clock, SettingsOpener settingsOpener, QObject *parent
)
    : QObject(parent), m_inspector(std::move(inspector)), m_clock(std::move(clock)),
      m_settingsOpener(std::move(settingsOpener))
{
  if (!m_inspector) {
    m_inspector = &WindowsFirewallProbe::inspectCurrentSystem;
  }
  if (!m_clock) {
    m_clock = []() { return QDateTime::currentDateTimeUtc(); };
  }
  if (!m_settingsOpener) {
    m_settingsOpener = []() -> PermissionOpenResult {
#if defined(Q_OS_WIN)
      const bool started = QProcess::startDetached(
          QStringLiteral("control.exe"), {QStringLiteral("/name"), QStringLiteral("Microsoft.WindowsFirewall")}
      );
      if (!started) {
        return {
            .error = PermissionOpenError::OpenFailed,
            .diagnostic = QStringLiteral("Windows Firewall settings could not be opened"),
        };
      }
      return {};
#else
      return {
          .error = PermissionOpenError::Unsupported,
          .diagnostic = QStringLiteral("Windows Firewall settings are unavailable on this platform"),
      };
#endif
    };
  }
  m_snapshot = snapshotFromInspection({});
}

WindowsFirewallProbeRequest WindowsFirewallProbe::requestForListeningServices(
    QString executablePath, quint16 inputPort, bool inputIsListening, quint16 filePort, quint32 processId
)
{
  WindowsFirewallProbeRequest request{
      .executablePath = std::move(executablePath),
      .processId = processId,
  };
  if (inputIsListening) {
    request.expectedTcpPorts.append(inputPort);
  }
  request.expectedTcpPorts.append(filePort);
  return request;
}

void WindowsFirewallProbe::refresh(WindowsFirewallProbeRequest request)
{
  request.executablePath = request.executablePath.trimmed();
  if (request.executablePath.isEmpty()) {
    request.executablePath = QCoreApplication::applicationFilePath();
  }
  request.expectedTcpPorts.removeAll(0);
  std::sort(request.expectedTcpPorts.begin(), request.expectedTcpPorts.end());
  request.expectedTcpPorts.erase(
      std::unique(request.expectedTcpPorts.begin(), request.expectedTcpPorts.end()),
      request.expectedTcpPorts.end()
  );

  const auto generation = ++m_generation;
  m_refreshing = true;
  auto *watcher = new QFutureWatcher<WindowsFirewallInspection>(this);
  connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, generation]() {
    const auto inspection = watcher->result();
    watcher->deleteLater();
    if (generation != m_generation) {
      return;
    }
    m_refreshing = false;
    m_snapshot = snapshotFromInspection(inspection);
    Q_EMIT snapshotChanged(m_snapshot);
  });
  watcher->setFuture(QtConcurrent::run(m_inspector, std::move(request)));
}

PermissionSnapshot WindowsFirewallProbe::current() const
{
  return m_snapshot;
}

bool WindowsFirewallProbe::isRefreshing() const noexcept
{
  return m_refreshing;
}

PermissionOpenResult WindowsFirewallProbe::openSystemSettings(PermissionKind kind)
{
  PermissionOpenResult result{
      .error = PermissionOpenError::Unsupported,
      .diagnostic = QStringLiteral("permission kind is not supported by the Windows adapter"),
  };
  switch (kind) {
  case PermissionKind::WindowsFirewall:
    result = m_settingsOpener();
    break;
  case PermissionKind::WindowsListeningPort:
    result = {
        .error = PermissionOpenError::NotActionable,
        .diagnostic = QStringLiteral("the listening-port diagnostic has no Windows settings page"),
    };
    break;
  case PermissionKind::MacLocalNetwork:
  case PermissionKind::MacAccessibility:
  case PermissionKind::MacInputMonitoring:
    result = {
        .error = PermissionOpenError::Unsupported,
        .diagnostic = QStringLiteral("permission kind is not supported by the Windows adapter"),
    };
    break;
  }
  if (!result.ok()) {
    Q_EMIT settingsOpenFailed(kind, result);
  }
  return result;
}

WindowsFirewallInspection WindowsFirewallProbe::inspectCurrentSystem(WindowsFirewallProbeRequest request)
{
#if defined(Q_OS_WIN)
  WindowsFirewallInspection result;
  result.firewall = inspectFirewallRules(request, &result.firewallDiagnostic);
  result.listeningPort = inspectListeningPorts(request, &result.listeningPortDiagnostic);
  return result;
#else
  (void)request;
  return {
      .firewall = WindowsFirewallRuleStatus::Unavailable,
      .listeningPort = WindowsListeningPortStatus::Unavailable,
      .firewallDiagnostic = QStringLiteral("Windows Firewall inspection is unavailable on this platform"),
      .listeningPortDiagnostic = QStringLiteral("Windows TCP listener inspection is unavailable on this platform"),
  };
#endif
}

PermissionSnapshot WindowsFirewallProbe::snapshotFromInspection(
    const WindowsFirewallInspection &inspection
) const
{
  auto checkedAt = m_clock().toUTC();
  if (!checkedAt.isValid()) {
    checkedAt = QDateTime::currentDateTimeUtc();
  }
  return {
      .platform = buildPermissionPlatform(),
      .entries = {firewallEntry(inspection), listeningPortEntry(inspection)},
      .checkedAtUtc = checkedAt,
  };
}

} // namespace deskflow::relaydesk
