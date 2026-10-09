// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Andrea Bedini

#include "fakeusbmuxd.h"
#include "qusbmuxprotocol_p.h"

#include <QtUsbmuxd/QUsbmuxSocket>

#include <QtCore/qelapsedtimer.h>
#include <QtCore/qthread.h>
#include <QtNetwork/qtcpserver.h>
#include <QtTest/qsignalspy.h>
#include <QtTest/qtest.h>

namespace {

constexpr quint32 device = 3;
constexpr quint16 greeterPort = 47047;
constexpr quint16 echoPort = 7;
constexpr quint16 hangupPort = 9;

QUsbmuxAddress deadAddress()
{
    QTcpServer server;
    server.listen(QHostAddress::LocalHost, 0);
    const quint16 port = server.serverPort();
    server.close();
    return QUsbmuxAddress::tcp(QStringLiteral("127.0.0.1"), port);
}

QByteArray readExactly(QIODevice &d, qint64 size, int msecs = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (d.bytesAvailable() < size && timer.elapsed() < msecs)
        d.waitForReadyRead(int(msecs - timer.elapsed()));
    return d.read(size);
}

} // namespace

class tst_QUsbmuxSocket : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void blockingConnectAndRead_data();
    void blockingConnectAndRead();
    void requestCarriesDeviceAndPort();
    void echo();
    void asyncConnect();
    void worksOnAThreadWithoutAnEventLoop();
    void connectionRefused();
    void unknownDevice();
    void daemonUnavailable();
    void timesOut();
    void remoteCloseKeepsUnreadData();
    void disconnectFromDevice();
    void reconnect();
    void abortWhileConnecting();

private:
    std::unique_ptr<FakeUsbmuxd> m_daemon;
};

void tst_QUsbmuxSocket::init()
{
    m_daemon = std::make_unique<FakeUsbmuxd>();
    QVERIFY(m_daemon->start());
    m_daemon->setDevices({ FakeUsbmuxd::usbDevice(device, QStringLiteral("serial")) });

    FakeUsbmuxd::Service greeter;
    greeter.greeting = "hello";
    m_daemon->addService(device, greeterPort, greeter);

    FakeUsbmuxd::Service echo;
    echo.echo = true;
    m_daemon->addService(device, echoPort, echo);

    FakeUsbmuxd::Service hangup;
    hangup.greeting = "bye";
    hangup.closeAfterGreeting = true;
    m_daemon->addService(device, hangupPort, hangup);
}

void tst_QUsbmuxSocket::cleanup()
{
    m_daemon.reset();
}

void tst_QUsbmuxSocket::blockingConnectAndRead_data()
{
    QTest::addColumn<FakeUsbmuxd::Transport>("transport");
    QTest::newRow("tcp") << FakeUsbmuxd::Transport::Tcp;
    QTest::newRow("local") << FakeUsbmuxd::Transport::Local;
}

// The greeting arrives in the same packet as the Connect reply, so this
// also checks that none of it is lost to the protocol handling.
void tst_QUsbmuxSocket::blockingConnectAndRead()
{
    QFETCH(FakeUsbmuxd::Transport, transport);

    FakeUsbmuxd daemon(transport);
    QString why;
    if (!daemon.start(&why))
        QSKIP(qPrintable(QStringLiteral("can't run the fake daemon here: ") + why));
    daemon.setDevices({ FakeUsbmuxd::usbDevice(device, QStringLiteral("serial")) });
    FakeUsbmuxd::Service greeter;
    greeter.greeting = "hello";
    daemon.addService(device, greeterPort, greeter);

    QUsbmuxSocket socket;
    socket.setDaemonAddress(daemon.address());
    socket.connectToDevice(device, greeterPort);
    QCOMPARE(socket.state(), QUsbmuxSocket::ConnectingState);
    QVERIFY2(socket.waitForConnected(5000), qPrintable(socket.errorString()));
    QCOMPARE(socket.state(), QUsbmuxSocket::ConnectedState);
    QVERIFY(socket.isOpen());
    QCOMPARE(readExactly(socket, 5), QByteArray("hello"));
}

void tst_QUsbmuxSocket::requestCarriesDeviceAndPort()
{
    QUsbmuxSocket socket;
    socket.setDaemonAddress(m_daemon->address());
    socket.connectToDevice(device, greeterPort);
    QVERIFY(socket.waitForConnected(5000));

    const auto requests = m_daemon->requests();
    QCOMPARE(requests.size(), 1);
    QCOMPARE(requests[0].value(QStringLiteral("MessageType")).toString(), QStringLiteral("Connect"));
    QCOMPARE(requests[0].value(QStringLiteral("DeviceID")).toUInt(), device);
    QCOMPARE(requests[0].value(QStringLiteral("PortNumber")).toUInt(), 51127u);
}

void tst_QUsbmuxSocket::echo()
{
    QUsbmuxSocket socket;
    socket.setDaemonAddress(m_daemon->address());
    socket.connectToDevice(device, echoPort);
    QVERIFY(socket.waitForConnected(5000));

    QCOMPARE(socket.write("ping\n"), 5);
    QVERIFY(socket.waitForBytesWritten(5000));
    QCOMPARE(readExactly(socket, 5), QByteArray("ping\n"));

    socket.write("line one\nline two\n");
    QTRY_VERIFY(socket.canReadLine());
    QCOMPARE(socket.readLine(), QByteArray("line one\n"));
}

void tst_QUsbmuxSocket::asyncConnect()
{
    QUsbmuxSocket socket;
    socket.setDaemonAddress(m_daemon->address());
    QSignalSpy connected(&socket, &QUsbmuxSocket::connected);
    QSignalSpy states(&socket, &QUsbmuxSocket::stateChanged);
    QSignalSpy readyRead(&socket, &QIODevice::readyRead);

    socket.connectToDevice(device, greeterPort);
    QTRY_COMPARE(connected.size(), 1);
    QTRY_VERIFY(readyRead.size() >= 1);
    QTRY_COMPARE(socket.bytesAvailable(), 5);
    QCOMPARE(socket.readAll(), QByteArray("hello"));

    QCOMPARE(states.size(), 2);
    QCOMPARE(states[0][0].value<QUsbmuxSocket::SocketState>(), QUsbmuxSocket::ConnectingState);
    QCOMPARE(states[1][0].value<QUsbmuxSocket::SocketState>(), QUsbmuxSocket::ConnectedState);
}

// The case the library was written for: a tracker thread that reads in a
// loop and never runs an event loop.
void tst_QUsbmuxSocket::worksOnAThreadWithoutAnEventLoop()
{
    const QUsbmuxAddress address = m_daemon->address();
    QByteArray received;
    QString error;

    std::unique_ptr<QThread> thread(QThread::create([&] {
        QUsbmuxSocket socket;
        socket.setDaemonAddress(address);
        socket.connectToDevice(device, echoPort);
        if (!socket.waitForConnected(5000)) {
            error = socket.errorString();
            return;
        }
        socket.write("from a thread");
        socket.waitForBytesWritten(5000);
        received = readExactly(socket, 13);
        socket.disconnectFromDevice();
        socket.waitForDisconnected(5000);
    }));
    thread->start();
    QVERIFY(thread->wait(10000));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(received, QByteArray("from a thread"));
}

void tst_QUsbmuxSocket::connectionRefused()
{
    QUsbmuxSocket socket;
    socket.setDaemonAddress(m_daemon->address());
    QSignalSpy errors(&socket, &QUsbmuxSocket::errorOccurred);
    QSignalSpy disconnected(&socket, &QUsbmuxSocket::disconnected);

    socket.connectToDevice(device, 1234);
    QVERIFY(!socket.waitForConnected(5000));
    QCOMPARE(socket.error(), QUsbmuxSocket::ConnectionRefusedError);
    QCOMPARE(socket.state(), QUsbmuxSocket::UnconnectedState);
    QCOMPARE(errors.size(), 1);
    QCOMPARE(disconnected.size(), 0); // it never connected
    QVERIFY(!socket.isOpen());
}

void tst_QUsbmuxSocket::unknownDevice()
{
    QUsbmuxSocket socket;
    socket.setDaemonAddress(m_daemon->address());
    socket.connectToDevice(99, greeterPort);
    QVERIFY(!socket.waitForConnected(5000));
    QCOMPARE(socket.error(), QUsbmuxSocket::DeviceNotFoundError);
}

void tst_QUsbmuxSocket::daemonUnavailable()
{
    QUsbmuxSocket socket;
    socket.setDaemonAddress(deadAddress());
    socket.connectToDevice(device, greeterPort);
    QVERIFY(!socket.waitForConnected(5000));
    QCOMPARE(socket.error(), QUsbmuxSocket::DaemonUnavailableError);
    QCOMPARE(socket.state(), QUsbmuxSocket::UnconnectedState);
}

void tst_QUsbmuxSocket::timesOut()
{
    m_daemon->setStalled(true);
    QUsbmuxSocket socket;
    socket.setDaemonAddress(m_daemon->address());
    socket.connectToDevice(device, greeterPort);

    QElapsedTimer timer;
    timer.start();
    QVERIFY(!socket.waitForConnected(200));
    QCOMPARE(socket.error(), QUsbmuxSocket::TimeoutError);
    QCOMPARE(socket.state(), QUsbmuxSocket::UnconnectedState);
    QVERIFY2(timer.elapsed() < 2000, qPrintable(QString::number(timer.elapsed())));
}

void tst_QUsbmuxSocket::remoteCloseKeepsUnreadData()
{
    QUsbmuxSocket socket;
    socket.setDaemonAddress(m_daemon->address());
    QSignalSpy disconnected(&socket, &QUsbmuxSocket::disconnected);
    QSignalSpy errors(&socket, &QUsbmuxSocket::errorOccurred);

    socket.connectToDevice(device, hangupPort);
    QVERIFY(socket.waitForConnected(5000));
    QTRY_COMPARE(disconnected.size(), 1);
    QCOMPARE(socket.state(), QUsbmuxSocket::UnconnectedState);
    QCOMPARE(socket.error(), QUsbmuxSocket::RemoteClosedError);
    QCOMPARE(errors.size(), 1);

    // As with QTcpSocket, what arrived before the close can still be read.
    QCOMPARE(socket.readAll(), QByteArray("bye"));
    QCOMPARE(socket.bytesAvailable(), 0);
}

void tst_QUsbmuxSocket::disconnectFromDevice()
{
    QUsbmuxSocket socket;
    socket.setDaemonAddress(m_daemon->address());
    QSignalSpy disconnected(&socket, &QUsbmuxSocket::disconnected);
    QSignalSpy errors(&socket, &QUsbmuxSocket::errorOccurred);

    socket.connectToDevice(device, echoPort);
    QVERIFY(socket.waitForConnected(5000));
    socket.disconnectFromDevice();
    QVERIFY(socket.waitForDisconnected(5000));
    QCOMPARE(socket.state(), QUsbmuxSocket::UnconnectedState);
    QCOMPARE(disconnected.size(), 1);
    QCOMPARE(errors.size(), 0);
    QCOMPARE(socket.error(), QUsbmuxSocket::NoError);
}

void tst_QUsbmuxSocket::reconnect()
{
    QUsbmuxSocket socket;
    socket.setDaemonAddress(m_daemon->address());

    socket.connectToDevice(device, 1234);
    QVERIFY(!socket.waitForConnected(5000));
    QCOMPARE(socket.error(), QUsbmuxSocket::ConnectionRefusedError);

    socket.connectToDevice(device, greeterPort);
    QVERIFY(socket.waitForConnected(5000));
    QCOMPARE(socket.error(), QUsbmuxSocket::NoError);
    QCOMPARE(readExactly(socket, 5), QByteArray("hello"));

    socket.close();
    QCOMPARE(socket.state(), QUsbmuxSocket::UnconnectedState);
    QVERIFY(!socket.isOpen());

    socket.connectToDevice(device, echoPort);
    QVERIFY(socket.waitForConnected(5000));
    socket.write("again");
    QCOMPARE(readExactly(socket, 5), QByteArray("again"));
}

void tst_QUsbmuxSocket::abortWhileConnecting()
{
    m_daemon->setStalled(true);
    QUsbmuxSocket socket;
    socket.setDaemonAddress(m_daemon->address());
    QSignalSpy disconnected(&socket, &QUsbmuxSocket::disconnected);
    QSignalSpy errors(&socket, &QUsbmuxSocket::errorOccurred);

    socket.connectToDevice(device, greeterPort);
    socket.abort();
    QCOMPARE(socket.state(), QUsbmuxSocket::UnconnectedState);
    QTest::qWait(50);
    QCOMPARE(disconnected.size(), 0);
    QCOMPARE(errors.size(), 0);
}

QTEST_GUILESS_MAIN(tst_QUsbmuxSocket)
#include "tst_qusbmuxsocket.moc"
