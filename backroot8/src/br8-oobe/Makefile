ROOT = ../..
CC = gcc
CFLAGS = -Wall -Wextra -O2 -I$(ROOT)/include $(shell pkg-config --cflags xft 2>/dev/null)
LDFLAGS = -lX11 $(shell pkg-config --libs xft 2>/dev/null)

br8-oobe: br8-oobe.c
	$(CC) $(CFLAGS) -o $@ br8-oobe.c $(LDFLAGS)

clean:
	rm -f br8-oobe

all: br8-oobe
.PHONY: all clean
