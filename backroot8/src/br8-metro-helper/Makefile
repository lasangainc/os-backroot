ROOT = ../..
CC = gcc
CFLAGS = -Wall -Wextra -O2 $(shell pkg-config --cflags xft 2>/dev/null)
LDFLAGS = -lX11 $(shell pkg-config --libs xft 2>/dev/null)
EMBLEM_H = ../br8-panel/emblem.h

br8-metro-helper: br8-metro-helper.c $(EMBLEM_H)
	$(CC) $(CFLAGS) -o $@ br8-metro-helper.c $(LDFLAGS)

clean:
	rm -f br8-metro-helper

all: br8-metro-helper
.PHONY: all clean
