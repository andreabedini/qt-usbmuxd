// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Andrea Bedini

#include <QtUsbmuxd/qusbmuxsocket.h>

#include <QtUsbmuxd/qusbmuxdevice.h>

#include "qusbmuxprotocol_p.h"
#include "qusbmuxtransport_p.h"

#include <QtCore/qdeadlinetimer.h>

using namespace QUsbmuxProtocol;

namespace {

int remaining(QDeadlineTimer deadline)
{
    return deadline.isForever() ? -1 : int(qMax<qint64>(0, deadline.remainingTime()));
}

} // namespace

class QUsbmuxSocketPrivate
{
public:
    explicit QUsbmuxSocketPrivate(QUsbmuxSocket *q) : q(q) {}

    void setState(QUsbmuxSocket::SocketState s);
    void fail(QUsbmuxSocket::SocketError e, const QString &message);
    // Drops the connection without emitting anything.
    void dropConnection();

    void onTransportConnected();
    void onTransportReadyRead();
    void onTransportBytesWritten(qint64 bytes);
    void onTransportDisconnected();
    void onTransportError();

    QUsbmuxSocket *q;
    QUsbmuxTransport *transport = nullptr;
    QUsbmuxAddress address = QUsbmuxAddress::systemDefault();

    QUsbmuxSocket::SocketState state = QUsbmuxSocket::UnconnectedState;
    QUsbmuxSocket::SocketError error = QUsbmuxSocket::NoError;
    QIODevice::OpenMode openMode;
    quint32 deviceId = 0;
    quint16 port = 0;
    quint32 connectTag = 0;
    quint32 nextTag = 1;

    // The device's data still unread when the connection closed. Kept so it
    // can be read after disconnected(), as with QTcpSocket.
    QByteArray leftover;
    // disconnectFromDevice() was called: the close that follows is expected.
    bool closing = false;
};

void QUsbmuxSocketPrivate::setState(QUsbmuxSocket::SocketState s)
{
    if (state == s)
        return;
    state = s;
    emit q->stateChanged(s);
}

void QUsbmuxSocketPrivate::dropConnection()
{
    if (transport)
        transport->abort();
}

void QUsbmuxSocketPrivate::fail(QUsbmuxSocket::SocketError e, const QString &message)
{
    const bool wasConnected = state == QUsbmuxSocket::ConnectedState;
    dropConnection();
    error = e;
    q->setErrorString(message);
    emit q->errorOccurred(e);
    setState(QUsbmuxSocket::UnconnectedState);
    if (wasConnected)
        emit q->disconnected();
}

void QUsbmuxSocketPrivate::onTransportConnected()
{
    if (state != QUsbmuxSocket::ConnectingState)
        return;

    connectTag = nextTag++;
    QVariantMap body = request(QStringLiteral("Connect"));
    body.insert(QStringLiteral("DeviceID"), deviceId);
    body.insert(QStringLiteral("PortNumber"), toWirePort(port));

    const QByteArray packet = encode(connectTag, body);
    if (transport->device()->write(packet) != packet.size())
        fail(QUsbmuxSocket::DaemonUnavailableError, transport->device()->errorString());
}

void QUsbmuxSocketPrivate::onTransportReadyRead()
{
    if (state == QUsbmuxSocket::ConnectedState) {
        emit q->readyRead();
        return;
    }
    if (state != QUsbmuxSocket::ConnectingState)
        return;

    Message reply;
    QString problem;
    switch (read(transport->device(), &reply, &problem)) {
    case ReadStatus::Incomplete:
        return;
    case ReadStatus::Error:
        fail(QUsbmuxSocket::ProtocolError, problem);
        return;
    case ReadStatus::Ok:
        break;
    }

    if (reply.tag != connectTag) {
        fail(QUsbmuxSocket::ProtocolError,
             QStringLiteral("reply has tag %1, expected %2").arg(reply.tag).arg(connectTag));
        return;
    }

    switch (const int code = resultCode(reply.body)) {
    case ResultOk:
        break;
    case ResultBadDevice:
        fail(QUsbmuxSocket::DeviceNotFoundError,
             QStringLiteral("usbmuxd doesn't know device %1").arg(deviceId));
        return;
    case ResultConnectionRefused:
        fail(QUsbmuxSocket::ConnectionRefusedError,
             QStringLiteral("device %1 refused the connection to port %2").arg(deviceId).arg(port));
        return;
    case -1:
        fail(QUsbmuxSocket::ProtocolError, QStringLiteral("unexpected reply to Connect"));
        return;
    default:
        fail(QUsbmuxSocket::DaemonError,
             QStringLiteral("usbmuxd refused Connect (result %1)").arg(code));
        return;
    }

    // From here on the connection carries the device's byte stream.
    q->QIODevice::open(openMode | QIODevice::Unbuffered);
    setState(QUsbmuxSocket::ConnectedState);
    emit q->connected();

    // The device may have sent something already, right behind the reply.
    if (state == QUsbmuxSocket::ConnectedState && transport->device()->bytesAvailable() > 0)
        emit q->readyRead();
}

void QUsbmuxSocketPrivate::onTransportBytesWritten(qint64 bytes)
{
    if (state == QUsbmuxSocket::ConnectedState)
        emit q->bytesWritten(bytes);
}

void QUsbmuxSocketPrivate::onTransportDisconnected()
{
    switch (state) {
    case QUsbmuxSocket::UnconnectedState:
        return;
    case QUsbmuxSocket::ConnectingState:
        fail(QUsbmuxSocket::DaemonUnavailableError,
             QStringLiteral("usbmuxd closed the connection before replying"));
        return;
    case QUsbmuxSocket::ConnectedState:
        break;
    }

    leftover += transport->device()->readAll();
    if (!closing) {
        error = QUsbmuxSocket::RemoteClosedError;
        q->setErrorString(QStringLiteral("the remote end closed the connection"));
        emit q->errorOccurred(error);
    }
    closing = false;
    setState(QUsbmuxSocket::UnconnectedState);
    emit q->disconnected();
}

void QUsbmuxSocketPrivate::onTransportError()
{
    if (state != QUsbmuxSocket::ConnectingState)
        return; // once connected, a dropped connection is reported via disconnected()
    if (!transport->isConnected()) {
        fail(QUsbmuxSocket::DaemonUnavailableError,
             QStringLiteral("can't connect to usbmuxd at %1: %2")
                 .arg(address.toString(), transport->errorString()));
    }
}

QUsbmuxSocket::QUsbmuxSocket(QObject *parent)
    : QIODevice(parent)
    , d(std::make_unique<QUsbmuxSocketPrivate>(this))
{
    d->transport = new QUsbmuxTransport(this);
    connect(d->transport, &QUsbmuxTransport::connected, this, [this] { d->onTransportConnected(); });
    connect(d->transport, &QUsbmuxTransport::readyRead, this, [this] { d->onTransportReadyRead(); });
    connect(d->transport, &QUsbmuxTransport::bytesWritten, this,
            [this](qint64 bytes) { d->onTransportBytesWritten(bytes); });
    connect(d->transport, &QUsbmuxTransport::disconnected, this,
            [this] { d->onTransportDisconnected(); });
    connect(d->transport, &QUsbmuxTransport::errorOccurred, this, [this] { d->onTransportError(); });
}

QUsbmuxSocket::~QUsbmuxSocket()
{
    d->dropConnection();
}

QUsbmuxAddress QUsbmuxSocket::daemonAddress() const
{
    return d->address;
}

void QUsbmuxSocket::setDaemonAddress(const QUsbmuxAddress &address)
{
    d->address = address;
}

void QUsbmuxSocket::connectToDevice(quint32 deviceId, quint16 port, OpenMode mode)
{
    if (d->state != UnconnectedState) {
        qWarning("QUsbmuxSocket::connectToDevice() called while already connecting or connected");
        return;
    }

    if (isOpen())
        QIODevice::close();
    d->leftover.clear();
    d->closing = false;
    d->error = NoError;
    setErrorString(QString());
    d->deviceId = deviceId;
    d->port = port;
    d->openMode = mode;

    d->setState(ConnectingState);
    d->transport->connectToDaemon(d->address);
}

void QUsbmuxSocket::connectToDevice(const QUsbmuxDevice &device, quint16 port, OpenMode mode)
{
    connectToDevice(device.deviceId(), port, mode);
}

void QUsbmuxSocket::disconnectFromDevice()
{
    switch (d->state) {
    case UnconnectedState:
        return;
    case ConnectingState:
        abort();
        return;
    case ConnectedState:
        d->closing = true;
        // Emits disconnected() through onTransportDisconnected(), now if
        // nothing is waiting to be written, otherwise once it has been.
        d->transport->disconnectFromDaemon();
        return;
    }
}

void QUsbmuxSocket::abort()
{
    const bool wasConnected = d->state == ConnectedState;
    d->dropConnection();
    d->closing = false;
    d->setState(UnconnectedState);
    if (wasConnected)
        emit disconnected();
}

bool QUsbmuxSocket::waitForConnected(int msecs)
{
    const QDeadlineTimer deadline(msecs);

    while (d->state == ConnectingState) {
        if (!d->transport->isConnected()) {
            // Emits connected() synchronously, which sends the request.
            if (!d->transport->waitForConnected(remaining(deadline))) {
                if (d->state == ConnectingState) {
                    d->fail(deadline.hasExpired() ? TimeoutError : DaemonUnavailableError,
                            QStringLiteral("can't connect to usbmuxd at %1: %2")
                                .arg(d->address.toString(),
                                     d->transport->device() ? d->transport->device()->errorString()
                                                            : d->transport->errorString()));
                }
                break;
            }
            continue;
        }

        if (deadline.hasExpired()) {
            d->fail(TimeoutError, QStringLiteral("timed out waiting for usbmuxd"));
            break;
        }

        QIODevice *device = d->transport->device();
        if (device->bytesToWrite() > 0)
            device->waitForBytesWritten(remaining(deadline));
        else
            device->waitForReadyRead(remaining(deadline)); // emits readyRead() synchronously
    }

    return d->state == ConnectedState;
}

bool QUsbmuxSocket::waitForDisconnected(int msecs)
{
    if (d->state == UnconnectedState)
        return true;
    if (d->state == ConnectingState) {
        qWarning("QUsbmuxSocket::waitForDisconnected() called while connecting");
        return false;
    }
    d->transport->waitForDisconnected(msecs);
    return d->state == UnconnectedState;
}

QUsbmuxSocket::SocketState QUsbmuxSocket::state() const
{
    return d->state;
}

QUsbmuxSocket::SocketError QUsbmuxSocket::error() const
{
    return d->error;
}

quint32 QUsbmuxSocket::deviceId() const
{
    return d->deviceId;
}

quint16 QUsbmuxSocket::port() const
{
    return d->port;
}

bool QUsbmuxSocket::isSequential() const
{
    return true;
}

qint64 QUsbmuxSocket::bytesAvailable() const
{
    qint64 n = QIODevice::bytesAvailable() + d->leftover.size();
    if (d->state == ConnectedState)
        n += d->transport->device()->bytesAvailable();
    return n;
}

qint64 QUsbmuxSocket::bytesToWrite() const
{
    if (d->state == ConnectedState)
        return d->transport->device()->bytesToWrite();
    return 0;
}

bool QUsbmuxSocket::canReadLine() const
{
    if (QIODevice::canReadLine() || d->leftover.contains('\n'))
        return true;
    return d->state == ConnectedState && d->transport->device()->canReadLine();
}

void QUsbmuxSocket::close()
{
    disconnectFromDevice();
    d->leftover.clear();
    QIODevice::close();
}

bool QUsbmuxSocket::waitForReadyRead(int msecs)
{
    if (d->state != ConnectedState)
        return false;
    return d->transport->device()->waitForReadyRead(msecs);
}

bool QUsbmuxSocket::waitForBytesWritten(int msecs)
{
    if (d->state != ConnectedState)
        return false;
    return d->transport->device()->waitForBytesWritten(msecs);
}

qint64 QUsbmuxSocket::readData(char *data, qint64 maxSize)
{
    qint64 n = 0;
    if (!d->leftover.isEmpty()) {
        n = qMin<qint64>(maxSize, d->leftover.size());
        memcpy(data, d->leftover.constData(), size_t(n));
        d->leftover.remove(0, n);
    }
    if (n < maxSize && d->state == ConnectedState) {
        const qint64 more = d->transport->device()->read(data + n, maxSize - n);
        if (more > 0)
            n += more;
    }
    if (n == 0 && d->state != ConnectedState)
        return -1; // nothing left and nothing more coming
    return n;
}

qint64 QUsbmuxSocket::writeData(const char *data, qint64 size)
{
    if (d->state != ConnectedState)
        return -1;
    return d->transport->device()->write(data, size);
}
