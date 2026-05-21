CC = gcc
CFLAGS = -Wall -Wextra -O2 $(shell pkg-config --cflags xft 2>/dev/null)
LDFLAGS = -lX11 $(shell pkg-config --libs xft 2>/dev/null)

backroot-hello: backroot-hello.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f backroot-hello

all: backroot-hello
.PHONY: all clean
