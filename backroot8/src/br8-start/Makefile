CC = gcc
CFLAGS = -Wall -Wextra -O2 $(shell pkg-config --cflags xft 2>/dev/null)
LDFLAGS = -lX11 -lm $(shell pkg-config --libs xft 2>/dev/null)

br8-start: br8-start.c stb_image.h
	$(CC) $(CFLAGS) -o $@ br8-start.c $(LDFLAGS)

clean:
	rm -f br8-start

all: br8-start
.PHONY: all clean
