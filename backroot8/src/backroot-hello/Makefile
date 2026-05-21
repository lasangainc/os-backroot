CC = gcc
CFLAGS = -Wall -Wextra -O2 $(shell pkg-config --cflags gtk4)
LDFLAGS = $(shell pkg-config --libs gtk4)

backroot-hello: backroot-hello.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f backroot-hello

all: backroot-hello
.PHONY: all clean
