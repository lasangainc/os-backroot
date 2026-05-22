ROOT = ../..
CC = gcc
CFLAGS = -Wall -Wextra -O2 $(shell pkg-config --cflags xft 2>/dev/null)
LDFLAGS = -lX11 $(shell pkg-config --libs xft 2>/dev/null)
EMBLEM_H = ../br8-panel/emblem.h

br8-wm: br8-wm.c $(EMBLEM_H)
	$(CC) $(CFLAGS) -o $@ br8-wm.c $(LDFLAGS)

clean:
	rm -f br8-wm

all: br8-wm
.PHONY: all clean
