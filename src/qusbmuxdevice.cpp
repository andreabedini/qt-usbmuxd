// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Andrea Bedini

#include <QtUsbmuxd/qusbmuxdevice.h>

#include <QtCore/qdebug.h>

class QUsbmuxDevicePrivate : public QSharedData
{
public:
    quint32 deviceId = 0;
    bool valid = false;
    QString serialNumber;
    QUsbmuxDevice::ConnectionType connectionType = QUsbmuxDevice::UnknownConnection;
    quint16 productId = 0;
    quint32 locationId = 0;
    quint64 connectionSpeed = 0;
    QVariantMap properties;
};

QUsbmuxDevice::QUsbmuxDevice()
    : d(new QUsbmuxDevicePrivate)
{
}

QUsbmuxDevice::QUsbmuxDevice(const QUsbmuxDevice &other) = default;
QUsbmuxDevice::QUsbmuxDevice(QUsbmuxDevice &&other) noexcept = default;
QUsbmuxDevice &QUsbmuxDevice::operator=(const QUsbmuxDevice &other) = default;
QUsbmuxDevice &QUsbmuxDevice::operator=(QUsbmuxDevice &&other) noexcept = default;
QUsbmuxDevice::~QUsbmuxDevice() = default;

QUsbmuxDevice QUsbmuxDevice::fromProperties(const QVariantMap &properties)
{
    QUsbmuxDevice device;

    bool ok = false;
    const quint32 id = properties.value(QStringLiteral("DeviceID")).toUInt(&ok);
    if (!ok)
        return device;

    QUsbmuxDevicePrivate *p = device.d.data();
    p->valid = true;
    p->deviceId = id;
    p->serialNumber = properties.value(QStringLiteral("SerialNumber")).toString();
    p->productId = quint16(properties.value(QStringLiteral("ProductID")).toUInt());
    p->locationId = properties.value(QStringLiteral("LocationID")).toUInt();
    p->connectionSpeed = properties.value(QStringLiteral("ConnectionSpeed")).toULongLong();
    p->properties = properties;

    const QString type = properties.value(QStringLiteral("ConnectionType")).toString();
    if (type == u"USB")
        p->connectionType = Usb;
    else if (type == u"Network")
        p->connectionType = Network;

    return device;
}

bool QUsbmuxDevice::isValid() const { return d->valid; }
quint32 QUsbmuxDevice::deviceId() const { return d->deviceId; }
QString QUsbmuxDevice::serialNumber() const { return d->serialNumber; }
QUsbmuxDevice::ConnectionType QUsbmuxDevice::connectionType() const { return d->connectionType; }
quint16 QUsbmuxDevice::productId() const { return d->productId; }
quint32 QUsbmuxDevice::locationId() const { return d->locationId; }
quint64 QUsbmuxDevice::connectionSpeed() const { return d->connectionSpeed; }
QVariantMap QUsbmuxDevice::properties() const { return d->properties; }

bool operator==(const QUsbmuxDevice &a, const QUsbmuxDevice &b)
{
    return a.d == b.d
        || (a.d->valid == b.d->valid && a.d->deviceId == b.d->deviceId
            && a.d->properties == b.d->properties);
}

QDebug operator<<(QDebug debug, const QUsbmuxDevice &device)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "QUsbmuxDevice(";
    if (device.isValid()) {
        debug << device.deviceId() << ", " << device.serialNumber() << ", "
              << device.connectionType();
    } else {
        debug << "invalid";
    }
    debug << ')';
    return debug;
}
