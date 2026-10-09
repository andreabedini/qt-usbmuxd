// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Andrea Bedini

// usbmux: a small command-line tool built on QtUsbmuxd.
//
//   usbmux list                  list attached devices
//   usbmux watch                 report devices as they come and go
//   usbmux cat <device> <port>   connect to a port on a device and copy
//                                whatever it sends to stdout
//
// <device> is a device ID or a UDID; "-" picks the first USB device.

#include <QtUsbmuxd/QUsbmuxClient>
#include <QtUsbmuxd/QUsbmuxSocket>

#include <QtCore/qcommandlineparser.h>
#include <QtCore/qcoreapplication.h>
#include <QtCore/qtextstream.h>

#include <cstdio>

namespace {

QTextStream &out()
{
    static QTextStream s(stdout);
    return s;
}

QTextStream &err()
{
    static QTextStream s(stderr);
    return s;
}

QString describe(const QUsbmuxDevice &device)
{
    const char *type = device.connectionType() == QUsbmuxDevice::Usb       ? "USB"
                       : device.connectionType() == QUsbmuxDevice::Network ? "Network"
                                                                            : "?";
    return QStringLiteral("%1\t%2\t%3").arg(device.deviceId()).arg(QLatin1StringView(type), device.serialNumber());
}

int list(QUsbmuxClient &client)
{
    const QList<QUsbmuxDevice> devices = client.listDevices();
    if (client.error() != QUsbmuxClient::NoError) {
        err() << "usbmux: " << client.errorString() << Qt::endl;
        return 1;
    }
    for (const QUsbmuxDevice &device : devices)
        out() << describe(device) << Qt::endl;
    return 0;
}

int watch(QUsbmuxClient &client)
{
    QObject::connect(&client, &QUsbmuxClient::deviceAttached, [](const QUsbmuxDevice &device) {
        out() << "+ " << describe(device) << Qt::endl;
    });
    QObject::connect(&client, &QUsbmuxClient::deviceDetached, [](const QUsbmuxDevice &device) {
        out() << "- " << describe(device) << Qt::endl;
    });
    QObject::connect(&client, &QUsbmuxClient::errorOccurred, [&client] {
        err() << "usbmux: " << client.errorString() << Qt::endl;
        QCoreApplication::exit(1);
    });
    client.startWatching();
    return QCoreApplication::exec();
}

int cat(QUsbmuxClient &client, const QString &which, quint16 port)
{
    const QList<QUsbmuxDevice> devices = client.listDevices();
    if (client.error() != QUsbmuxClient::NoError) {
        err() << "usbmux: " << client.errorString() << Qt::endl;
        return 1;
    }

    QUsbmuxDevice target;
    for (const QUsbmuxDevice &device : devices) {
        const bool match = which == u"-" ? device.connectionType() == QUsbmuxDevice::Usb
                                         : (QString::number(device.deviceId()) == which
                                            || device.serialNumber() == which);
        if (match) {
            target = device;
            break;
        }
    }
    if (!target.isValid()) {
        err() << "usbmux: no such device: " << which << Qt::endl;
        return 1;
    }

    QUsbmuxSocket socket;
    socket.setDaemonAddress(client.daemonAddress());
    socket.connectToDevice(target, port, QIODevice::ReadOnly);
    if (!socket.waitForConnected()) {
        err() << "usbmux: " << socket.errorString() << Qt::endl;
        return 1;
    }

    // Blocking on purpose: this is the pattern for threads without an
    // event loop.
    while (socket.state() == QUsbmuxSocket::ConnectedState || socket.bytesAvailable() > 0) {
        if (socket.bytesAvailable() == 0 && !socket.waitForReadyRead(-1))
            continue;
        const QByteArray data = socket.readAll();
        std::fwrite(data.constData(), 1, size_t(data.size()), stdout);
        std::fflush(stdout);
    }
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("usbmux"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Talk to iOS devices through usbmuxd."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("list, watch, or cat <device> <port>"));
    QCommandLineOption address(QStringList{ QStringLiteral("a"), QStringLiteral("address") },
                               QStringLiteral("Daemon address: UNIX:<path> or <host>:<port>."),
                               QStringLiteral("address"));
    parser.addOption(address);
    parser.process(app);

    QUsbmuxClient client;
    if (parser.isSet(address)) {
        const QUsbmuxAddress a = QUsbmuxAddress::fromString(parser.value(address));
        if (!a.isValid()) {
            err() << "usbmux: bad address: " << parser.value(address) << Qt::endl;
            return 2;
        }
        client.setDaemonAddress(a);
    }

    const QStringList args = parser.positionalArguments();
    const QString command = args.value(0);
    if (command == u"list" && args.size() == 1)
        return list(client);
    if (command == u"watch" && args.size() == 1)
        return watch(client);
    if (command == u"cat" && args.size() == 3) {
        bool ok = false;
        const uint port = args[2].toUInt(&ok);
        if (ok && port > 0 && port <= 0xffff)
            return cat(client, args[1], quint16(port));
    }
    parser.showHelp(2);
}
