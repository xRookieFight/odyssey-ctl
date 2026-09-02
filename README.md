# odyssey-ctl

DDC/CI control utility and Linux backlight driver for the Samsung Odyssey G5
(G53F, `LS27FG53x`).

The monitor has no hardware buttons worth using and no vendor software on Linux,
but it does implement VESA MCCS 2.1 over DDC/CI. This project talks that
protocol directly: a command line tool for scripting and one-off changes, and an
optional kernel module that registers the panel as a standard backlight device
so desktop brightness controls drive the external screen.

Nothing here is required to make the display work. Mode setting, HDR and colour
depth are handled by the kernel's DRM stack from the EDID. This repository only
covers the OSD settings the display exposes over the DDC lines.

## Status

Developed against a `LS27FG53x` on Fedora 44, kernel 7.1, Intel `i915`. The
protocol implementation is generic; other MCCS 2.1 displays are likely to work,
but only the G53F feature table is shipped.

## Features

| Feature | VCP | Access | Notes |
| --- | --- | --- | --- |
| `brightness` | `0x10` | read/write | 0-100 |
| `contrast` | `0x12` | read/write | 0-100 |
| `color-preset` | `0x14` | read/write | `4000k` `5000k` `6500k` `8200k` `9300k` `user1` |
| `red` `green` `blue` | `0x16` `0x18` `0x1a` | read/write | video gain per channel |
| `input` | `0x60` | read/write | `hdmi-1` `hdmi-2` `dp-1` |
| `volume` | `0x62` | read/write | headphone output level |
| `gamma` | `0x72` | read/write | `1.0` `1.2` `1.4` |
| `h-frequency` `v-frequency` | `0xac` `0xae` | read | active timing |
| `usage-time` | `0xc0` | read | panel power-on hours |
| `firmware` | `0xc9` | read | firmware level |
| `power` | `0xd6` | read/write | the panel only accepts `off` and `hard-off` |

The G53F has no built-in speakers. `volume` drives its 3.5 mm headphone output.

## Building

```sh
make                 # command line tool
make module          # kernel module, needs kernel headers
```

Fedora build dependencies:

```sh
sudo dnf install gcc make kernel-devel
```

## Installing

```sh
sudo make install
```

This installs the binary to `/usr/local/bin`, the manual page, the udev rules
and a systemd user unit.

DDC buses are root-only by default. Join the `i2c` group so the tool works
without `sudo`:

```sh
sudo groupadd -f i2c
sudo usermod -aG i2c "$USER"
sudo udevadm control --reload && sudo udevadm trigger
```

Log out and back in for the group change to take effect.

## Usage

```console
$ odyssey-ctl list
card1-HDMI-A-1       /dev/i2c-4   SAM LS27FG53x
card1-eDP-1          /dev/i2c-12  CMN (unnamed)

$ odyssey-ctl get brightness
brightness = 100 (max 100)

$ odyssey-ctl set brightness 70

$ odyssey-ctl get input
input = hdmi-2 (18, max 14)

$ odyssey-ctl set input dp-1
```

Select a display with `-d` when more than one is connected:

```sh
odyssey-ctl -d card1-HDMI-A-1 set contrast 80
```

`--save` asks the display to persist the change across a power cycle. Most
settings survive without it; some panels ignore the request entirely.

### Profiles

`odyssey-ctl apply` writes every setting listed in a profile. The default
location is `$XDG_CONFIG_HOME/odyssey-ctl/profile.conf`, falling back to
`~/.config/odyssey-ctl/profile.conf`.

```ini
brightness = 75
contrast = 75
color-preset = 6500k
volume = 40
```

To apply it on login:

```sh
systemctl --user enable --now odyssey-ctl.service
```

Unknown or malformed lines are reported and skipped, so a stale entry does not
prevent the rest of the profile from being applied.

## Kernel module

`kernel/odyssey_ddc.c` registers the display's luminance feature as a backlight
class device and exports contrast, colour preset, input source, volume and power
mode through sysfs. With it loaded, a desktop brightness slider drives the
external panel the same way it drives a laptop screen.

Class based I2C instantiation is not available for DDC buses, so the client has
to be created explicitly:

```sh
sudo insmod kernel/odyssey_ddc.ko
echo odyssey-ddc 0x37 | sudo tee /sys/class/drm/card1-HDMI-A-1/ddc/new_device
```

The shipped `61-odyssey-ddc-module.rules` does this automatically for `i915` DDC
buses. It is installed but has no effect unless the module is loaded.

```console
$ cat /sys/class/backlight/odyssey_ddc/brightness
100
$ cat /sys/bus/i2c/drivers/odyssey-ddc/*/contrast
75
```

### Secure Boot

An unsigned module will not load while Secure Boot is enabled. Either sign it
with an enrolled MOK or use the command line tool, which needs no module at all.

## Troubleshooting

**`cannot open /dev/i2c-N: Permission denied`** — the udev rule is not installed
or the `i2c` group change has not taken effect yet. Log out and back in.

**`cannot read brightness: Input/output error`** — the display did not answer.
DDC/CI is timing sensitive; a display that has just woken up or is mid link
training will NAK. Retry after a second.

**`no usable display found`** — no connected DRM connector exposes a DDC bus.
Internal panels never do. Check `odyssey-ctl list`.

**Nothing happens on `set`** — DDC/CI set requests are not acknowledged, so a
rejected value looks like success. Read the feature back to confirm, and check
`odyssey-ctl caps` for the values the display actually advertises.

## Protocol notes

A host request is framed as

```
0x51 | 0x80 | len | payload... | checksum
```

The checksum is the XOR of every preceding byte seeded with the destination
address as it appears on the wire (`0x37 << 1`). Replies are seeded with the
virtual host address `0x50` instead, and their checksum sits immediately after
the payload the header announced, not at the end of the buffer that was read.

VESA DDC/CI 1.1 requires a 40 ms gap before reading a reply and 50 ms after a
set request. The G53F NAKs sporadically below those figures.

## Licence

GPL-2.0-or-later. See [LICENSE](LICENSE).
