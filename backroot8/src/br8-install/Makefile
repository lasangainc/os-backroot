ROOT = ../..
CC = gcc
CFLAGS = -Wall -Wextra -O2 $(shell pkg-config --cflags xft 2>/dev/null)
LDFLAGS = -lX11 -lm $(shell pkg-config --libs xft 2>/dev/null)

br8-install: br8-install.c
	$(CC) $(CFLAGS) -o $@ br8-install.c $(LDFLAGS)

clean:
	rm -f br8-install

all: br8-install
.PHONY: all clean
