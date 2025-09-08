# swab

**swab** is a simple status bar for wayland compositors that implement the [layer shell protocol](https://wayland.app/protocols/wlr-layer-shell-unstable-v1#compositor-support).
The status is read from stdin and is expected to be newline terminated and utf-8 encoded.

![image](https://github.com/user-attachments/assets/8f08ad85-897e-4ef7-b9bd-516c0f170de7)

> Works well with [muxst](https://github.com/levipare/muxst)

## Dependencies
- wayland
- pixman
- fcft
- wayland-scanner (for building)
- pkg-config (for building)

## Building
```sh
git clone https://github.com/levipare/swab
cd swab
make
```

## Usage
Pipe your status generating utility into **swab**.
```sh
while date; do sleep 1; done | swab
```

## Configuration
**swab** is configured via command flags. Run `swab -h` to view the options.
> View `man fonts-conf` to see the available font config attributes.

The following sets the font (in pt), foreground color (RGBA), and background color (RGBA).
```sh
swab -f TerminessNerdFont:size=12 -F 0xccccccff -B 0x005555ff
```
