CC = gcc
CFLAGS = -Wall -Wextra -O2 $(shell pkg-config --cflags xft 2>/dev/null)
LDFLAGS = -lX11 -lm $(shell pkg-config --libs xft 2>/dev/null)

br8-lock: br8-lock.c
	$(CC) $(CFLAGS) -o $@ br8-lock.c $(LDFLAGS)

clean:
	rm -f br8-lock

all: br8-lock
.PHONY: all clean
