// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Andrea Bedini

#pragma once

#include <QtUsbmuxd/qtusbmuxdglobal.h>
#include <QtUsbmuxd/qusbmuxaddress.h>
#include <QtUsbmuxd/qusbmuxdevice.h>

#include <QtCore/qlist.h>
#include <QtCore/qobject.h>

#include <memory>

class QUsbmuxClientPrivate;

// Discovers iOS devices through the usbmuxd daemon.
//
// listDevices() asks for the current device list and blocks until it
// arrives, so it can be used from threads without an event loop.
//
// startWatching() keeps a connection to the daemon open and reports devices
// as they come and go through deviceAttached() and deviceDetached(). Devices
// already attached are reported straight away. Watching needs a running
// event loop.
//
// To talk to a device, pass its deviceId() to QUsbmuxSocket.
class QTUSBMUXD_EXPORT QUsbmuxClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool watching READ isWatching NOTIFY watchingChanged)

public:
    enum Error {
        NoError,
        // The daemon isn't running or the address is wrong.
        DaemonUnavailableError,
        TimeoutError,
        // The daemon refused the request.
        DaemonError,
        // A reply that doesn't follow the protocol.
        ProtocolError,
        // The daemon closed the watching connection.
        DisconnectedError,
    };
    Q_ENUM(Error)

    explicit QUsbmuxClient(QObject *parent = nullptr);
    ~QUsbmuxClient() override;

    // Defaults to QUsbmuxAddress::systemDefault(). Changing it doesn't affect
    // a watch already in progress.
    QUsbmuxAddress daemonAddress() const;
    void setDaemonAddress(const QUsbmuxAddress &address);

    // The devices attached right now. Blocks for at most msecs; on failure
    // returns an empty list and sets error().
    QList<QUsbmuxDevice> listDevices(int msecs = 5000);

    void startWatching();
    void stopWatching();
    bool isWatching() const;

    // While watching, the devices attached as far as the daemon has told us.
    QList<QUsbmuxDevice> devices() const;

    Error error() const;
    QString errorString() const;

Q_SIGNALS:
    void deviceAttached(const QUsbmuxDevice &device);
    void deviceDetached(const QUsbmuxDevice &device);
    void watchingChanged(bool watching);
    // Emitted when watching fails; watching has stopped by then.
    void errorOccurred(QUsbmuxClient::Error error);

private:
    Q_DISABLE_COPY_MOVE(QUsbmuxClient)
    friend class QUsbmuxClientPrivate;
    std::unique_ptr<QUsbmuxClientPrivate> d;
};
