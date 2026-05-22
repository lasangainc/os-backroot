CC = gcc
CFLAGS = -Wall -Wextra -O2 -I../../include \
	$(shell pkg-config --cflags poppler-glib cairo 2>/dev/null)
LDFLAGS = -lX11 $(shell pkg-config --libs poppler-glib cairo 2>/dev/null)

powerpdf: power-pdf.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f powerpdf

.PHONY: clean
