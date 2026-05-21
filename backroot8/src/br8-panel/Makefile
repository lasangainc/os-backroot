CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lX11

br8-panel: br8-panel.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f br8-panel

all: br8-panel
.PHONY: all clean
