// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Andrea Bedini

#include "qusbmuxprotocol_p.h"

#include "qusbmuxplist_p.h"
#include "qusbmuxtransport_p.h"

#include <QtCore/qcoreapplication.h>
#include <QtCore/qendian.h>
#include <QtCore/qiodevice.h>

namespace QUsbmuxProtocol {

QVariantMap request(const QString &messageType)
{
    QString progName = QCoreApplication::applicationName();
    if (progName.isEmpty())
        progName = QStringLiteral("QtUsbmuxd");

    // libusbmuxd sends the same fields; the daemon uses ProgName and
    // ClientVersionString only for logging.
    return {
        { QStringLiteral("MessageType"), messageType },
        { QStringLiteral("ProgName"), progName },
        { QStringLiteral("ClientVersionString"), QStringLiteral("QtUsbmuxd " QTUSBMUXD_VERSION_STRING) },
        { QStringLiteral("kLibUSBMuxVersion"), 3 },
    };
}

quint16 toWirePort(quint16 port)
{
    return qToBigEndian(port);
}

QByteArray encode(quint32 tag, const QVariantMap &body)
{
    const QByteArray xml = QUsbmuxPlist::toXml(body);

    QByteArray packet(HeaderSize, Qt::Uninitialized);
    qToLittleEndian<quint32>(quint32(HeaderSize + xml.size()), packet.data());
    qToLittleEndian<quint32>(PlistVersion, packet.data() + 4);
    qToLittleEndian<quint32>(PlistMessageType, packet.data() + 8);
    qToLittleEndian<quint32>(tag, packet.data() + 12);
    packet += xml;
    return packet;
}

ReadStatus read(QIODevice *device, Message *message, QString *error)
{
    if (device->bytesAvailable() < HeaderSize)
        return ReadStatus::Incomplete;

    char header[HeaderSize];
    if (device->peek(header, HeaderSize) != HeaderSize)
        return ReadStatus::Incomplete;

    const auto length = qFromLittleEndian<quint32>(header);
    const auto version = qFromLittleEndian<quint32>(header + 4);
    const auto type = qFromLittleEndian<quint32>(header + 8);
    const auto tag = qFromLittleEndian<quint32>(header + 12);

    if (length < HeaderSize || length > MaxMessageSize) {
        *error = QStringLiteral("bad message length %1").arg(length);
        return ReadStatus::Error;
    }
    if (version != PlistVersion || type != PlistMessageType) {
        *error = QStringLiteral("unsupported message (version %1, type %2)").arg(version).arg(type);
        return ReadStatus::Error;
    }
    if (device->bytesAvailable() < qint64(length))
        return ReadStatus::Incomplete;

    const QByteArray packet = device->read(length);
    if (packet.size() != qsizetype(length)) {
        *error = QStringLiteral("short read");
        return ReadStatus::Error;
    }

    QString plistError;
    auto body = QUsbmuxPlist::fromXml(packet.mid(HeaderSize), &plistError);
    if (!body) {
        *error = QStringLiteral("malformed message body: %1").arg(plistError);
        return ReadStatus::Error;
    }

    message->tag = tag;
    message->body = std::move(*body);
    return ReadStatus::Ok;
}

WaitStatus waitForMessage(QUsbmuxTransport *transport, Message *message, QDeadlineTimer deadline,
                          QString *error)
{
    QIODevice *device = transport->device();
    if (!device) {
        *error = QStringLiteral("not connected");
        return WaitStatus::Error;
    }

    for (;;) {
        switch (read(device, message, error)) {
        case ReadStatus::Ok:
            return WaitStatus::Ok;
        case ReadStatus::Error:
            return WaitStatus::Error;
        case ReadStatus::Incomplete:
            break;
        }

        if (!transport->isConnected()) {
            *error = QStringLiteral("the daemon closed the connection");
            return WaitStatus::Error;
        }
        if (deadline.hasExpired()) {
            *error = QStringLiteral("timed out waiting for the daemon");
            return WaitStatus::Timeout;
        }
        // waitForReadyRead() also returns false when the connection drops;
        // the next iteration tells the two apart.
        const int msecs = deadline.isForever() ? -1 : int(qMax<qint64>(1, deadline.remainingTime()));
        device->waitForReadyRead(msecs);
    }
}

int resultCode(const QVariantMap &body)
{
    if (body.value(QStringLiteral("MessageType")).toString() != u"Result")
        return -1;
    bool ok = false;
    const int number = body.value(QStringLiteral("Number")).toInt(&ok);
    return ok ? number : -1;
}

} // namespace QUsbmuxProtocol
