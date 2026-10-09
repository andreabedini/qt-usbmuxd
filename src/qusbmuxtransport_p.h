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

#include <QtUsbmuxd/qusbmuxaddress.h>

#include <QtCore/qobject.h>

QT_BEGIN_NAMESPACE
class QIODevice;
class QLocalSocket;
class QTcpSocket;
QT_END_NAMESPACE

// A connection to the daemon over whichever kind of socket the address
// calls for, with the signals and blocking calls QLocalSocket and
// QTcpSocket have in common but don't share a base class for.
//
// The sockets are created on first use and then reused, never deleted
// until the transport is, so that reconnecting from inside one of its
// signals is safe.
class QUsbmuxTransport : public QObject
{
    Q_OBJECT

public:
    explicit QUsbmuxTransport(QObject *parent = nullptr);
    ~QUsbmuxTransport() override;

    // Starts connecting, dropping any previous connection silently. An
    // invalid address fails straight away: errorOccurred() is emitted
    // before this returns.
    void connectToDaemon(const QUsbmuxAddress &address);
    bool waitForConnected(int msecs);
    bool waitForDisconnected(int msecs);
    bool isConnected() const;

    // Graceful: pending writes are flushed first.
    void disconnectFromDaemon();
    // Immediate, and silent: emits nothing.
    void abort();

    // The socket in use; nullptr before connectToDaemon().
    QIODevice *device() const;
    QString errorString() const;

Q_SIGNALS:
    void connected();
    void disconnected();
    void errorOccurred();
    void readyRead();
    void bytesWritten(qint64 bytes);

private:
    QLocalSocket *localSocket();
    QTcpSocket *tcpSocket();

    QLocalSocket *m_local = nullptr;
    QTcpSocket *m_tcp = nullptr;
    QIODevice *m_active = nullptr;
    QString m_error;
};
