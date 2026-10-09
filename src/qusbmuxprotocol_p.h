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
#include <QtCore/qdeadlinetimer.h>
#include <QtCore/qstring.h>
#include <QtCore/qvariant.h>

QT_BEGIN_NAMESPACE
class QIODevice;
QT_END_NAMESPACE

class QUsbmuxTransport;

// usbmuxd's message framing. Every message is a 16-byte header of
// little-endian uint32s (total length including the header, protocol
// version, message type, tag), followed by the body. Only protocol version 1
// is used, where the message type is always 8 and the body is an XML plist
// whose "MessageType" key says what it is. Replies echo the request's tag;
// notifications on a listening connection have tag 0.
namespace QUsbmuxProtocol {

constexpr qint64 HeaderSize = 16;
constexpr quint32 PlistVersion = 1;
constexpr quint32 PlistMessageType = 8;

// Replies are a few hundred bytes per device; anything near this is not a
// usbmuxd reply.
constexpr quint32 MaxMessageSize = 4 * 1024 * 1024;

// The "Number" in a "Result" reply.
enum ResultCode : int {
    ResultOk = 0,
    ResultBadCommand = 1,
    ResultBadDevice = 2,
    ResultConnectionRefused = 3,
    ResultBadVersion = 6,
};

struct Message
{
    quint32 tag = 0;
    QVariantMap body;
};

// A request dictionary with the fields every client sends.
QTUSBMUXD_EXPORT QVariantMap request(const QString &messageType);

// The PortNumber field of a Connect request holds the port in network byte
// order, read back as a host integer.
QTUSBMUXD_EXPORT quint16 toWirePort(quint16 port);

QTUSBMUXD_EXPORT QByteArray encode(quint32 tag, const QVariantMap &body);

enum class ReadStatus {
    // Not a whole message available yet; nothing consumed.
    Incomplete,
    Ok,
    Error,
};

// Reads one message if a whole one is available, and never more than that:
// after a successful Connect the same socket carries the device's stream.
QTUSBMUXD_EXPORT ReadStatus read(QIODevice *device, Message *message, QString *error);

enum class WaitStatus {
    Ok,
    Timeout,
    Error,
};

// Blocks until a whole message arrives, the deadline passes, or the
// connection fails.
WaitStatus waitForMessage(QUsbmuxTransport *transport, Message *message, QDeadlineTimer deadline,
                          QString *error);

// The "Number" of a "Result" reply, or -1 if the body isn't one.
QTUSBMUXD_EXPORT int resultCode(const QVariantMap &body);

} // namespace QUsbmuxProtocol
