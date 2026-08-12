# SPDX-FileCopyrightText: (C) 2026 RelayDesk Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

add_library(relaydesk_device_model STATIC
  DeviceHomeModel.cpp
  DeviceHomeModel.h
  PairingWizardModel.cpp
  PairingWizardModel.h
)

target_include_directories(
  relaydesk_device_model
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../.."
)
target_link_libraries(
  relaydesk_device_model
  PUBLIC
    relaydesk_device
    relaydesk_i18n
    relaydesk_pairing
    Qt6::Core
)
