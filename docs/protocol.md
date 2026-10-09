# The usbmuxd protocol

Apple has never documented usbmuxd's protocol. This is what QtUsbmuxd
relies on, worked out from the behaviour of existing clients
([libusbmuxd], [pymobiledevice3], [usbmux-client]) and checked against
libusbmuxd 2.1.1 on the wire. The [iPhone Wiki page][wiki] has some
history.

[libusbmuxd]: https://github.com/libimobiledevice/libusbmuxd
[pymobiledevice3]: https://github.com/doronz88/pymobiledevice3
[usbmux-client]: https://github.com/httptoolkit/usbmux-client
[wiki]: https://theiphonewiki.com/wiki/Usbmux

## The daemon

A single daemon owns the USB connection to every iOS device and
multiplexes TCP-like connections to ports on them.

| Platform        | Daemon                                    | Address                        |
|-----------------|-------------------------------------------|--------------------------------|
| macOS           | Apple's `usbmuxd`                         | Unix socket `/var/run/usbmuxd` |
| Linux           | libimobiledevice's `usbmuxd`              | Unix socket `/var/run/usbmuxd` |
| Windows         | Apple Mobile Device Service (iTunes / Apple Devices app) | TCP `127.0.0.1:27015` |

libusbmuxd lets `USBMUXD_SOCKET_ADDRESS` override this, as `UNIX:<path>`
or `<host>:<port>`. QtUsbmuxd honours the same variable.

## Framing

Every message, in both directions, is a 16-byte header followed by a body.
The header is four little-endian `uint32`s:

| Offset | Field   | Value                                                     |
|--------|---------|-----------------------------------------------------------|
| 0      | length  | Total message length, header included                     |
| 4      | version | `1` for the plist protocol                                |
| 8      | type    | `8` (plist message)                                       |
| 12     | tag     | Chosen by the client; replies echo it, notifications use 0 |

The body is an XML property list whose root is a `<dict>`. Its
`MessageType` key says what the message is.

Version 0 is an older binary protocol with fixed C structs instead of
plists. A daemon that only speaks version 0 answers a version 1 request
with result 6 (`BADVERSION`). libusbmuxd then falls back to version 0;
QtUsbmuxd doesn't implement it.

The daemon answers XML requests with XML. libusbmuxd only parses XML
replies, and so does usbmux-client, both of them against Apple's daemons.

## Requests

Every request carries these keys:

| Key                   | Value                                        |
|-----------------------|----------------------------------------------|
| `MessageType`         | The request                                  |
| `ClientVersionString` | Free text, used for logging                  |
| `ProgName`            | Free text, used for logging                  |
| `kLibUSBMuxVersion`   | `3`, as libusbmuxd sends                     |

libusbmuxd also sends `BundleID` when it can work one out (on macOS).

Each request needs its own connection. After `Listen` the connection only
carries notifications, and after a successful `Connect` it only carries
the device's data. libusbmuxd opens a fresh connection for every request
anyway.

### `ListDevices`

Reply:

```xml
<dict>
	<key>DeviceList</key>
	<array>
		<dict>
			<key>DeviceID</key>          <integer>3</integer>
			<key>MessageType</key>       <string>Attached</string>
			<key>Properties</key>
			<dict>
				<key>ConnectionSpeed</key> <integer>480000000</integer>
				<key>ConnectionType</key>  <string>USB</string>
				<key>DeviceID</key>        <integer>3</integer>
				<key>LocationID</key>      <integer>336592896</integer>
				<key>ProductID</key>       <integer>4776</integer>
				<key>SerialNumber</key>    <string>00008030-001A2B3C4D5E6F70</string>
			</dict>
		</dict>
	</array>
</dict>
```

`SerialNumber` is the device's UDID. Devices paired for Wi-Fi sync show
up with `ConnectionType` `Network` and extra properties
(`EscapedFullServiceName`, `InterfaceIndex`, `NetworkAddress`, the last a
`<data>` holding a `sockaddr`).

Daemons that predate `ListDevices` answer it with result 1 (`BADCOMMAND`).
The fallback is `Listen`, collecting the `Attached` notifications that
arrive straight away until a short pause. libusbmuxd waits 100 ms.

### `Listen`

Reply: a `Result` (below). After a successful one, the connection carries
notifications with tag 0, starting with one `Attached` for every device
already present:

- `Attached`: same shape as a `ListDevices` entry.
- `Detached`: `{ MessageType: Detached, DeviceID: n }`. Only the ID.
- `Paired`: `{ MessageType: Paired, DeviceID: n }`. The device was
  paired with this host.

### `Connect`

Extra keys:

| Key          | Value                                                       |
|--------------|-------------------------------------------------------------|
| `DeviceID`   | From `ListDevices` or `Attached`                            |
| `PortNumber` | The TCP port **in network byte order**, read as an integer  |

The byte order of `PortNumber` is the classic trap. Port 47047 (`0xB7C7`)
is sent as `<integer>51127</integer>` (`0xC7B7`), and port 22 as 5632.
libusbmuxd sends `htons(port)`.

Reply: a `Result`. If it is 0, the connection from then on carries the
raw byte stream to and from that port on the device, with no more
framing. The device's first bytes can arrive in the same read as the
reply, so a client must take exactly `length` bytes for the reply and
leave the rest.

### `Result`

```xml
<dict>
	<key>MessageType</key> <string>Result</string>
	<key>Number</key>      <integer>0</integer>
</dict>
```

| Number | Meaning                                                  |
|--------|----------------------------------------------------------|
| 0      | OK                                                       |
| 1      | Bad command (e.g. `ListDevices` on an old daemon)        |
| 2      | Bad device: no device with that `DeviceID`               |
| 3      | Connection refused: nothing listening on that port       |
| 6      | Bad version: the daemon wants protocol version 0         |

## Not covered

The daemon also stores pairing records (`ReadBUID`, `ReadPairRecord`,
`SavePairRecord`, `DeletePairRecord`), used by lockdownd clients.
QtUsbmuxd doesn't wrap these yet.
