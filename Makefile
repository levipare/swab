CC = gcc
CFLAGS = -Wall `pkg-config --cflags wayland-client pangocairo dbus-1`
LDFLAGS = `pkg-config --libs wayland-client pangocairo dbus-1`

BIN = a.out

SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)

MODULES = $(wildcard modules/*.c)
MODULE_OBJS = $(MODULES:%.c=%.so)

PROTOCOLS = $(wildcard protocols/*.xml)
PROTOCOL_SRCS = $(PROTOCOLS:protocols/%.xml=%-protocol.c)
PROTOCOL_OBJS = $(PROTOCOLS:protocols/%.xml=%-protocol.o)
PROTOCOL_HEADERS = $(PROTOCOLS:protocols/%.xml=%-client-protocol.h)

all: $(PROTOCOL_HEADERS) $(PROTOCOL_SRCS) $(PROTOCOL_OBJS) $(BIN)

$(BIN): $(OBJS) $(MODULE_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) $(MODULE_OBJS) -o $(BIN)

%-protocol.c: protocols/%.xml
	wayland-scanner private-code $< $@

%-client-protocol.h: protocols/%.xml
	wayland-scanner client-header $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

modules/%.so: modules/%.c
	$(CC) $(CFLAGS) $(LDFLAGS) -shared -fPIC $< -o $@


clean:
	rm -f $(BIN) $(OBJS) $(MODULE_OBJS) $(PROTOCOL_HEADERS) $(PROTOCOL_SRCS)
