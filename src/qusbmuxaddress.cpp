// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Andrea Bedini

#include <QtUsbmuxd/qusbmuxaddress.h>

#include <QtCore/qdebug.h>

QUsbmuxAddress QUsbmuxAddress::local(const QString &path)
{
    QUsbmuxAddress a;
    if (path.isEmpty())
        return a;
    a.m_type = Local;
    a.m_hostOrPath = path;
    return a;
}

QUsbmuxAddress QUsbmuxAddress::tcp(const QString &host, quint16 port)
{
    QUsbmuxAddress a;
    if (host.isEmpty() || port == 0)
        return a;
    a.m_type = Tcp;
    a.m_hostOrPath = host;
    a.m_port = port;
    return a;
}

QUsbmuxAddress QUsbmuxAddress::platformDefault()
{
#ifdef Q_OS_WIN
    return tcp(QStringLiteral("127.0.0.1"), 27015);
#else
    return local(QStringLiteral("/var/run/usbmuxd"));
#endif
}

QUsbmuxAddress QUsbmuxAddress::fromString(const QString &address)
{
    if (address.startsWith(u"UNIX:"))
        return local(address.mid(5));

    // Split on the last colon so a bracketed IPv6 host works: "[::1]:27015".
    const qsizetype colon = address.lastIndexOf(u':');
    if (colon <= 0)
        return {};

    QString host = address.left(colon);
    if (host.startsWith(u'[') && host.endsWith(u']'))
        host = host.mid(1, host.size() - 2);

    bool ok = false;
    const uint port = address.mid(colon + 1).toUInt(&ok);
    if (!ok || port > 0xffff)
        return {};
    return tcp(host, quint16(port));
}

QUsbmuxAddress QUsbmuxAddress::systemDefault()
{
    const QString env = qEnvironmentVariable("USBMUXD_SOCKET_ADDRESS");
    if (!env.isEmpty()) {
        const QUsbmuxAddress a = fromString(env);
        if (a.isValid())
            return a;
        qWarning("QUsbmuxAddress: ignoring malformed USBMUXD_SOCKET_ADDRESS \"%s\"", qPrintable(env));
    }
    return platformDefault();
}

QString QUsbmuxAddress::toString() const
{
    switch (m_type) {
    case Local:
        return QStringLiteral("UNIX:") + m_hostOrPath;
    case Tcp:
        if (m_hostOrPath.contains(u':'))
            return QStringLiteral("[%1]:%2").arg(m_hostOrPath).arg(m_port);
        return QStringLiteral("%1:%2").arg(m_hostOrPath).arg(m_port);
    case Invalid:
        break;
    }
    return {};
}

QDebug operator<<(QDebug debug, const QUsbmuxAddress &address)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "QUsbmuxAddress(";
    if (address.isValid())
        debug << address.toString();
    else
        debug << "invalid";
    debug << ')';
    return debug;
}
