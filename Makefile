CC = gcc
LIBS = wayland-client fcft pixman-1
CFLAGS = -g --std=c99 -Wall $(shell pkg-config --cflags $(LIBS))
LDFLAGS = -lm $(shell pkg-config --libs $(LIBS) )

BIN = wb

SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)

PROTOCOLS = $(wildcard protocols/*.xml)
PROTOCOL_SRCS = $(PROTOCOLS:protocols/%.xml=%-protocol.c)
PROTOCOL_OBJS = $(PROTOCOLS:protocols/%.xml=%-protocol.o)
PROTOCOL_HEADERS = $(PROTOCOLS:protocols/%.xml=%-client-protocol.h)

all: $(BIN)

$(BIN): $(PROTOCOL_HEADERS) $(PROTOCOL_OBJS) $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $(BIN)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%-protocol.c: protocols/%.xml
	wayland-scanner private-code $< $@

%-client-protocol.h: protocols/%.xml
	wayland-scanner client-header $< $@

install: $(BIN)
	install -m 0755 $(BIN) /usr/local/bin/$(BIN)

uninstall:
	rm /usr/local/bin/$(BIN)

clean:
	rm -f $(BIN) $(OBJS) $(PROTOCOL_OBJS) $(PROTOCOL_HEADERS) 
