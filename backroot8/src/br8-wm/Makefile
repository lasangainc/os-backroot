CC = gcc
CFLAGS = -Wall -Wextra -O2 $(shell pkg-config --cflags xft 2>/dev/null)
LDFLAGS = -lX11 $(shell pkg-config --libs xft 2>/dev/null)

br8-wm: br8-wm.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f br8-wm

all: br8-wm
.PHONY: all clean
