.POSIX:

VERSION = 0.1

PREFIX = /usr/local

PKG_CONFIG = pkg-config

PKGS = wayland-client fcft pixman-1
INCS != $(PKG_CONFIG) --cflags $(PKGS)
LIBS != $(PKG_CONFIG) --libs $(PKGS)

CPPFLAGS += -DVERSION=\"$(VERSION)\" -D_DEFAULT_SOURCE
CFLAGS += -Wall $(INCS)
LDLIBS = $(LIBS)

WAYLAND_SCANNER   != $(PKG_CONFIG) --variable=wayland_scanner wayland-scanner
WAYLAND_PROTOCOLS != $(PKG_CONFIG) --variable=pkgdatadir wayland-protocols
PROTOCOLS = xdg-shell-protocol.h wlr-layer-shell-unstable-v1-protocol.h

SRC = swab.c $(PROTOCOLS:.h=.c)
OBJ = $(SRC:.c=.o)

BIN = swab

all: $(BIN)

$(BIN): $(OBJ)
$(OBJ): $(PROTOCOLS)

xdg-shell-protocol.c:
	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

wlr-layer-shell-unstable-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code wlr-layer-shell-unstable-v1.xml $@
wlr-layer-shell-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) client-header wlr-layer-shell-unstable-v1.xml $@
wlr-layer-shell-unstable-v1-protocol.o: xdg-shell-protocol.o


clean:
	rm -f $(BIN) $(OBJ) $(PROTOCOLS:.h=.c) $(PROTOCOLS)

install: all
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)
	
.PHONY: all clean install uninstall
