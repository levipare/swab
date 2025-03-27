CC = gcc
CFLAGS = -g --std=c99 -Wall $(shell pkg-config --cflags wayland-client pangocairo dbus-1)
LDFLAGS = $(shell pkg-config --libs wayland-client pangocairo dbus-1)

BIN = wb

SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)

PROTOCOLS = $(wildcard protocols/*.xml)
PROTOCOL_SRCS = $(PROTOCOLS:protocols/%.xml=%-protocol.c)
PROTOCOL_OBJS = $(PROTOCOLS:protocols/%.xml=%-protocol.o)
PROTOCOL_HEADERS = $(PROTOCOLS:protocols/%.xml=%-client-protocol.h)

all: $(PROTOCOL_HEADERS) $(BIN)

$(BIN): $(OBJS) $(PROTOCOL_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $(BIN)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%-protocol.c: protocols/%.xml
	wayland-scanner private-code $< $@

%-client-protocol.h: protocols/%.xml
	wayland-scanner client-header $< $@


clean:
	rm -f $(BIN) $(OBJS) $(PROTOCOL_OBJS) $(PROTOCOL_HEADERS) 
