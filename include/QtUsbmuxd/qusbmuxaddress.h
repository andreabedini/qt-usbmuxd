// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Andrea Bedini

#pragma once

#include <QtUsbmuxd/qtusbmuxdglobal.h>

#include <QtCore/qmetatype.h>
#include <QtCore/qstring.h>

QT_BEGIN_NAMESPACE
class QDebug;
QT_END_NAMESPACE

// Where the usbmuxd daemon listens: a local (Unix domain) socket or a TCP
// endpoint.
//
// usbmuxd itself listens on /var/run/usbmuxd on Linux and macOS. On Windows
// the daemon is Apple Mobile Device Service, which listens on TCP
// 127.0.0.1:27015.
class QTUSBMUXD_EXPORT QUsbmuxAddress
{
public:
    enum Type {
        Invalid,
        Local,
        Tcp,
    };

    QUsbmuxAddress() = default;

    static QUsbmuxAddress local(const QString &path);
    static QUsbmuxAddress tcp(const QString &host, quint16 port);

    // The platform's standard daemon address.
    static QUsbmuxAddress platformDefault();

    // Parses the format libusbmuxd uses for USBMUXD_SOCKET_ADDRESS:
    // "UNIX:<path>" or "<host>:<port>". Returns an invalid address if the
    // string is malformed.
    static QUsbmuxAddress fromString(const QString &address);

    // The address from USBMUXD_SOCKET_ADDRESS if that is set and valid,
    // platformDefault() otherwise. This is what QUsbmuxClient and
    // QUsbmuxSocket use unless told otherwise.
    static QUsbmuxAddress systemDefault();

    Type type() const { return m_type; }
    bool isValid() const { return m_type != Invalid; }

    // Only meaningful for Local addresses.
    QString path() const { return m_type == Local ? m_hostOrPath : QString(); }
    // Only meaningful for Tcp addresses.
    QString host() const { return m_type == Tcp ? m_hostOrPath : QString(); }
    quint16 port() const { return m_port; }

    // The inverse of fromString().
    QString toString() const;

    friend bool operator==(const QUsbmuxAddress &a, const QUsbmuxAddress &b)
    {
        return a.m_type == b.m_type && a.m_hostOrPath == b.m_hostOrPath && a.m_port == b.m_port;
    }
    friend bool operator!=(const QUsbmuxAddress &a, const QUsbmuxAddress &b) { return !(a == b); }

private:
    Type m_type = Invalid;
    QString m_hostOrPath;
    quint16 m_port = 0;
};

QTUSBMUXD_EXPORT QDebug operator<<(QDebug debug, const QUsbmuxAddress &address);

Q_DECLARE_METATYPE(QUsbmuxAddress)
