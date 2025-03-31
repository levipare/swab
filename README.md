# wb
**wb** is a simple status bar for wayland compositors that implement the [layer shell protocol](https://wayland.app/protocols/wlr-layer-shell-unstable-v1#compositor-support).

The status is read from stdin and is expected to be newline terminated and utf-8 encoded.

## Dependencies
- wayland
- pixman
- fcft
- wayland-scanner (for building)
- pkg-config (for building)

## Building
```sh
git clone https://github.com/levipare/wb
cd wb
make
```

## Usage
Pipe your status generating utility into **wb**.
```sh
while date; do sleep 1; done | wb
```

## Configuration
**wb** is configured via command flags. Run `wb --help` to view the options.
> View `man fonts-conf` to see the available font config attributes.

The following sets the font (in pt), foreground color (ARGB), and background color (ARGB).
```sh
wb -f TerminessNerdFont:size=12 -F 0xFFCCCCCC -B 0xFF005555
```
