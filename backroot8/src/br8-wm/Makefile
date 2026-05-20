CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lX11

br8-wm: br8-wm.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f br8-wm

all: br8-wm
.PHONY: all clean
