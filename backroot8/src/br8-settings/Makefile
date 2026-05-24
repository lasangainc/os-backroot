CC = gcc
CFLAGS = -Wall -Wextra -O2 -I../../include $(shell pkg-config --cflags xft 2>/dev/null)
LDFLAGS = -lX11 -lm $(shell pkg-config --libs xft 2>/dev/null)

br8-settings: br8-settings.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f br8-settings

all: br8-settings
.PHONY: all clean
