// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Andrea Bedini

#pragma once

#include <QtUsbmuxd/qtusbmuxdglobal.h>
#include <QtUsbmuxd/qusbmuxaddress.h>

#include <QtCore/qiodevice.h>

#include <memory>

class QUsbmuxDevice;
class QUsbmuxSocketPrivate;

// A TCP-like connection to a port on an iOS device, tunnelled through
// usbmuxd.
//
// Use it like QTcpSocket: connectToDevice(), then wait for connected() or
// call waitForConnected(), then read and write. The waitFor...() functions
// work without an event loop.
//
// The device must already be listening on the port; ConnectionRefusedError
// means it isn't.
class QTUSBMUXD_EXPORT QUsbmuxSocket : public QIODevice
{
    Q_OBJECT

public:
    enum SocketState {
        UnconnectedState,
        // Connecting to the daemon, or waiting for it to reach the device.
        ConnectingState,
        ConnectedState,
    };
    Q_ENUM(SocketState)

    enum SocketError {
        NoError,
        // The daemon isn't running or the address is wrong.
        DaemonUnavailableError,
        // The daemon doesn't know the device ID (e.g. it was unplugged).
        DeviceNotFoundError,
        // Nothing on the device is listening on the port.
        ConnectionRefusedError,
        TimeoutError,
        // The daemon refused the request for another reason.
        DaemonError,
        // A reply that doesn't follow the protocol.
        ProtocolError,
        // The connection was closed from the other end.
        RemoteClosedError,
    };
    Q_ENUM(SocketError)

    explicit QUsbmuxSocket(QObject *parent = nullptr);
    ~QUsbmuxSocket() override;

    // Defaults to QUsbmuxAddress::systemDefault(). Takes effect on the next
    // connectToDevice().
    QUsbmuxAddress daemonAddress() const;
    void setDaemonAddress(const QUsbmuxAddress &address);

    void connectToDevice(quint32 deviceId, quint16 port, OpenMode mode = ReadWrite);
    void connectToDevice(const QUsbmuxDevice &device, quint16 port, OpenMode mode = ReadWrite);
    void disconnectFromDevice();
    // Disconnects immediately, discarding anything not yet written.
    void abort();

    bool waitForConnected(int msecs = 30000);
    bool waitForDisconnected(int msecs = 30000);

    SocketState state() const;
    SocketError error() const;

    quint32 deviceId() const;
    quint16 port() const;

    bool isSequential() const override;
    qint64 bytesAvailable() const override;
    qint64 bytesToWrite() const override;
    bool canReadLine() const override;
    void close() override;
    bool waitForReadyRead(int msecs = 30000) override;
    bool waitForBytesWritten(int msecs = 30000) override;

Q_SIGNALS:
    void connected();
    void disconnected();
    void stateChanged(QUsbmuxSocket::SocketState state);
    void errorOccurred(QUsbmuxSocket::SocketError error);

protected:
    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData(const char *data, qint64 size) override;

private:
    Q_DISABLE_COPY_MOVE(QUsbmuxSocket)
    friend class QUsbmuxSocketPrivate;
    std::unique_ptr<QUsbmuxSocketPrivate> d;
};
