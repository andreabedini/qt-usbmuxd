// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Andrea Bedini

#include "qusbmuxplist_p.h"

#include <QtCore/qdatetime.h>
#include <QtCore/qfile.h>
#include <QtTest/qtest.h>

namespace {

QByteArray fixture(const char *name)
{
    QFile f(QStringLiteral(FIXTURES_DIR "/") + QString::fromLatin1(name));
    if (!f.open(QIODevice::ReadOnly))
        qFatal("can't open fixture %s", name);
    return f.readAll();
}

} // namespace

class tst_Plist : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesLibplistListDevicesReply();
    void parsesLibusbmuxdRequests();
    void parsesNotificationsAndResults();
    void roundTrips();
    void writesWhatLibplistWrites();
    void rejectsBinaryPlists();
    void rejectsMalformed_data();
    void rejectsMalformed();
};

// Written by libplist, which is what Linux usbmuxd uses to encode replies.
void tst_Plist::parsesLibplistListDevicesReply()
{
    QString error;
    const auto body = QUsbmuxPlist::fromXml(fixture("listdevices-reply.plist"), &error);
    QVERIFY2(body, qPrintable(error));

    const QVariantList list = body->value(QStringLiteral("DeviceList")).toList();
    QCOMPARE(list.size(), 2);

    const QVariantMap usb = list[0].toMap().value(QStringLiteral("Properties")).toMap();
    QCOMPARE(usb.value(QStringLiteral("DeviceID")).toUInt(), 3u);
    QCOMPARE(usb.value(QStringLiteral("ConnectionType")).toString(), QStringLiteral("USB"));
    QCOMPARE(usb.value(QStringLiteral("ConnectionSpeed")).toLongLong(), 480000000);
    QCOMPARE(usb.value(QStringLiteral("LocationID")).toLongLong(), 336592896);
    QCOMPARE(usb.value(QStringLiteral("ProductID")).toInt(), 4776);
    QCOMPARE(usb.value(QStringLiteral("SerialNumber")).toString(),
             QStringLiteral("00008030-001A2B3C4D5E6F70"));

    // libplist wraps <data> across lines and indents it with tabs.
    const QVariantMap net = list[1].toMap().value(QStringLiteral("Properties")).toMap();
    QByteArray expected("\x1c\x1e", 2);
    for (char c = 0; c < 26; ++c)
        expected.append(c);
    QCOMPARE(net.value(QStringLiteral("NetworkAddress")).typeId(), QMetaType::QByteArray);
    QCOMPARE(net.value(QStringLiteral("NetworkAddress")).toByteArray(), expected);
    QCOMPARE(net.value(QStringLiteral("InterfaceIndex")).toInt(), 12);
}

// Captured from libusbmuxd 2.1.1 talking to a fake daemon.
void tst_Plist::parsesLibusbmuxdRequests()
{
    const auto connect = QUsbmuxPlist::fromXml(fixture("libusbmuxd-connect-request.plist"));
    QVERIFY(connect);
    QCOMPARE(connect->value(QStringLiteral("MessageType")).toString(), QStringLiteral("Connect"));
    QCOMPARE(connect->value(QStringLiteral("DeviceID")).toUInt(), 7u);
    QCOMPARE(connect->value(QStringLiteral("PortNumber")).toUInt(), 51127u); // 47047, byte-swapped
    QCOMPARE(connect->value(QStringLiteral("kLibUSBMuxVersion")).toInt(), 3);

    const auto list = QUsbmuxPlist::fromXml(fixture("libusbmuxd-listdevices-request.plist"));
    QVERIFY(list);
    QCOMPARE(list->value(QStringLiteral("MessageType")).toString(), QStringLiteral("ListDevices"));
}

void tst_Plist::parsesNotificationsAndResults()
{
    const auto attached = QUsbmuxPlist::fromXml(fixture("attached.plist"));
    QVERIFY(attached);
    QCOMPARE(attached->value(QStringLiteral("MessageType")).toString(), QStringLiteral("Attached"));
    QCOMPARE(attached->value(QStringLiteral("Properties")).toMap().value(QStringLiteral("SerialNumber")).toString(),
             QStringLiteral("00008030-001A2B3C4D5E6F70"));

    const auto detached = QUsbmuxPlist::fromXml(fixture("detached.plist"));
    QVERIFY(detached);
    QCOMPARE(detached->value(QStringLiteral("MessageType")).toString(), QStringLiteral("Detached"));
    QCOMPARE(detached->value(QStringLiteral("DeviceID")).toUInt(), 3u);

    const auto refused = QUsbmuxPlist::fromXml(fixture("result-refused.plist"));
    QVERIFY(refused);
    QCOMPARE(refused->value(QStringLiteral("Number")).toInt(), 3);
}

void tst_Plist::roundTrips()
{
    const QVariantMap original{
        { QStringLiteral("string"), QStringLiteral("héllo <&> \"world\"") },
        { QStringLiteral("empty"), QString() },
        { QStringLiteral("int"), qint64(-42) },
        { QStringLiteral("big"), quint64(18446744073709551615ull) },
        { QStringLiteral("real"), 3.25 },
        { QStringLiteral("yes"), true },
        { QStringLiteral("no"), false },
        { QStringLiteral("data"), QByteArray("\0\1\2\xff", 4) },
        { QStringLiteral("date"), QDateTime(QDate(2026, 10, 9), QTime(12, 34, 56), QTimeZone::UTC) },
        { QStringLiteral("array"), QVariantList{ qint64(1), QStringLiteral("two"), QVariantList{} } },
        { QStringLiteral("dict"), QVariantMap{ { QStringLiteral("nested"), qint64(1) } } },
    };

    QString error;
    const auto parsed = QUsbmuxPlist::fromXml(QUsbmuxPlist::toXml(original), &error);
    QVERIFY2(parsed, qPrintable(error));
    QCOMPARE(*parsed, original);
}

// Not required by the protocol, but it makes captures easy to diff.
void tst_Plist::writesWhatLibplistWrites()
{
    const QVariantMap body{
        { QStringLiteral("MessageType"), QStringLiteral("Result") },
        { QStringLiteral("Number"), 0 },
    };
    QCOMPARE(QUsbmuxPlist::toXml(body), fixture("result-ok.plist"));
}

void tst_Plist::rejectsBinaryPlists()
{
    QString error;
    QVERIFY(!QUsbmuxPlist::fromXml(fixture("listdevices-reply.bplist"), &error));
    QVERIFY2(error.contains(QStringLiteral("binary")), qPrintable(error));
}

void tst_Plist::rejectsMalformed_data()
{
    QTest::addColumn<QByteArray>("xml");

    QTest::newRow("empty") << QByteArray();
    QTest::newRow("not xml") << QByteArray("hello");
    QTest::newRow("not a plist") << QByteArray("<html/>");
    QTest::newRow("empty plist") << QByteArray("<plist version=\"1.0\"></plist>");
    QTest::newRow("top level array") << QByteArray("<plist><array/></plist>");
    QTest::newRow("unclosed") << QByteArray("<plist><dict><key>a</key><string>b</string>");
    QTest::newRow("value without key") << QByteArray("<plist><dict><string>b</string></dict></plist>");
    QTest::newRow("key without value") << QByteArray("<plist><dict><key>a</key></dict></plist>");
    QTest::newRow("bad integer") << QByteArray("<plist><dict><key>a</key><integer>x</integer></dict></plist>");
    QTest::newRow("bad base64") << QByteArray("<plist><dict><key>a</key><data>!!!!</data></dict></plist>");
    QTest::newRow("unknown element") << QByteArray("<plist><dict><key>a</key><uid>1</uid></dict></plist>");
}

void tst_Plist::rejectsMalformed()
{
    QFETCH(QByteArray, xml);
    QString error;
    QVERIFY(!QUsbmuxPlist::fromXml(xml, &error));
    QVERIFY(!error.isEmpty());
}

QTEST_APPLESS_MAIN(tst_Plist)
#include "tst_plist.moc"
