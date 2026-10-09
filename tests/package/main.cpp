#include <QUsbmuxClient>
#include <QUsbmuxSocket>
#include <QtUsbmuxd/qusbmuxaddress.h>

int main()
{
    // Links and runs; no daemon needed.
    const QUsbmuxAddress address = QUsbmuxAddress::fromString(QStringLiteral("127.0.0.1:27015"));
    QUsbmuxClient client;
    client.setDaemonAddress(address);
    QUsbmuxSocket socket;
    return address.isValid() && client.daemonAddress() == address ? 0 : 1;
}
