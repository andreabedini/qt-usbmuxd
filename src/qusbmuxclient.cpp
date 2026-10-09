// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Andrea Bedini

#include <QtUsbmuxd/qusbmuxclient.h>

#include "qusbmuxprotocol_p.h"
#include "qusbmuxtransport_p.h"

#include <QtCore/qdeadlinetimer.h>
#include <QtCore/qiodevice.h>
#include <QtCore/qmap.h>
#include <QtCore/qpointer.h>

using namespace QUsbmuxProtocol;

namespace {

// After a successful Listen the daemon reports every attached device right
// away. When listing devices that way, a pause this long means it has
// finished. libusbmuxd waits the same.
constexpr int ListenSettleMsecs = 100;

// Write and flush, for the blocking paths.
bool writeAll(QUsbmuxTransport &transport, const QByteArray &data, QDeadlineTimer deadline,
              QString *error)
{
    QIODevice *device = transport.device();
    if (device->write(data) != data.size()) {
        *error = device->errorString();
        return false;
    }
    while (device->bytesToWrite() > 0) {
        const int msecs = deadline.isForever() ? -1 : int(qMax<qint64>(1, deadline.remainingTime()));
        if (!device->waitForBytesWritten(msecs)) {
            *error = deadline.hasExpired() ? QStringLiteral("timed out writing to the daemon")
                                           : device->errorString();
            return false;
        }
    }
    return true;
}

QUsbmuxDevice deviceFromEntry(const QVariantMap &entry)
{
    // A ListDevices entry and an Attached notification both look like
    // { DeviceID, MessageType: "Attached", Properties: { DeviceID, ... } }.
    QVariantMap properties = entry.value(QStringLiteral("Properties")).toMap();
    if (!properties.contains(QStringLiteral("DeviceID")) && entry.contains(QStringLiteral("DeviceID")))
        properties.insert(QStringLiteral("DeviceID"), entry.value(QStringLiteral("DeviceID")));
    return QUsbmuxDevice::fromProperties(properties);
}

} // namespace

class QUsbmuxClientPrivate
{
public:
    explicit QUsbmuxClientPrivate(QUsbmuxClient *q) : q(q) {}

    void setError(QUsbmuxClient::Error e, const QString &message)
    {
        error = e;
        errorString = message;
    }

    // Blocking helpers for listDevices().
    bool connectBlocking(QUsbmuxTransport &transport, QDeadlineTimer deadline);
    std::optional<QList<QUsbmuxDevice>> listWithListDevices(QDeadlineTimer deadline, bool *unsupported);
    std::optional<QList<QUsbmuxDevice>> listWithListen(QDeadlineTimer deadline);

    // Watching.
    void onWatchConnected();
    void onWatchReadyRead();
    void onWatchDisconnected();
    void onWatchError();
    void handleNotification(const QVariantMap &body);
    void failWatch(QUsbmuxClient::Error e, const QString &message);
    void teardownWatch();

    QUsbmuxClient *q;
    QUsbmuxAddress address = QUsbmuxAddress::systemDefault();
    QUsbmuxClient::Error error = QUsbmuxClient::NoError;
    QString errorString;

    QPointer<QUsbmuxTransport> watch;
    bool watching = false;
    bool listening = false; // the daemon has accepted Listen
    quint32 nextTag = 1;
    quint32 listenTag = 0;
    QMap<quint32, QUsbmuxDevice> devices;
};

bool QUsbmuxClientPrivate::connectBlocking(QUsbmuxTransport &transport, QDeadlineTimer deadline)
{
    transport.connectToDaemon(address);
    if (!transport.device()) {
        setError(QUsbmuxClient::DaemonUnavailableError, transport.errorString());
        return false;
    }
    const int msecs = deadline.isForever() ? -1 : int(qMax<qint64>(1, deadline.remainingTime()));
    if (!transport.waitForConnected(msecs)) {
        setError(QUsbmuxClient::DaemonUnavailableError,
                 QStringLiteral("can't connect to usbmuxd at %1: %2")
                     .arg(address.toString(), transport.device()->errorString()));
        return false;
    }
    return true;
}

std::optional<QList<QUsbmuxDevice>> QUsbmuxClientPrivate::listWithListDevices(QDeadlineTimer deadline,
                                                                              bool *unsupported)
{
    *unsupported = false;

    QUsbmuxTransport transport;
    if (!connectBlocking(transport, deadline))
        return std::nullopt;

    const quint32 tag = nextTag++;
    QString message;
    if (!writeAll(transport, encode(tag, request(QStringLiteral("ListDevices"))), deadline, &message)) {
        setError(QUsbmuxClient::DaemonUnavailableError, message);
        return std::nullopt;
    }

    Message reply;
    switch (waitForMessage(&transport, &reply, deadline, &message)) {
    case WaitStatus::Ok:
        break;
    case WaitStatus::Timeout:
        setError(QUsbmuxClient::TimeoutError, message);
        return std::nullopt;
    case WaitStatus::Error:
        setError(QUsbmuxClient::ProtocolError, message);
        return std::nullopt;
    }

    if (reply.tag != tag) {
        setError(QUsbmuxClient::ProtocolError,
                 QStringLiteral("reply has tag %1, expected %2").arg(reply.tag).arg(tag));
        return std::nullopt;
    }

    const QVariant list = reply.body.value(QStringLiteral("DeviceList"));
    if (list.typeId() == QMetaType::QVariantList) {
        QList<QUsbmuxDevice> result;
        for (const QVariant &entry : list.toList()) {
            const QUsbmuxDevice device = deviceFromEntry(entry.toMap());
            if (device.isValid())
                result.append(device);
        }
        return result;
    }

    // Daemons that predate ListDevices answer it with an error result.
    const int code = resultCode(reply.body);
    if (code == ResultBadCommand || code == ResultBadVersion) {
        *unsupported = true;
        return std::nullopt;
    }
    if (code >= 0) {
        setError(QUsbmuxClient::DaemonError,
                 QStringLiteral("usbmuxd refused ListDevices (result %1)").arg(code));
    } else {
        setError(QUsbmuxClient::ProtocolError, QStringLiteral("unexpected reply to ListDevices"));
    }
    return std::nullopt;
}

std::optional<QList<QUsbmuxDevice>> QUsbmuxClientPrivate::listWithListen(QDeadlineTimer deadline)
{
    QUsbmuxTransport transport;
    if (!connectBlocking(transport, deadline))
        return std::nullopt;

    const quint32 tag = nextTag++;
    QString message;
    if (!writeAll(transport, encode(tag, request(QStringLiteral("Listen"))), deadline, &message)) {
        setError(QUsbmuxClient::DaemonUnavailableError, message);
        return std::nullopt;
    }

    Message reply;
    switch (waitForMessage(&transport, &reply, deadline, &message)) {
    case WaitStatus::Ok:
        break;
    case WaitStatus::Timeout:
        setError(QUsbmuxClient::TimeoutError, message);
        return std::nullopt;
    case WaitStatus::Error:
        setError(QUsbmuxClient::ProtocolError, message);
        return std::nullopt;
    }
    if (reply.tag != tag || resultCode(reply.body) != ResultOk) {
        setError(QUsbmuxClient::DaemonError, QStringLiteral("usbmuxd refused Listen"));
        return std::nullopt;
    }

    QMap<quint32, QUsbmuxDevice> found;
    for (;;) {
        QDeadlineTimer settle(ListenSettleMsecs);
        if (deadline.deadline() < settle.deadline())
            settle = deadline;

        Message notification;
        const WaitStatus status = waitForMessage(&transport, &notification, settle, &message);
        if (status == WaitStatus::Timeout)
            break;
        if (status == WaitStatus::Error) {
            setError(QUsbmuxClient::ProtocolError, message);
            return std::nullopt;
        }

        const QString type = notification.body.value(QStringLiteral("MessageType")).toString();
        if (type == u"Attached") {
            const QUsbmuxDevice device = deviceFromEntry(notification.body);
            if (device.isValid())
                found.insert(device.deviceId(), device);
        } else if (type == u"Detached") {
            found.remove(notification.body.value(QStringLiteral("DeviceID")).toUInt());
        }
    }
    return found.values();
}

QUsbmuxClient::QUsbmuxClient(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<QUsbmuxClientPrivate>(this))
{
}

QUsbmuxClient::~QUsbmuxClient()
{
    d->teardownWatch();
}

QUsbmuxAddress QUsbmuxClient::daemonAddress() const
{
    return d->address;
}

void QUsbmuxClient::setDaemonAddress(const QUsbmuxAddress &address)
{
    d->address = address;
}

QList<QUsbmuxDevice> QUsbmuxClient::listDevices(int msecs)
{
    d->setError(NoError, QString());
    const QDeadlineTimer deadline(msecs);

    bool unsupported = false;
    if (auto devices = d->listWithListDevices(deadline, &unsupported))
        return *devices;
    if (!unsupported)
        return {};

    if (auto devices = d->listWithListen(deadline))
        return *devices;
    return {};
}

void QUsbmuxClient::startWatching()
{
    if (d->watching)
        return;

    d->setError(NoError, QString());
    d->watching = true;
    d->listening = false;
    d->devices.clear();

    auto *transport = new QUsbmuxTransport(this);
    d->watch = transport;
    connect(transport, &QUsbmuxTransport::connected, this, [this] { d->onWatchConnected(); });
    connect(transport, &QUsbmuxTransport::readyRead, this, [this] { d->onWatchReadyRead(); });
    connect(transport, &QUsbmuxTransport::disconnected, this, [this] { d->onWatchDisconnected(); });
    connect(transport, &QUsbmuxTransport::errorOccurred, this, [this] { d->onWatchError(); });

    emit watchingChanged(true);
    transport->connectToDaemon(d->address);
}

void QUsbmuxClient::stopWatching()
{
    if (!d->watching)
        return;
    d->teardownWatch();
    emit watchingChanged(false);
}

bool QUsbmuxClient::isWatching() const
{
    return d->watching;
}

QList<QUsbmuxDevice> QUsbmuxClient::devices() const
{
    return d->devices.values();
}

QUsbmuxClient::Error QUsbmuxClient::error() const
{
    return d->error;
}

QString QUsbmuxClient::errorString() const
{
    return d->errorString;
}

void QUsbmuxClientPrivate::onWatchConnected()
{
    listenTag = nextTag++;
    const QByteArray packet = encode(listenTag, request(QStringLiteral("Listen")));
    if (watch->device()->write(packet) != packet.size())
        failWatch(QUsbmuxClient::DaemonUnavailableError, watch->device()->errorString());
}

void QUsbmuxClientPrivate::onWatchReadyRead()
{
    // Each message handled can end the watch, which drops the transport.
    while (watch) {
        Message message;
        QString problem;
        switch (read(watch->device(), &message, &problem)) {
        case ReadStatus::Incomplete:
            return;
        case ReadStatus::Error:
            failWatch(QUsbmuxClient::ProtocolError, problem);
            return;
        case ReadStatus::Ok:
            break;
        }

        if (!listening) {
            const int code = resultCode(message.body);
            if (message.tag != listenTag || code != ResultOk) {
                failWatch(code > 0 ? QUsbmuxClient::DaemonError : QUsbmuxClient::ProtocolError,
                          QStringLiteral("usbmuxd refused Listen (result %1)").arg(code));
                return;
            }
            listening = true;
            continue;
        }

        handleNotification(message.body);
    }
}

void QUsbmuxClientPrivate::handleNotification(const QVariantMap &body)
{
    const QString type = body.value(QStringLiteral("MessageType")).toString();

    if (type == u"Attached") {
        const QUsbmuxDevice device = deviceFromEntry(body);
        if (!device.isValid())
            return;
        devices.insert(device.deviceId(), device);
        emit q->deviceAttached(device);
    } else if (type == u"Detached") {
        bool ok = false;
        const quint32 id = body.value(QStringLiteral("DeviceID")).toUInt(&ok);
        if (!ok)
            return;
        QUsbmuxDevice device = devices.take(id);
        if (!device.isValid())
            device = QUsbmuxDevice::fromProperties({ { QStringLiteral("DeviceID"), id } });
        emit q->deviceDetached(device);
    }
    // "Paired" and anything newer: nothing to do.
}

void QUsbmuxClientPrivate::onWatchDisconnected()
{
    failWatch(QUsbmuxClient::DisconnectedError, QStringLiteral("usbmuxd closed the connection"));
}

void QUsbmuxClientPrivate::onWatchError()
{
    if (!watch)
        return;
    if (!watch->isConnected() && !listening) {
        failWatch(QUsbmuxClient::DaemonUnavailableError,
                  QStringLiteral("can't connect to usbmuxd at %1: %2")
                      .arg(address.toString(), watch->errorString()));
    }
    // Errors on an established connection are followed by disconnected().
}

void QUsbmuxClientPrivate::failWatch(QUsbmuxClient::Error e, const QString &message)
{
    if (!watching)
        return;
    setError(e, message);
    teardownWatch();
    emit q->watchingChanged(false);
    emit q->errorOccurred(e);
}

void QUsbmuxClientPrivate::teardownWatch()
{
    watching = false;
    listening = false;
    devices.clear();
    if (watch) {
        // This can run inside one of the transport's own signals, so it
        // mustn't be deleted here. Watching needs an event loop anyway.
        watch->disconnect(q);
        watch->abort();
        watch->deleteLater();
        watch = nullptr;
    }
}
