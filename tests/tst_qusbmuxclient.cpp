// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Andrea Bedini

#include "fakeusbmuxd.h"

#include <QtUsbmuxd/QUsbmuxClient>

#include <QtCore/qelapsedtimer.h>
#include <QtNetwork/qtcpserver.h>
#include <QtTest/qsignalspy.h>
#include <QtTest/qtest.h>

namespace {

const QString serialA = QStringLiteral("00008030-000A");
const QString serialB = QStringLiteral("00008030-000B");

// A TCP address nothing is listening on.
QUsbmuxAddress deadAddress()
{
    QTcpServer server;
    server.listen(QHostAddress::LocalHost, 0);
    const quint16 port = server.serverPort();
    server.close();
    return QUsbmuxAddress::tcp(QStringLiteral("127.0.0.1"), port);
}

} // namespace

class tst_QUsbmuxClient : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void listDevices_data();
    void listDevices();
    void listDevicesFallsBackToListen();
    void listDevicesWithNoDevices();
    void listDevicesDaemonUnavailable();
    void listDevicesTimesOut();
    void listDevicesInvalidAddress();
    void watchReportsDevicesAsTheyComeAndGo();
    void watchFailsWhenDaemonGoes();
    void watchFailsWhenDaemonUnavailable();
    void stopWatching();
};

void tst_QUsbmuxClient::listDevices_data()
{
    QTest::addColumn<FakeUsbmuxd::Transport>("transport");
    QTest::newRow("tcp") << FakeUsbmuxd::Transport::Tcp;
    QTest::newRow("local") << FakeUsbmuxd::Transport::Local;
}

void tst_QUsbmuxClient::listDevices()
{
    QFETCH(FakeUsbmuxd::Transport, transport);

    FakeUsbmuxd daemon(transport);
    QString why;
    if (!daemon.start(&why))
        QSKIP(qPrintable(QStringLiteral("can't run the fake daemon here: ") + why));
    daemon.setDevices({ FakeUsbmuxd::usbDevice(3, serialA), FakeUsbmuxd::networkDevice(4, serialB) });

    QUsbmuxClient client;
    client.setDaemonAddress(daemon.address());
    const QList<QUsbmuxDevice> devices = client.listDevices();
    QCOMPARE(client.error(), QUsbmuxClient::NoError);
    QCOMPARE(devices.size(), 2);

    QCOMPARE(devices[0].deviceId(), 3u);
    QCOMPARE(devices[0].serialNumber(), serialA);
    QCOMPARE(devices[0].connectionType(), QUsbmuxDevice::Usb);
    QCOMPARE(devices[0].productId(), quint16(4776));
    QCOMPARE(devices[0].locationId(), 336592896u);
    QCOMPARE(devices[0].connectionSpeed(), 480000000u);

    QCOMPARE(devices[1].deviceId(), 4u);
    QCOMPARE(devices[1].connectionType(), QUsbmuxDevice::Network);
    QVERIFY(devices[1].properties().contains(QStringLiteral("EscapedFullServiceName")));

    const auto requests = daemon.requests();
    QCOMPARE(requests.size(), 1);
    QCOMPARE(requests[0].value(QStringLiteral("MessageType")).toString(), QStringLiteral("ListDevices"));
    QCOMPARE(requests[0].value(QStringLiteral("kLibUSBMuxVersion")).toInt(), 3);
}

void tst_QUsbmuxClient::listDevicesFallsBackToListen()
{
    FakeUsbmuxd daemon;
    QVERIFY(daemon.start());
    daemon.setSupportsListDevices(false);
    daemon.setDevices({ FakeUsbmuxd::usbDevice(3, serialA), FakeUsbmuxd::usbDevice(5, serialB) });

    QUsbmuxClient client;
    client.setDaemonAddress(daemon.address());
    const QList<QUsbmuxDevice> devices = client.listDevices();
    QCOMPARE(client.error(), QUsbmuxClient::NoError);
    QCOMPARE(devices.size(), 2);
    QCOMPARE(devices[0].deviceId(), 3u);
    QCOMPARE(devices[1].deviceId(), 5u);

    const auto requests = daemon.requests();
    QCOMPARE(requests.size(), 2);
    QCOMPARE(requests[0].value(QStringLiteral("MessageType")).toString(), QStringLiteral("ListDevices"));
    QCOMPARE(requests[1].value(QStringLiteral("MessageType")).toString(), QStringLiteral("Listen"));
}

void tst_QUsbmuxClient::listDevicesWithNoDevices()
{
    FakeUsbmuxd daemon;
    QVERIFY(daemon.start());

    QUsbmuxClient client;
    client.setDaemonAddress(daemon.address());
    QVERIFY(client.listDevices().isEmpty());
    QCOMPARE(client.error(), QUsbmuxClient::NoError);
}

void tst_QUsbmuxClient::listDevicesDaemonUnavailable()
{
    QUsbmuxClient client;
    client.setDaemonAddress(deadAddress());
    QVERIFY(client.listDevices().isEmpty());
    QCOMPARE(client.error(), QUsbmuxClient::DaemonUnavailableError);
    QVERIFY(!client.errorString().isEmpty());
}

void tst_QUsbmuxClient::listDevicesTimesOut()
{
    FakeUsbmuxd daemon;
    QVERIFY(daemon.start());
    daemon.setStalled(true);

    QUsbmuxClient client;
    client.setDaemonAddress(daemon.address());
    QElapsedTimer timer;
    timer.start();
    QVERIFY(client.listDevices(200).isEmpty());
    QCOMPARE(client.error(), QUsbmuxClient::TimeoutError);
    QVERIFY2(timer.elapsed() < 2000, qPrintable(QString::number(timer.elapsed())));
}

void tst_QUsbmuxClient::listDevicesInvalidAddress()
{
    QUsbmuxClient client;
    client.setDaemonAddress(QUsbmuxAddress());
    QVERIFY(client.listDevices().isEmpty());
    QCOMPARE(client.error(), QUsbmuxClient::DaemonUnavailableError);
}

void tst_QUsbmuxClient::watchReportsDevicesAsTheyComeAndGo()
{
    FakeUsbmuxd daemon;
    QVERIFY(daemon.start());
    daemon.setDevices({ FakeUsbmuxd::usbDevice(3, serialA) });

    QUsbmuxClient client;
    client.setDaemonAddress(daemon.address());
    QSignalSpy attached(&client, &QUsbmuxClient::deviceAttached);
    QSignalSpy detached(&client, &QUsbmuxClient::deviceDetached);
    QSignalSpy watching(&client, &QUsbmuxClient::watchingChanged);

    client.startWatching();
    QVERIFY(client.isWatching());
    QCOMPARE(watching.size(), 1);

    // Devices already there are reported first.
    QTRY_COMPARE(attached.size(), 1);
    QCOMPARE(attached[0][0].value<QUsbmuxDevice>().serialNumber(), serialA);

    daemon.attach(FakeUsbmuxd::usbDevice(9, serialB));
    QTRY_COMPARE(attached.size(), 2);
    QCOMPARE(attached[1][0].value<QUsbmuxDevice>().deviceId(), 9u);
    QCOMPARE(client.devices().size(), 2);

    // Detached only carries the ID; the client fills in what it knew.
    daemon.detach(3);
    QTRY_COMPARE(detached.size(), 1);
    const auto gone = detached[0][0].value<QUsbmuxDevice>();
    QCOMPARE(gone.deviceId(), 3u);
    QCOMPARE(gone.serialNumber(), serialA);
    QCOMPARE(client.devices().size(), 1);
    QCOMPARE(client.devices()[0].deviceId(), 9u);

    QCOMPARE(client.error(), QUsbmuxClient::NoError);
}

void tst_QUsbmuxClient::watchFailsWhenDaemonGoes()
{
    FakeUsbmuxd daemon;
    QVERIFY(daemon.start());
    daemon.setDevices({ FakeUsbmuxd::usbDevice(3, serialA) });

    QUsbmuxClient client;
    client.setDaemonAddress(daemon.address());
    QSignalSpy attached(&client, &QUsbmuxClient::deviceAttached);
    QSignalSpy errors(&client, &QUsbmuxClient::errorOccurred);
    QSignalSpy watching(&client, &QUsbmuxClient::watchingChanged);

    client.startWatching();
    QTRY_COMPARE(attached.size(), 1);

    daemon.disconnectAll();
    QTRY_COMPARE(errors.size(), 1);
    QCOMPARE(errors[0][0].value<QUsbmuxClient::Error>(), QUsbmuxClient::DisconnectedError);
    QVERIFY(!client.isWatching());
    QCOMPARE(watching.size(), 2);
    QCOMPARE(watching[1][0].toBool(), false);
    QVERIFY(client.devices().isEmpty());
}

void tst_QUsbmuxClient::watchFailsWhenDaemonUnavailable()
{
    QUsbmuxClient client;
    client.setDaemonAddress(deadAddress());
    QSignalSpy errors(&client, &QUsbmuxClient::errorOccurred);

    client.startWatching();
    QTRY_COMPARE(errors.size(), 1);
    QCOMPARE(errors[0][0].value<QUsbmuxClient::Error>(), QUsbmuxClient::DaemonUnavailableError);
    QVERIFY(!client.isWatching());
}

void tst_QUsbmuxClient::stopWatching()
{
    FakeUsbmuxd daemon;
    QVERIFY(daemon.start());
    daemon.setDevices({ FakeUsbmuxd::usbDevice(3, serialA) });

    QUsbmuxClient client;
    client.setDaemonAddress(daemon.address());
    QSignalSpy attached(&client, &QUsbmuxClient::deviceAttached);
    QSignalSpy errors(&client, &QUsbmuxClient::errorOccurred);

    client.startWatching();
    QTRY_COMPARE(attached.size(), 1);
    client.stopWatching();
    QVERIFY(!client.isWatching());
    QVERIFY(client.devices().isEmpty());

    // Nothing more arrives once stopped.
    daemon.attach(FakeUsbmuxd::usbDevice(9, serialB));
    QTest::qWait(100);
    QCOMPARE(attached.size(), 1);
    QCOMPARE(errors.size(), 0);

    // And it can start again.
    client.startWatching();
    QTRY_COMPARE(attached.size(), 3);
}

QTEST_GUILESS_MAIN(tst_QUsbmuxClient)
#include "tst_qusbmuxclient.moc"
