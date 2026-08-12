/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "relaydesk/platform/WindowsStartAtLogin.h"

#include <QDir>
#include <QFileInfo>

#include <string>
#include <utility>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace deskflow::relaydesk {

namespace {

constexpr auto kRunSubKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

StartAtLoginSnapshot snapshotFromRegistryError(
    const WindowsRunRegistryResult &result, const QString &expectedCommand
)
{
  return {
      .state = StartAtLoginState::Disabled,
      .expectedCommand = expectedCommand,
      .errorCode = result.errorCode,
      .nativeError = result.nativeError,
      .diagnostic = result.diagnostic,
  };
}

class NativeWindowsRunRegistry final : public IWindowsRunRegistry
{
public:
  WindowsRunRegistryReadResult read(const QString &valueName) const override
  {
#if defined(Q_OS_WIN)
    DWORD type = 0;
    DWORD byteCount = 0;
    auto result = RegGetValueW(
        HKEY_CURRENT_USER, kRunSubKey, reinterpret_cast<LPCWSTR>(valueName.utf16()),
        RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, &type, nullptr, &byteCount
    );
    if (result == ERROR_FILE_NOT_FOUND) {
      return {};
    }
    if (result != ERROR_SUCCESS) {
      return {
          .errorCode = StartAtLoginErrorCode::RegistryReadFailed,
          .nativeError = static_cast<quint32>(result),
          .diagnostic = QStringLiteral("Unable to read the current-user Run value (Win32 %1)").arg(result),
      };
    }

    std::wstring buffer(byteCount / sizeof(wchar_t), L'\0');
    result = RegGetValueW(
        HKEY_CURRENT_USER, kRunSubKey, reinterpret_cast<LPCWSTR>(valueName.utf16()),
        RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, &type, buffer.data(), &byteCount
    );
    if (result != ERROR_SUCCESS) {
      return {
          .errorCode = StartAtLoginErrorCode::RegistryReadFailed,
          .nativeError = static_cast<quint32>(result),
          .diagnostic = QStringLiteral("Unable to read the current-user Run command (Win32 %1)").arg(result),
      };
    }

    const auto characterCount = byteCount / sizeof(wchar_t);
    if (characterCount > 0 && buffer.at(characterCount - 1) == L'\0') {
      buffer.resize(characterCount - 1);
    } else {
      buffer.resize(characterCount);
    }
    return {.found = true, .command = QString::fromStdWString(buffer)};
#else
    Q_UNUSED(valueName)
    return {
        .errorCode = StartAtLoginErrorCode::UnsupportedPlatform,
        .diagnostic = QStringLiteral("Windows start-at-login is unavailable on this platform"),
    };
#endif
  }

  WindowsRunRegistryResult write(const QString &valueName, const QString &command) override
  {
#if defined(Q_OS_WIN)
    HKEY key = nullptr;
    auto result = RegCreateKeyExW(
        HKEY_CURRENT_USER, kRunSubKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr
    );
    if (result == ERROR_SUCCESS) {
      const auto byteCount = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
      result = RegSetValueExW(
          key, reinterpret_cast<LPCWSTR>(valueName.utf16()), 0, REG_SZ,
          reinterpret_cast<const BYTE *>(command.utf16()), byteCount
      );
      RegCloseKey(key);
    }
    if (result != ERROR_SUCCESS) {
      return {
          .errorCode = StartAtLoginErrorCode::RegistryWriteFailed,
          .nativeError = static_cast<quint32>(result),
          .diagnostic = QStringLiteral("Unable to write the current-user Run value (Win32 %1)").arg(result),
      };
    }
    return {};
#else
    Q_UNUSED(valueName)
    Q_UNUSED(command)
    return {
        .errorCode = StartAtLoginErrorCode::UnsupportedPlatform,
        .diagnostic = QStringLiteral("Windows start-at-login is unavailable on this platform"),
    };
#endif
  }

  WindowsRunRegistryResult remove(const QString &valueName) override
  {
#if defined(Q_OS_WIN)
    const auto result =
        RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunSubKey, reinterpret_cast<LPCWSTR>(valueName.utf16()));
    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
      return {
          .errorCode = StartAtLoginErrorCode::RegistryDeleteFailed,
          .nativeError = static_cast<quint32>(result),
          .diagnostic = QStringLiteral("Unable to remove the current-user Run value (Win32 %1)").arg(result),
      };
    }
    return {};
#else
    Q_UNUSED(valueName)
    return {
        .errorCode = StartAtLoginErrorCode::UnsupportedPlatform,
        .diagnostic = QStringLiteral("Windows start-at-login is unavailable on this platform"),
    };
#endif
  }
};

} // namespace

WindowsStartAtLogin::WindowsStartAtLogin(
    IWindowsRunRegistry &registry, QString valueName, QString executablePath, QStringList arguments
)
    : m_registry(registry),
      m_valueName(std::move(valueName)),
      m_expectedCommand(buildCommand(executablePath, arguments))
{
}

StartAtLoginSnapshot WindowsStartAtLogin::query() const
{
  if (m_valueName.trimmed().isEmpty() || m_valueName.contains(QChar::Null) || m_expectedCommand.isEmpty()) {
    return invalidConfiguration();
  }

  const auto registryResult = m_registry.read(m_valueName);
  if (registryResult.errorCode != StartAtLoginErrorCode::None) {
    return {
        .state = StartAtLoginState::Disabled,
        .expectedCommand = m_expectedCommand,
        .errorCode = registryResult.errorCode,
        .nativeError = registryResult.nativeError,
        .diagnostic = registryResult.diagnostic,
    };
  }

  if (!registryResult.found) {
    return {.state = StartAtLoginState::Disabled, .expectedCommand = m_expectedCommand};
  }

  const auto state = registryResult.command == m_expectedCommand ? StartAtLoginState::Enabled : StartAtLoginState::Stale;
  return {
      .state = state,
      .configuredCommand = registryResult.command,
      .expectedCommand = m_expectedCommand,
  };
}

StartAtLoginSnapshot WindowsStartAtLogin::setEnabled(bool enabled)
{
  if (m_valueName.trimmed().isEmpty() || m_valueName.contains(QChar::Null) || m_expectedCommand.isEmpty()) {
    return invalidConfiguration();
  }

  const auto operation = enabled ? m_registry.write(m_valueName, m_expectedCommand) : m_registry.remove(m_valueName);
  if (!operation.succeeded()) {
    return snapshotFromRegistryError(operation, m_expectedCommand);
  }

  auto verified = query();
  const auto desiredState = enabled ? StartAtLoginState::Enabled : StartAtLoginState::Disabled;
  if (!verified.succeeded() || verified.state == desiredState) {
    return verified;
  }

  verified.errorCode = StartAtLoginErrorCode::RegistryVerificationFailed;
  verified.diagnostic = enabled ? QStringLiteral("The current-user Run value did not retain the requested command")
                                : QStringLiteral("The current-user Run value remained after removal");
  return verified;
}

QString WindowsStartAtLogin::quoteCommandLineArgument(const QString &argument)
{
  const bool needsQuotes = argument.isEmpty() || argument.contains(QChar::Space) || argument.contains(QLatin1Char('\t')) ||
                           argument.contains(QChar('"'));
  if (!needsQuotes) {
    return argument;
  }

  QString quoted;
  quoted.reserve(argument.size() + 2);
  quoted += QChar('"');
  qsizetype backslashes = 0;
  for (const auto character : argument) {
    if (character == QChar('\\')) {
      ++backslashes;
      continue;
    }
    if (character == QChar('"')) {
      quoted += QString(backslashes * 2 + 1, QChar('\\'));
      quoted += character;
      backslashes = 0;
      continue;
    }
    quoted += QString(backslashes, QChar('\\'));
    backslashes = 0;
    quoted += character;
  }
  quoted += QString(backslashes * 2, QChar('\\'));
  quoted += QChar('"');
  return quoted;
}

QString WindowsStartAtLogin::buildCommand(const QString &executablePath, const QStringList &arguments)
{
  if (executablePath.trimmed().isEmpty() || executablePath.contains(QChar::Null) ||
      executablePath.contains(QChar('"')) || !QFileInfo(executablePath).isAbsolute()) {
    return {};
  }

  const auto nativePath = QDir::toNativeSeparators(QDir::cleanPath(executablePath));
  QStringList commandParts{quoteCommandLineArgument(nativePath)};
  for (const auto &argument : arguments) {
    if (argument.contains(QChar::Null)) {
      return {};
    }
    commandParts.append(quoteCommandLineArgument(argument));
  }
  return commandParts.join(QChar::Space);
}

StartAtLoginSnapshot WindowsStartAtLogin::invalidConfiguration() const
{
  return {
      .state = StartAtLoginState::Disabled,
      .expectedCommand = m_expectedCommand,
      .errorCode = StartAtLoginErrorCode::InvalidExecutablePath,
      .diagnostic = QStringLiteral("A non-empty value name and absolute executable path are required"),
  };
}

std::unique_ptr<IWindowsRunRegistry> makeNativeWindowsRunRegistry()
{
  return std::make_unique<NativeWindowsRunRegistry>();
}

} // namespace deskflow::relaydesk
