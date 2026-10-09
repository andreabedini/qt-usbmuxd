// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Andrea Bedini

#include <QtUsbmuxd/QUsbmuxAddress>

#include <QtTest/qtest.h>

class tst_QUsbmuxAddress : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void fromString_data();
    void fromString();
    void platformDefault();
    void systemDefaultUsesEnvironment();
    void systemDefaultIgnoresMalformedEnvironment();
};

void tst_QUsbmuxAddress::fromString_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QUsbmuxAddress>("expected");
    QTest::addColumn<QString>("roundTrip");

    QTest::newRow("unix") << QStringLiteral("UNIX:/var/run/usbmuxd")
                          << QUsbmuxAddress::local(QStringLiteral("/var/run/usbmuxd"))
                          << QStringLiteral("UNIX:/var/run/usbmuxd");
    QTest::newRow("ipv4") << QStringLiteral("127.0.0.1:27015")
                          << QUsbmuxAddress::tcp(QStringLiteral("127.0.0.1"), 27015)
                          << QStringLiteral("127.0.0.1:27015");
    QTest::newRow("hostname") << QStringLiteral("localhost:5000")
                              << QUsbmuxAddress::tcp(QStringLiteral("localhost"), 5000)
                              << QStringLiteral("localhost:5000");
    QTest::newRow("ipv6") << QStringLiteral("[::1]:27015")
                          << QUsbmuxAddress::tcp(QStringLiteral("::1"), 27015)
                          << QStringLiteral("[::1]:27015");

    for (const char *bad : { "", "nonsense", ":27015", "host:", "host:0", "host:99999", "host:12x", "UNIX:" })
        QTest::newRow(bad) << QString::fromLatin1(bad) << QUsbmuxAddress() << QString();
}

void tst_QUsbmuxAddress::fromString()
{
    QFETCH(QString, input);
    QFETCH(QUsbmuxAddress, expected);
    QFETCH(QString, roundTrip);

    const QUsbmuxAddress parsed = QUsbmuxAddress::fromString(input);
    QCOMPARE(parsed, expected);
    QCOMPARE(parsed.toString(), roundTrip);
}

void tst_QUsbmuxAddress::platformDefault()
{
    const QUsbmuxAddress a = QUsbmuxAddress::platformDefault();
#ifdef Q_OS_WIN
    QCOMPARE(a, QUsbmuxAddress::tcp(QStringLiteral("127.0.0.1"), 27015));
#else
    QCOMPARE(a, QUsbmuxAddress::local(QStringLiteral("/var/run/usbmuxd")));
#endif
}

void tst_QUsbmuxAddress::systemDefaultUsesEnvironment()
{
    qputenv("USBMUXD_SOCKET_ADDRESS", "UNIX:/tmp/elsewhere");
    QCOMPARE(QUsbmuxAddress::systemDefault(), QUsbmuxAddress::local(QStringLiteral("/tmp/elsewhere")));
    qunsetenv("USBMUXD_SOCKET_ADDRESS");
    QCOMPARE(QUsbmuxAddress::systemDefault(), QUsbmuxAddress::platformDefault());
}

void tst_QUsbmuxAddress::systemDefaultIgnoresMalformedEnvironment()
{
    qputenv("USBMUXD_SOCKET_ADDRESS", "nonsense");
    QTest::ignoreMessage(QtWarningMsg,
                         "QUsbmuxAddress: ignoring malformed USBMUXD_SOCKET_ADDRESS \"nonsense\"");
    QCOMPARE(QUsbmuxAddress::systemDefault(), QUsbmuxAddress::platformDefault());
    qunsetenv("USBMUXD_SOCKET_ADDRESS");
}

QTEST_APPLESS_MAIN(tst_QUsbmuxAddress)
#include "tst_qusbmuxaddress.moc"
