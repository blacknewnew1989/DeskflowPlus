/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QString>
#include <QStringList>
#include <QMetaType>
#include <QtGlobal>

#include <memory>

namespace deskflow::relaydesk {

enum class StartAtLoginState
{
  Disabled,
  Enabled,
  Stale,
};

// Stable application-facing codes. Native registry errors are kept separately
// so callers can diagnose a failure without exposing platform text to users.
enum class StartAtLoginErrorCode : int
{
  None = 0,
  UnsupportedPlatform = 4300,
  InvalidExecutablePath = 4301,
  RegistryReadFailed = 4302,
  RegistryWriteFailed = 4303,
  RegistryDeleteFailed = 4304,
  RegistryVerificationFailed = 4305,
};

struct WindowsRunRegistryReadResult
{
  bool found = false;
  QString command;
  StartAtLoginErrorCode errorCode = StartAtLoginErrorCode::None;
  quint32 nativeError = 0;
  QString diagnostic;
};

struct WindowsRunRegistryResult
{
  StartAtLoginErrorCode errorCode = StartAtLoginErrorCode::None;
  quint32 nativeError = 0;
  QString diagnostic;

  [[nodiscard]] bool succeeded() const noexcept
  {
    return errorCode == StartAtLoginErrorCode::None;
  }
};

class IWindowsRunRegistry
{
public:
  virtual ~IWindowsRunRegistry() = default;

  [[nodiscard]] virtual WindowsRunRegistryReadResult read(const QString &valueName) const = 0;
  [[nodiscard]] virtual WindowsRunRegistryResult write(const QString &valueName, const QString &command) = 0;
  [[nodiscard]] virtual WindowsRunRegistryResult remove(const QString &valueName) = 0;
};

struct StartAtLoginSnapshot
{
  StartAtLoginState state = StartAtLoginState::Disabled;
  QString configuredCommand;
  QString expectedCommand;
  StartAtLoginErrorCode errorCode = StartAtLoginErrorCode::None;
  quint32 nativeError = 0;
  QString diagnostic;

  [[nodiscard]] bool succeeded() const noexcept
  {
    return errorCode == StartAtLoginErrorCode::None;
  }

  [[nodiscard]] bool enabled() const noexcept
  {
    return succeeded() && state == StartAtLoginState::Enabled;
  }
};

class WindowsStartAtLogin
{
public:
  WindowsStartAtLogin(
      IWindowsRunRegistry &registry, QString valueName, QString executablePath,
      QStringList arguments = {QStringLiteral("--start-in-tray")}
  );

  [[nodiscard]] StartAtLoginSnapshot query() const;
  [[nodiscard]] StartAtLoginSnapshot setEnabled(bool enabled);

  [[nodiscard]] static QString quoteCommandLineArgument(const QString &argument);
  [[nodiscard]] static QString buildCommand(const QString &executablePath, const QStringList &arguments = {});

private:
  [[nodiscard]] StartAtLoginSnapshot invalidConfiguration() const;

  IWindowsRunRegistry &m_registry;
  QString m_valueName;
  QString m_expectedCommand;
};

[[nodiscard]] std::unique_ptr<IWindowsRunRegistry> makeNativeWindowsRunRegistry();

} // namespace deskflow::relaydesk

Q_DECLARE_METATYPE(deskflow::relaydesk::StartAtLoginState)
Q_DECLARE_METATYPE(deskflow::relaydesk::StartAtLoginErrorCode)
Q_DECLARE_METATYPE(deskflow::relaydesk::StartAtLoginSnapshot)
