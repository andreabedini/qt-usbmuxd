// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Andrea Bedini

#pragma once

//
//  W A R N I N G
//  -------------
//
// This file is not part of the QtUsbmuxd API. It may change or disappear
// without notice.
//

#include <QtUsbmuxd/qtusbmuxdglobal.h>

#include <QtCore/qbytearray.h>
#include <QtCore/qstring.h>
#include <QtCore/qvariant.h>

#include <optional>

// The subset of Apple's XML property list format that usbmuxd messages use.
//
// Mapping:
//   dict    <-> QVariantMap
//   array   <-> QVariantList
//   string  <-> QString
//   integer <-> qint64 (quint64 for values above INT64_MAX)
//   real    <-> double
//   true    <-> bool
//   false   <-> bool
//   data    <-> QByteArray
//   date    <-> QDateTime (UTC)
//
// Binary plists ("bplist00") are not supported; usbmuxd answers XML requests
// with XML.
namespace QUsbmuxPlist {

// Exported only so the tests can reach them.
QTUSBMUXD_EXPORT QByteArray toXml(const QVariantMap &dict);
QTUSBMUXD_EXPORT std::optional<QVariantMap> fromXml(const QByteArray &xml, QString *error = nullptr);

} // namespace QUsbmuxPlist
