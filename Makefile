CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
PREFIX ?= /usr/local

cLife3: cLife3.c
	$(CC) $(CFLAGS) -o cLife3 cLife3.c -lm

install: cLife3
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 cLife3 $(DESTDIR)$(PREFIX)/bin/cLife3

clean:
	rm -f cLife3
