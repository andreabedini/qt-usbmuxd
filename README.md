# QtUsbmuxd

A Qt 6 library for talking to iOS devices over USB through the usbmuxd
daemon. It discovers devices, watches them come and go, and opens
TCP-like connections to ports on them.

It speaks the daemon's protocol itself, so it needs nothing beyond Qt
Core and Qt Network. There's no libusbmuxd, libplist or
libimobiledevice-glue to build, ship or link.

> **Status: early.** Its requests carry the same fields and values as
> libusbmuxd's (checked against captures from libusbmuxd 2.1.1), and it
> is tested against a fake daemon. It has not yet been run
> against Apple's daemon on macOS or Windows, or against a real device.
> Reports welcome.

```cpp
#include <QUsbmuxClient>
#include <QUsbmuxSocket>

QUsbmuxClient client;
const QList<QUsbmuxDevice> devices = client.listDevices();
if (devices.isEmpty())
    return;

QUsbmuxSocket socket;
socket.connectToDevice(devices.first(), 47047);
if (!socket.waitForConnected())
    qWarning() << socket.errorString();

while (socket.waitForReadyRead())
    process(socket.readAll());
```

## Requirements

- Qt 6.5 or later (Core, Network)
- A C++17 compiler
- At runtime, the usbmuxd daemon:
  - **macOS**: built in.
  - **Windows**: Apple Mobile Device Service, which comes with iTunes or
    the Apple Devices app.
  - **Linux**: [usbmuxd](https://github.com/libimobiledevice/usbmuxd),
    packaged by most distributions.

## Using it

### Finding devices

`listDevices()` blocks until the daemon replies, so it works on any
thread, with or without an event loop:

```cpp
QUsbmuxClient client;
for (const QUsbmuxDevice &device : client.listDevices())
    qDebug() << device.deviceId() << device.serialNumber() << device.connectionType();
if (client.error() != QUsbmuxClient::NoError)
    qWarning() << client.errorString();
```

To follow devices as they're plugged in and out, watch instead. This
needs an event loop. Devices already attached are reported first:

```cpp
auto *client = new QUsbmuxClient(this);
connect(client, &QUsbmuxClient::deviceAttached, this, [](const QUsbmuxDevice &d) { ... });
connect(client, &QUsbmuxClient::deviceDetached, this, [](const QUsbmuxDevice &d) { ... });
connect(client, &QUsbmuxClient::errorOccurred, this, [client] { qWarning() << client->errorString(); });
client->startWatching();
```

A device's `deviceId()` is only valid while it stays attached. Use
`serialNumber()` (the UDID) to recognise it after a reconnect.

### Connecting to a port

`QUsbmuxSocket` is a `QIODevice` and behaves like `QTcpSocket`. It works
asynchronously:

```cpp
auto *socket = new QUsbmuxSocket(this);
connect(socket, &QUsbmuxSocket::connected, this, [] { ... });
connect(socket, &QIODevice::readyRead, this, [socket] { handle(socket->readAll()); });
connect(socket, &QUsbmuxSocket::errorOccurred, this, [socket] { qWarning() << socket->errorString(); });
socket->connectToDevice(device, port);
```

It also works synchronously, which suits a worker thread without an event
loop:

```cpp
QUsbmuxSocket socket;
socket.connectToDevice(device, port);
if (!socket.waitForConnected(1000))
    return;
while (!isInterruptionRequested()) {
    if (socket.waitForReadyRead(10))
        handle(socket.readAll());
    else if (socket.state() != QUsbmuxSocket::ConnectedState)
        break;
}
```

`ConnectionRefusedError` means nothing on the device is listening on that
port, usually because the app isn't running. Data that arrives right
before the device closes the connection can still be read after
`disconnected()`.

### Daemon address

Both classes use `QUsbmuxAddress::systemDefault()`. That's
`USBMUXD_SOCKET_ADDRESS` if set (the same variable libusbmuxd reads,
`UNIX:<path>` or `<host>:<port>`), otherwise the platform's standard
address. `setDaemonAddress()` overrides it.

## Building

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

Options:

| Option                     | Default                  |                              |
|----------------------------|--------------------------|------------------------------|
| `BUILD_SHARED_LIBS`        | `OFF`                    | Shared instead of static     |
| `QTUSBMUXD_BUILD_TESTS`    | on when top-level        |                              |
| `QTUSBMUXD_BUILD_EXAMPLES` | on when top-level        | The `usbmux` command-line tool |
| `QTUSBMUXD_INSTALL`        | on when top-level        | Install rules and CMake package |

The tests run against a fake daemon, so they need no device.

### In a CMake project

Installed:

```cmake
find_package(QtUsbmuxd REQUIRED)
target_link_libraries(myapp PRIVATE QtUsbmuxd::QtUsbmuxd)
```

Vendored (e.g. as a git submodule), or with `FetchContent`:

```cmake
add_subdirectory(third-party/qt-usbmuxd)
target_link_libraries(myapp PRIVATE QtUsbmuxd::QtUsbmuxd)
```

When it's a subproject, tests, examples and install rules are off by
default, and it builds as a static library unless `BUILD_SHARED_LIBS` is
on.

## The `usbmux` tool

```
usbmux list                  list attached devices
usbmux watch                 report devices as they come and go
usbmux cat <device> <port>   print what a port on the device sends
```

`<device>` is a device ID, a UDID, or `-` for the first USB device.

## Protocol

[docs/protocol.md](docs/protocol.md) describes the protocol, as far as
it's known: Apple doesn't document it.

## License

MIT. See [LICENSE](LICENSE).
