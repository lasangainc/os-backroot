CC = gcc
CFLAGS = -Wall -Wextra -O2 $(shell pkg-config --cflags gtk4 gdk-pixbuf-2.0)
LDFLAGS = $(shell pkg-config --libs gtk4 gdk-pixbuf-2.0)

br8-start-menu: br8-start-menu.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f br8-start-menu

all: br8-start-menu

.PHONY: all clean
