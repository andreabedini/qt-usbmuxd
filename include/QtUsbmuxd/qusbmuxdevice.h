// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Andrea Bedini

#pragma once

#include <QtUsbmuxd/qtusbmuxdglobal.h>

#include <QtCore/qlist.h>
#include <QtCore/qmetatype.h>
#include <QtCore/qobjectdefs.h>
#include <QtCore/qshareddata.h>
#include <QtCore/qstring.h>
#include <QtCore/qvariant.h>

QT_BEGIN_NAMESPACE
class QDebug;
QT_END_NAMESPACE

class QUsbmuxDevicePrivate;

// An iOS device as reported by usbmuxd.
class QTUSBMUXD_EXPORT QUsbmuxDevice
{
    Q_GADGET
    Q_PROPERTY(quint32 deviceId READ deviceId CONSTANT)
    Q_PROPERTY(QString serialNumber READ serialNumber CONSTANT)
    Q_PROPERTY(ConnectionType connectionType READ connectionType CONSTANT)
    Q_PROPERTY(quint16 productId READ productId CONSTANT)
    Q_PROPERTY(quint32 locationId READ locationId CONSTANT)
    Q_PROPERTY(quint64 connectionSpeed READ connectionSpeed CONSTANT)

public:
    enum ConnectionType {
        UnknownConnection,
        Usb,
        Network,
    };
    Q_ENUM(ConnectionType)

    QUsbmuxDevice();
    QUsbmuxDevice(const QUsbmuxDevice &other);
    QUsbmuxDevice(QUsbmuxDevice &&other) noexcept;
    QUsbmuxDevice &operator=(const QUsbmuxDevice &other);
    QUsbmuxDevice &operator=(QUsbmuxDevice &&other) noexcept;
    ~QUsbmuxDevice();

    void swap(QUsbmuxDevice &other) noexcept { d.swap(other.d); }

    // Builds a device from the "Properties" dictionary usbmuxd sends in
    // ListDevices replies and Attached notifications. Returns an invalid
    // device if there is no DeviceID.
    static QUsbmuxDevice fromProperties(const QVariantMap &properties);

    bool isValid() const;

    // usbmuxd's handle for the device. Only stable while the device stays
    // attached; use serialNumber() to recognise a device across reconnects.
    quint32 deviceId() const;
    // The device's UDID.
    QString serialNumber() const;
    ConnectionType connectionType() const;
    // USB product ID; 0 for network devices.
    quint16 productId() const;
    quint32 locationId() const;
    // In bits per second, as reported by the daemon; 0 if unknown.
    quint64 connectionSpeed() const;

    // Everything the daemon reported, including properties without an
    // accessor above (e.g. "EscapedFullServiceName" for network devices).
    QVariantMap properties() const;

    friend QTUSBMUXD_EXPORT bool operator==(const QUsbmuxDevice &a, const QUsbmuxDevice &b);
    friend bool operator!=(const QUsbmuxDevice &a, const QUsbmuxDevice &b) { return !(a == b); }

private:
    QSharedDataPointer<QUsbmuxDevicePrivate> d;
};

QTUSBMUXD_EXPORT QDebug operator<<(QDebug debug, const QUsbmuxDevice &device);

Q_DECLARE_SHARED(QUsbmuxDevice)
Q_DECLARE_METATYPE(QUsbmuxDevice)
