// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Andrea Bedini

#pragma once

#include <QtUsbmuxd/qusbmuxaddress.h>

#include <QtCore/qbytearray.h>
#include <QtCore/qlist.h>
#include <QtCore/qobject.h>
#include <QtCore/qthread.h>
#include <QtCore/qvariant.h>

class FakeUsbmuxdWorker;

// An in-process stand-in for the usbmuxd daemon, running on its own thread
// so that blocking client calls on the test thread can be served.
//
// It behaves like the real daemon as far as the plist protocol goes:
// ListDevices, Listen with Attached/Detached notifications, and Connect,
// after which the connection is handed to a scripted service.
class FakeUsbmuxd
{
public:
    enum class Transport { Tcp, Local };

    struct Service
    {
        // Sent right behind the Connect reply, in the same write.
        QByteArray greeting;
        // Echo back whatever the client sends.
        bool echo = false;
        // Close the connection after the greeting.
        bool closeAfterGreeting = false;
    };

    explicit FakeUsbmuxd(Transport transport = Transport::Tcp);
    ~FakeUsbmuxd();

    // Starts listening. Fails if the platform (or a sandbox) won't let us.
    bool start(QString *error = nullptr);
    QUsbmuxAddress address() const;

    // Each entry is a "Properties" dictionary; DeviceID is required.
    void setDevices(const QList<QVariantMap> &devices);
    // Answer ListDevices with result 1, as daemons that predate it do.
    void setSupportsListDevices(bool supported);
    // Never answer anything.
    void setStalled(bool stalled);
    void addService(quint32 deviceId, quint16 port, const Service &service);

    // Notify listening clients.
    void attach(const QVariantMap &properties);
    void detach(quint32 deviceId);
    // Drop every client connection.
    void disconnectAll();

    // The bodies of all requests received so far, in order.
    QList<QVariantMap> requests() const;

    static QVariantMap usbDevice(quint32 id, const QString &serial);
    static QVariantMap networkDevice(quint32 id, const QString &serial);

private:
    QThread m_thread;
    FakeUsbmuxdWorker *m_worker = nullptr;
};

Q_DECLARE_METATYPE(FakeUsbmuxd::Transport)
