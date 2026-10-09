// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Andrea Bedini

#include "fakeusbmuxd.h"

#include "qusbmuxprotocol_p.h"

#include <QtCore/qendian.h>
#include <QtCore/qmap.h>
#include <QtCore/qmutex.h>
#include <QtCore/qpointer.h>
#include <QtCore/qtemporarydir.h>
#include <QtNetwork/qlocalserver.h>
#include <QtNetwork/qlocalsocket.h>
#include <QtNetwork/qtcpserver.h>
#include <QtNetwork/qtcpsocket.h>

#include <memory>

using namespace QUsbmuxProtocol;

class FakeUsbmuxdWorker : public QObject
{
    Q_OBJECT

public:
    struct Connection
    {
        QPointer<QIODevice> socket;
        bool listening = false;
        bool tunnelling = false;
        FakeUsbmuxd::Service service;
    };

    explicit FakeUsbmuxdWorker(FakeUsbmuxd::Transport transport) : m_transport(transport) {}

    // All of these run on the worker thread.
    Q_INVOKABLE bool start(QString *error);
    Q_INVOKABLE void attach(const QVariantMap &properties);
    Q_INVOKABLE void detach(quint32 deviceId);
    Q_INVOKABLE void disconnectAll();
    Q_INVOKABLE void shutdown();

    void onNewConnection(QIODevice *socket);
    void onReadyRead(QIODevice *socket);
    void handle(Connection &c, const Message &message);
    void reply(QIODevice *socket, quint32 tag, const QVariantMap &body, const QByteArray &trailer = {});
    static void closeSocket(QIODevice *socket);

    // Shared with the test thread.
    mutable QMutex mutex;
    QList<QVariantMap> devices;
    bool supportsListDevices = true;
    bool stalled = false;
    QMap<QPair<quint32, quint16>, FakeUsbmuxd::Service> services;
    QList<QVariantMap> requests;
    QUsbmuxAddress address;

private:
    FakeUsbmuxd::Transport m_transport;
    std::unique_ptr<QTcpServer> m_tcp;
    std::unique_ptr<QLocalServer> m_local;
    std::unique_ptr<QTemporaryDir> m_dir;
    QMap<QIODevice *, Connection> m_connections;
};

bool FakeUsbmuxdWorker::start(QString *error)
{
    if (m_transport == FakeUsbmuxd::Transport::Tcp) {
        m_tcp = std::make_unique<QTcpServer>();
        if (!m_tcp->listen(QHostAddress::LocalHost, 0)) {
            *error = m_tcp->errorString();
            return false;
        }
        QObject::connect(m_tcp.get(), &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = m_tcp->nextPendingConnection())
                onNewConnection(s);
        });
        QMutexLocker lock(&mutex);
        address = QUsbmuxAddress::tcp(QStringLiteral("127.0.0.1"), m_tcp->serverPort());
        return true;
    }

    m_dir = std::make_unique<QTemporaryDir>();
    const QString path = m_dir->filePath(QStringLiteral("usbmuxd"));
    m_local = std::make_unique<QLocalServer>();
    if (!m_local->listen(path)) {
        *error = m_local->errorString();
        return false;
    }
    QObject::connect(m_local.get(), &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *s = m_local->nextPendingConnection())
            onNewConnection(s);
    });
    QMutexLocker lock(&mutex);
    address = QUsbmuxAddress::local(path);
    return true;
}

void FakeUsbmuxdWorker::onNewConnection(QIODevice *socket)
{
    m_connections.insert(socket, Connection{ socket, false, false, {} });
    QObject::connect(socket, &QIODevice::readyRead, this, [this, socket] { onReadyRead(socket); });
    const auto forget = [this, socket] {
        m_connections.remove(socket);
        socket->deleteLater();
    };
    if (auto *tcp = qobject_cast<QTcpSocket *>(socket))
        QObject::connect(tcp, &QTcpSocket::disconnected, this, forget);
    else if (auto *local = qobject_cast<QLocalSocket *>(socket))
        QObject::connect(local, &QLocalSocket::disconnected, this, forget);
}

void FakeUsbmuxdWorker::onReadyRead(QIODevice *socket)
{
    auto it = m_connections.find(socket);
    if (it == m_connections.end())
        return;

    if (it->tunnelling) {
        const QByteArray data = socket->readAll();
        if (it->service.echo)
            socket->write(data);
        return;
    }

    for (;;) {
        Message message;
        QString error;
        const ReadStatus status = read(socket, &message, &error);
        if (status == ReadStatus::Incomplete)
            return;
        if (status == ReadStatus::Error) {
            qWarning("FakeUsbmuxd: %s", qPrintable(error));
            closeSocket(socket);
            return;
        }
        {
            QMutexLocker lock(&mutex);
            requests.append(message.body);
            if (stalled)
                continue;
        }
        handle(*it, message);
        // Closing a socket can drop it from the map before we get back here.
        it = m_connections.find(socket);
        if (it == m_connections.end())
            return;
        if (it->tunnelling) {
            // Anything the client sent right behind its request is tunnel data.
            if (socket->bytesAvailable() > 0)
                onReadyRead(socket);
            return;
        }
    }
}

void FakeUsbmuxdWorker::handle(Connection &c, const Message &message)
{
    const QString type = message.body.value(QStringLiteral("MessageType")).toString();
    const auto result = [](int number) {
        return QVariantMap{ { QStringLiteral("MessageType"), QStringLiteral("Result") },
                            { QStringLiteral("Number"), number } };
    };
    const auto entry = [](const QVariantMap &props) {
        return QVariantMap{ { QStringLiteral("DeviceID"), props.value(QStringLiteral("DeviceID")) },
                            { QStringLiteral("MessageType"), QStringLiteral("Attached") },
                            { QStringLiteral("Properties"), props } };
    };

    QMutexLocker lock(&mutex);

    if (type == u"ListDevices") {
        if (!supportsListDevices) {
            reply(c.socket, message.tag, result(ResultBadCommand));
            return;
        }
        QVariantList list;
        for (const QVariantMap &props : std::as_const(devices))
            list.append(entry(props));
        reply(c.socket, message.tag, { { QStringLiteral("DeviceList"), list } });
        return;
    }

    if (type == u"Listen") {
        c.listening = true;
        reply(c.socket, message.tag, result(ResultOk));
        for (const QVariantMap &props : std::as_const(devices))
            reply(c.socket, 0, entry(props));
        return;
    }

    if (type == u"Connect") {
        const quint32 id = message.body.value(QStringLiteral("DeviceID")).toUInt();
        // The client sends the port in network byte order.
        const quint16 port = qFromBigEndian(quint16(message.body.value(QStringLiteral("PortNumber")).toUInt()));

        const bool known = std::any_of(devices.cbegin(), devices.cend(), [id](const QVariantMap &p) {
            return p.value(QStringLiteral("DeviceID")).toUInt() == id;
        });
        if (!known) {
            reply(c.socket, message.tag, result(ResultBadDevice));
            return;
        }
        const auto service = services.constFind({ id, port });
        if (service == services.cend()) {
            reply(c.socket, message.tag, result(ResultConnectionRefused));
            return;
        }

        c.tunnelling = true;
        c.service = *service;
        reply(c.socket, message.tag, result(ResultOk), service->greeting);
        if (service->closeAfterGreeting)
            closeSocket(c.socket);
        return;
    }

    reply(c.socket, message.tag, result(ResultBadCommand));
}

void FakeUsbmuxdWorker::reply(QIODevice *socket, quint32 tag, const QVariantMap &body,
                              const QByteArray &trailer)
{
    socket->write(encode(tag, body) + trailer);
}

void FakeUsbmuxdWorker::closeSocket(QIODevice *socket)
{
    if (auto *tcp = qobject_cast<QTcpSocket *>(socket))
        tcp->disconnectFromHost();
    else if (auto *local = qobject_cast<QLocalSocket *>(socket))
        local->disconnectFromServer();
}

void FakeUsbmuxdWorker::attach(const QVariantMap &properties)
{
    {
        QMutexLocker lock(&mutex);
        devices.append(properties);
    }
    const QVariantMap notification{
        { QStringLiteral("DeviceID"), properties.value(QStringLiteral("DeviceID")) },
        { QStringLiteral("MessageType"), QStringLiteral("Attached") },
        { QStringLiteral("Properties"), properties },
    };
    for (const Connection &c : std::as_const(m_connections)) {
        if (c.listening)
            reply(c.socket, 0, notification);
    }
}

void FakeUsbmuxdWorker::detach(quint32 deviceId)
{
    {
        QMutexLocker lock(&mutex);
        devices.removeIf([deviceId](const QVariantMap &p) {
            return p.value(QStringLiteral("DeviceID")).toUInt() == deviceId;
        });
    }
    const QVariantMap notification{
        { QStringLiteral("DeviceID"), deviceId },
        { QStringLiteral("MessageType"), QStringLiteral("Detached") },
    };
    for (const Connection &c : std::as_const(m_connections)) {
        if (c.listening)
            reply(c.socket, 0, notification);
    }
}

void FakeUsbmuxdWorker::disconnectAll()
{
    const auto sockets = m_connections.keys();
    for (QIODevice *socket : sockets)
        closeSocket(socket);
}

void FakeUsbmuxdWorker::shutdown()
{
    disconnectAll();
    qDeleteAll(m_connections.keys());
    m_connections.clear();
    m_tcp.reset();
    m_local.reset();
}

FakeUsbmuxd::FakeUsbmuxd(Transport transport)
{
    m_worker = new FakeUsbmuxdWorker(transport);
    m_worker->moveToThread(&m_thread);
    QObject::connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start();
}

FakeUsbmuxd::~FakeUsbmuxd()
{
    QMetaObject::invokeMethod(m_worker, &FakeUsbmuxdWorker::shutdown, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
}

bool FakeUsbmuxd::start(QString *error)
{
    bool ok = false;
    QString message;
    QMetaObject::invokeMethod(m_worker, [&] { ok = m_worker->start(&message); },
                              Qt::BlockingQueuedConnection);
    if (error)
        *error = message;
    return ok;
}

QUsbmuxAddress FakeUsbmuxd::address() const
{
    QMutexLocker lock(&m_worker->mutex);
    return m_worker->address;
}

void FakeUsbmuxd::setDevices(const QList<QVariantMap> &devices)
{
    QMutexLocker lock(&m_worker->mutex);
    m_worker->devices = devices;
}

void FakeUsbmuxd::setSupportsListDevices(bool supported)
{
    QMutexLocker lock(&m_worker->mutex);
    m_worker->supportsListDevices = supported;
}

void FakeUsbmuxd::setStalled(bool stalled)
{
    QMutexLocker lock(&m_worker->mutex);
    m_worker->stalled = stalled;
}

void FakeUsbmuxd::addService(quint32 deviceId, quint16 port, const Service &service)
{
    QMutexLocker lock(&m_worker->mutex);
    m_worker->services.insert({ deviceId, port }, service);
}

void FakeUsbmuxd::attach(const QVariantMap &properties)
{
    QMetaObject::invokeMethod(m_worker, [=] { m_worker->attach(properties); },
                              Qt::BlockingQueuedConnection);
}

void FakeUsbmuxd::detach(quint32 deviceId)
{
    QMetaObject::invokeMethod(m_worker, [=] { m_worker->detach(deviceId); },
                              Qt::BlockingQueuedConnection);
}

void FakeUsbmuxd::disconnectAll()
{
    QMetaObject::invokeMethod(m_worker, &FakeUsbmuxdWorker::disconnectAll,
                              Qt::BlockingQueuedConnection);
}

QList<QVariantMap> FakeUsbmuxd::requests() const
{
    QMutexLocker lock(&m_worker->mutex);
    return m_worker->requests;
}

QVariantMap FakeUsbmuxd::usbDevice(quint32 id, const QString &serial)
{
    return {
        { QStringLiteral("ConnectionSpeed"), 480000000 },
        { QStringLiteral("ConnectionType"), QStringLiteral("USB") },
        { QStringLiteral("DeviceID"), id },
        { QStringLiteral("LocationID"), 336592896 },
        { QStringLiteral("ProductID"), 4776 },
        { QStringLiteral("SerialNumber"), serial },
    };
}

QVariantMap FakeUsbmuxd::networkDevice(quint32 id, const QString &serial)
{
    return {
        { QStringLiteral("ConnectionType"), QStringLiteral("Network") },
        { QStringLiteral("DeviceID"), id },
        { QStringLiteral("EscapedFullServiceName"), QStringLiteral("aa:bb@fe80::1._apple-mobdev2._tcp.local.") },
        { QStringLiteral("InterfaceIndex"), 12 },
        { QStringLiteral("NetworkAddress"), QByteArray(28, '\0') },
        { QStringLiteral("SerialNumber"), serial },
    };
}

#include "fakeusbmuxd.moc"
