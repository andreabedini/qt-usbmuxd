// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Andrea Bedini

#include "qusbmuxtransport_p.h"

#include <QtCore/qobject.h>
#include <QtNetwork/qlocalsocket.h>
#include <QtNetwork/qtcpsocket.h>

QUsbmuxTransport::QUsbmuxTransport(QObject *parent)
    : QObject(parent)
{
}

QUsbmuxTransport::~QUsbmuxTransport()
{
    abort();
}

QLocalSocket *QUsbmuxTransport::localSocket()
{
    if (!m_local) {
        m_local = new QLocalSocket(this);
        connect(m_local, &QLocalSocket::connected, this, &QUsbmuxTransport::connected);
        connect(m_local, &QLocalSocket::disconnected, this, &QUsbmuxTransport::disconnected);
        connect(m_local, &QLocalSocket::readyRead, this, &QUsbmuxTransport::readyRead);
        connect(m_local, &QLocalSocket::bytesWritten, this, &QUsbmuxTransport::bytesWritten);
        connect(m_local, &QLocalSocket::errorOccurred, this, [this] {
            m_error = m_local->errorString();
            emit errorOccurred();
        });
    }
    return m_local;
}

QTcpSocket *QUsbmuxTransport::tcpSocket()
{
    if (!m_tcp) {
        m_tcp = new QTcpSocket(this);
        connect(m_tcp, &QTcpSocket::connected, this, &QUsbmuxTransport::connected);
        connect(m_tcp, &QTcpSocket::disconnected, this, &QUsbmuxTransport::disconnected);
        connect(m_tcp, &QTcpSocket::readyRead, this, &QUsbmuxTransport::readyRead);
        connect(m_tcp, &QTcpSocket::bytesWritten, this, &QUsbmuxTransport::bytesWritten);
        connect(m_tcp, &QTcpSocket::errorOccurred, this, [this] {
            m_error = m_tcp->errorString();
            emit errorOccurred();
        });
    }
    return m_tcp;
}

void QUsbmuxTransport::connectToDaemon(const QUsbmuxAddress &address)
{
    abort();
    m_error.clear();

    switch (address.type()) {
    case QUsbmuxAddress::Local:
        m_active = localSocket();
        m_local->connectToServer(address.path());
        break;
    case QUsbmuxAddress::Tcp:
        m_active = tcpSocket();
        m_tcp->connectToHost(address.host(), address.port());
        break;
    case QUsbmuxAddress::Invalid:
        m_active = nullptr;
        m_error = QStringLiteral("invalid usbmuxd address");
        emit errorOccurred();
        break;
    }
}

bool QUsbmuxTransport::waitForConnected(int msecs)
{
    if (m_active == m_local && m_local)
        return m_local->waitForConnected(msecs);
    if (m_active == m_tcp && m_tcp)
        return m_tcp->waitForConnected(msecs);
    return false;
}

bool QUsbmuxTransport::waitForDisconnected(int msecs)
{
    if (!isConnected())
        return true;
    if (m_active == m_local)
        return m_local->waitForDisconnected(msecs);
    return m_tcp->waitForDisconnected(msecs);
}

bool QUsbmuxTransport::isConnected() const
{
    if (m_active && m_active == m_local)
        return m_local->state() == QLocalSocket::ConnectedState;
    if (m_active && m_active == m_tcp)
        return m_tcp->state() == QAbstractSocket::ConnectedState;
    return false;
}

void QUsbmuxTransport::disconnectFromDaemon()
{
    if (m_active && m_active == m_local)
        m_local->disconnectFromServer();
    else if (m_active && m_active == m_tcp)
        m_tcp->disconnectFromHost();
}

void QUsbmuxTransport::abort()
{
    // abort() can emit disconnected() and stateChanged() synchronously;
    // callers use this to drop a connection they no longer care about.
    if (m_local) {
        const QSignalBlocker blocker(m_local);
        m_local->abort();
    }
    if (m_tcp) {
        const QSignalBlocker blocker(m_tcp);
        m_tcp->abort();
    }
}

QIODevice *QUsbmuxTransport::device() const
{
    return m_active;
}

QString QUsbmuxTransport::errorString() const
{
    return m_error;
}
