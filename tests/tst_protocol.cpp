// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Andrea Bedini

#include "qusbmuxplist_p.h"
#include "qusbmuxprotocol_p.h"

#include <QtCore/qbuffer.h>
#include <QtCore/qendian.h>
#include <QtCore/qfile.h>
#include <QtTest/qtest.h>

using namespace QUsbmuxProtocol;

namespace {

QByteArray fixture(const char *name)
{
    QFile f(QStringLiteral(FIXTURES_DIR "/") + QString::fromLatin1(name));
    if (!f.open(QIODevice::ReadOnly))
        qFatal("can't open fixture %s", name);
    return f.readAll();
}

quint32 headerField(const QByteArray &packet, int index)
{
    return qFromLittleEndian<quint32>(packet.constData() + 4 * index);
}

} // namespace

class tst_Protocol : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void encodesHeader();
    void portIsInNetworkByteOrder();
    void connectRequestMatchesLibusbmuxd();
    void readWaitsForWholeMessage();
    void readNeverReadsPastTheMessage();
    void readRejectsBadHeaders_data();
    void readRejectsBadHeaders();
    void parsesResultCode();
};

void tst_Protocol::encodesHeader()
{
    const QVariantMap body{ { QStringLiteral("MessageType"), QStringLiteral("Listen") } };
    const QByteArray packet = encode(5, body);

    QCOMPARE(headerField(packet, 0), quint32(packet.size()));
    QCOMPARE(headerField(packet, 1), 1u); // plist protocol
    QCOMPARE(headerField(packet, 2), 8u); // plist message
    QCOMPARE(headerField(packet, 3), 5u); // tag
    QCOMPARE(packet.mid(HeaderSize), QUsbmuxPlist::toXml(body));
}

void tst_Protocol::portIsInNetworkByteOrder()
{
    QCOMPARE(toWirePort(22), quint16(5632));
    QCOMPARE(toWirePort(47047), quint16(51127));
    QCOMPARE(toWirePort(62078), quint16(32498)); // lockdownd
}

// Same fields and values as libusbmuxd, apart from who we say we are.
void tst_Protocol::connectRequestMatchesLibusbmuxd()
{
    auto ours = request(QStringLiteral("Connect"));
    ours.insert(QStringLiteral("DeviceID"), 7);
    ours.insert(QStringLiteral("PortNumber"), toWirePort(47047));
    ours = *QUsbmuxPlist::fromXml(QUsbmuxPlist::toXml(ours));

    auto theirs = *QUsbmuxPlist::fromXml(fixture("libusbmuxd-connect-request.plist"));

    for (const QString &key : { QStringLiteral("ClientVersionString"), QStringLiteral("ProgName") }) {
        QVERIFY(ours.contains(key));
        QVERIFY(theirs.contains(key));
        ours.remove(key);
        theirs.remove(key);
    }
    QCOMPARE(ours, theirs);
}

void tst_Protocol::readWaitsForWholeMessage()
{
    const QByteArray packet = encode(1, { { QStringLiteral("MessageType"), QStringLiteral("Result") },
                                          { QStringLiteral("Number"), 0 } });
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadWrite);

    Message message;
    QString error;
    for (const qsizetype split : { qsizetype(0), qsizetype(10), qsizetype(HeaderSize), packet.size() - 1 }) {
        data = packet.left(split);
        buffer.seek(0);
        QCOMPARE(read(&buffer, &message, &error), ReadStatus::Incomplete);
        QCOMPARE(buffer.pos(), 0); // nothing consumed
    }

    data = packet;
    buffer.seek(0);
    QCOMPARE(read(&buffer, &message, &error), ReadStatus::Ok);
    QCOMPARE(message.tag, 1u);
    QCOMPARE(resultCode(message.body), 0);
}

// After a Connect reply the same connection carries the device's data,
// possibly in the same packet.
void tst_Protocol::readNeverReadsPastTheMessage()
{
    QByteArray data = encode(2, { { QStringLiteral("MessageType"), QStringLiteral("Result") },
                                  { QStringLiteral("Number"), 0 } })
        + QByteArray("device data");
    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadOnly);

    Message message;
    QString error;
    QCOMPARE(read(&buffer, &message, &error), ReadStatus::Ok);
    QCOMPARE(buffer.readAll(), QByteArray("device data"));
}

void tst_Protocol::readRejectsBadHeaders_data()
{
    QTest::addColumn<QByteArray>("packet");

    const auto header = [](quint32 length, quint32 version, quint32 type) {
        QByteArray h(HeaderSize, '\0');
        qToLittleEndian(length, h.data());
        qToLittleEndian(version, h.data() + 4);
        qToLittleEndian(type, h.data() + 8);
        return h;
    };
    QTest::newRow("length below header") << header(8, 1, 8);
    QTest::newRow("absurd length") << header(0x7fffffff, 1, 8);
    QTest::newRow("binary protocol") << header(20, 0, 1) + QByteArray(4, '\0');
    QTest::newRow("not a plist message") << header(16, 1, 1);
    QTest::newRow("garbage body") << header(16 + 5, 1, 8) + QByteArray("<<<<<");
}

void tst_Protocol::readRejectsBadHeaders()
{
    QFETCH(QByteArray, packet);
    QBuffer buffer(&packet);
    buffer.open(QIODevice::ReadOnly);

    Message message;
    QString error;
    QCOMPARE(read(&buffer, &message, &error), ReadStatus::Error);
    QVERIFY(!error.isEmpty());
}

void tst_Protocol::parsesResultCode()
{
    QCOMPARE(QUsbmuxProtocol::resultCode({ { QStringLiteral("MessageType"), QStringLiteral("Result") },
                                           { QStringLiteral("Number"), 3 } }),
             3);
    QCOMPARE(QUsbmuxProtocol::resultCode({ { QStringLiteral("MessageType"), QStringLiteral("Attached") } }), -1);
    QCOMPARE(QUsbmuxProtocol::resultCode({ { QStringLiteral("MessageType"), QStringLiteral("Result") } }), -1);
}

QTEST_APPLESS_MAIN(tst_Protocol)
#include "tst_protocol.moc"
