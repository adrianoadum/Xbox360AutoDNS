# Xbox360AutoDNS

DashLaunch plugin for an Xbox 360 running BadUpdate or BadAvatar. Until the
exploit runs, the console has no working DNS, so the stock dashboard can't
reach Xbox Live. Once the exploit loads AutoDNS, it switches the console to
the DNS servers in `AutoDNS.ini` (Cloudflare's by default) and the console
goes online.

## Setup

1. Boot the console normally, without running the exploit. On the stock
dashboard, go to System Settings > Network Settings > your network >
Configure Network > DNS Settings > Manual and set both servers to
`192.0.2.1`. The console saves this setting and uses it on every boot, so you
only do this once.

> [!IMPORTANT]
> Run Test Xbox Live Connection and check it fails at DNS. If it passes, your
> router is resolving names for the console instead of the dead server you
> set. Turn off DNS redirection in the router until the test fails.

2. Download `AutoDNS.xex` and `AutoDNS.ini` from the
[latest release](https://github.com/dclstn/Xbox360AutoDNS/releases/latest)
and copy both to the root of the USB stick. Add `AutoDNS.xex` to `launch.ini`
above any plugin that needs the network.

```ini
[Plugins]
plugin1 = Usb:\xbdm.xex
plugin2 = Usb:\AutoDNS.xex
plugin3 = Usb:\xbGuard.xex
plugin4 = Usb:\JRPC2.xex
```

3. Run BadUpdate or BadAvatar as usual. When AutoDNS loads, it switches the
DNS, the console goes online, and a notification shows the servers.

## AutoDNS.ini

AutoDNS takes its settings from `AutoDNS.ini`, in the same folder as
`AutoDNS.xex`:

```ini
dns1 = 1.1.1.1
dns2 = 1.0.0.1
notify = 1
```

- `dns1` is required.
- `dns2` is optional. Leave it out and the console gets only one server.
- `notify` is optional. `notify = 0` turns the notifications off.
- Lines that start with `;` are comments.

Other public resolvers:

| Resolver   | `dns1`         | `dns2`          |
|------------|----------------|-----------------|
| Cloudflare | 1.1.1.1        | 1.0.0.1         |
| Google     | 8.8.8.8        | 8.8.4.4         |
| Quad9      | 9.9.9.9        | 149.112.112.112 |
| OpenDNS    | 208.67.222.222 | 208.67.220.220  |

AutoDNS reads the file each time it loads. If the file is missing, has no
`dns1`, or holds a bad value, AutoDNS changes nothing and the console stays
offline.

### Notifications

| Notification | Meaning |
| --- | --- |
| `DNS set to 1.1.1.1, 1.0.0.1` | The console uses the new servers. |
| `AutoDNS.ini not found` | There's no `AutoDNS.ini` next to `AutoDNS.xex`. |
| `AutoDNS.ini is invalid` | The file has no `dns1`, has a bad value, or is 512 bytes or more. |
| `network startup failed` | AutoDNS couldn't start the console's network API. |
| `no network address after 90 s` | The console didn't connect to the network. Check the cable or Wi-Fi. |
| `can't read network settings` | AutoDNS couldn't read the stored network settings. |
| `No network address after 30 s` | AutoDNS set the servers, but the network didn't come back. |

A failure leaves the DNS unchanged. If the file is missing or invalid,
AutoDNS shows the notification even when the file says `notify = 0`.

## How it works

`192.0.2.1` is a documentation address ([RFC 5737](https://www.rfc-editor.org/info/rfc5737/))
with no DNS server behind it. While the exploit isn't running, the console
can't resolve a single Live hostname. After the exploit loads the plugin,
AutoDNS decrypts the stored network settings with `XnpLoadConfigParams`, puts
the servers from `AutoDNS.ini` in place of the dead server, and applies them
with `XnpConfig`. `XnpConfig` only changes the running network stack and never
writes to storage. The next boot starts offline again.

## Build

You need the Xbox 360 XDK. Point `XEDK` at the SDK folder and run
`./build.sh`. The result is `build/AutoDNS.xex`, with a copy of `AutoDNS.ini`
next to it.

### Without Windows

`docker/` runs the same build under Wine in a container. You need Docker
and the XDK 21256 installer, `XDKSetupXenon21256.*.exe`.

Unpack the XDK into the `autodns-xdk` Docker volume once. Pass the folder
that holds the installer:

```bash
docker/setup-xdk.sh ~/Downloads
```

Then build. `docker/run.sh` runs a command in the container, `./build.sh`
when you give none:

```bash
docker/run.sh
```
