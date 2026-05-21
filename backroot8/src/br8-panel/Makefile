CC = gcc
CFLAGS = -Wall -Wextra -O2 $(shell pkg-config --cflags xft 2>/dev/null)
LDFLAGS = -lX11 -lm $(shell pkg-config --libs xft 2>/dev/null)
ROOT = ../..
EMBLEM_SRC = $(ROOT)/assets/taskbar-emblem.png
EMBLEM_H = emblem.h

br8-panel: br8-panel.c $(EMBLEM_H) ../br8-start/stb_image.h
	$(CC) $(CFLAGS) -o $@ br8-panel.c $(LDFLAGS)

$(EMBLEM_H): $(EMBLEM_SRC) $(ROOT)/scripts/png-to-emblem-h.py
	python3 $(ROOT)/scripts/png-to-emblem-h.py $(EMBLEM_SRC) $@

clean:
	rm -f br8-panel

all: br8-panel
.PHONY: all clean
