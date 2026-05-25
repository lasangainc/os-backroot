CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lcrypt

br8-chkpass: br8-chkpass.c
	$(CC) $(CFLAGS) -o $@ br8-chkpass.c $(LDFLAGS)

clean:
	rm -f br8-chkpass

all: br8-chkpass
.PHONY: all clean
